#!/usr/bin/env bash
# Avaliador de desafio-24: código de saída 0 somente se todos os critérios passarem.
# Sobe o Redis do compose se preciso e roda go vet + go test -race num container
# ligado à rede interna do compose. Cada teste esvazia o banco lógico 0 do Redis
# e usa um banco de produtos simulado próprio (internal/productdb).
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

# Critério -> teste (função de nível superior em tests/).
CRITERIA=(
  "TestCache_CompartilhadoEntreReplicas|o cache é usado e compartilhado: leituras repetidas, de qualquer réplica, não voltam ao banco"
  "TestChaves_SemColisao|lojas diferentes com o mesmo ID (e IDs com ':' e espaços) nunca recebem o produto uma da outra"
  "TestInvalidacao_LeituraDuranteEscrita|depois de SaveProduct, todas as réplicas leem o valor novo — mesmo com leitura durante a gravação"
  "TestInvalidacao_CorridaLeituraLenta|uma leitura lenta iniciada antes da gravação não repovoa o cache com o valor antigo"
  "TestStampede_UmaIdaAoBanco|50 leituras simultâneas de um produto fora do cache: 1 consulta ao banco, sem bloquear outros produtos"
  "TestTTL_ComJitter|TTL de produtos entre 5 e 15 min, com expirações espalhadas (≤ 5% no mesmo segundo, janela ≥ 100 s)"
  "TestNegativo_IdInexistente|IDs inexistentes: no máximo 1 consulta ao banco por ID, entrada negativa expira em ≤ 60 s"
  "TestRedisFora_Degrada|Redis travado ou fora do ar: leituras servidas do banco e gravações concluídas em ≤ 1 s"
)

gq24_require
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  gq_warn "imagem de toolchain ausente — construindo (prefira rodar ./start.sh antes)"
  gq_docker_build "$IMAGE" - Dockerfile
fi
gq24_up

gq_title "desafio-24 · go vet + go test -race contra o Redis (cerca de 20–40 s)"
rm -f build/test.log build/vet.log
rc=0
run make --no-print-directory test || rc=$?

[[ -f build/test.log ]] || gq_die "os testes não chegaram a rodar (código $rc). Rode ./start.sh para preparar o ambiente."

if grep -q '\[build failed\]\|\[setup failed\]' build/test.log; then
  gq_error "o código não compila:"
  grep -v '^\(ok\|FAIL\|---\|===\)' build/test.log | head -30 >&2
  exit 1
fi

# failure_details <TestName>: mensagens t.Errorf/t.Fatalf do teste (sem o ruído do -v).
failure_details() {
  awk -v t="$1" '
    $0 ~ "^=== RUN   " t "(/|$)" { on = 1; next }
    $0 ~ "^=== RUN   " && $0 !~ "^=== RUN   " t "(/|$)" { on = 0 }
    on && /^ +[A-Za-z_]+_test\.go:[0-9]+:/ { sub(/^ +/, ""); print "        " $0 }
  ' build/test.log | head -8
}

pass=0 fail=0
echo
for entry in "${CRITERIA[@]}"; do
  name=${entry%%|*} desc=${entry#*|}
  if grep -q "^--- PASS: $name " build/test.log; then
    printf '  %s✔%s %s\n' "$GQ_GREEN" "$GQ_RESET" "$desc"; pass=$((pass + 1))
  elif grep -q "^--- FAIL: $name " build/test.log; then
    printf '  %s✘%s %s\n' "$GQ_RED" "$GQ_RESET" "$desc"; fail=$((fail + 1))
    failure_details "$name"
  else
    printf '  %s✘%s %s — não executado (pânico, data race ou timeout; veja build/test.log)\n' \
      "$GQ_RED" "$GQ_RESET" "$desc"; fail=$((fail + 1))
  fi
done

if grep -q 'WARNING: DATA RACE' build/test.log; then
  printf '  %s✘%s race detector encontrou data race(s) — veja build/test.log\n' "$GQ_RED" "$GQ_RESET"
  fail=$((fail + 1))
fi
if grep -q '^panic: test timed out' build/test.log; then
  printf '  %s✘%s a suíte estourou o tempo limite de 180s — algo travou (lock esquecido? chamada sem prazo?)\n' "$GQ_RED" "$GQ_RESET"
  fail=$((fail + 1))
fi
if [[ -s build/vet.log ]] && grep -qv '^#' build/vet.log; then
  printf '  %s✘%s go vet encontrou problemas:\n' "$GQ_RED" "$GQ_RESET"; sed 's/^/        /' build/vet.log | head -10
  fail=$((fail + 1))
else
  printf '  %s✔%s go vet sem alertas\n' "$GQ_GREEN" "$GQ_RESET"; pass=$((pass + 1))
fi

echo
if ((fail == 0 && rc == 0)); then
  gq_ok "$pass critério(s) satisfeito(s). Desafio concluído!"
  exit 0
fi
((fail == 0)) && gq_error "a suíte retornou código $rc — veja build/test.log"
gq_error "$fail critério(s) pendente(s), $pass satisfeito(s). Log completo: build/test.log"
exit 1
