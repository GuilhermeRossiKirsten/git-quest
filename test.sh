#!/usr/bin/env bash
# Avaliador de desafio-17: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                      # padrão: Docker (go vet + go test -race)
#   GITQUEST_LOCAL=1 ./test.sh     # alternativa: Go >= 1.25 local, Linux/macOS (cgo para -race)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

# Critério -> teste (função de nível superior em tests/).
CRITERIA=(
  "TestRequestID_PropagaEGera|request id: gerado ou preservado, no contexto e na resposta"
  "TestLogging_RegistraStatus|log estruturado registra o status real de cada requisição"
  "TestRecover_PanicoVira500|pânico no handler vira 500 (ErrAbortHandler é repassado)"
  "TestTimeout_RespondeNoPrazo|timeout por requisição responde 503 no prazo"
  "TestTimeout_SemVazamentoNemCorrida|timeout sem goroutine vazada nem escrita tardia na resposta"
  "TestTimeout_PanicoPropagado|pânico sob o timeout é capturado por Recover (processo sobrevive)"
  "TestPedidos_ExpiradoNaoGrava|pedido que expirou não é gravado no journal"
  "TestJournal_CloseGravaTudo|Close grava no disco tudo o que foi aceito"
  "TestJournal_ConcorrenciaIntegra|journal íntegro com escritas e descargas concorrentes"
  "TestJournal_WorkerParaComContexto|worker do journal para pelo contexto, descarrega e não vaza"
  "TestBinario_DesligamentoGracioso|SIGTERM: readyz 503, drenagem, pedidos em andamento concluem, journal completo, saída 0"
  "TestBinario_PrazoEstourado|SIGTERM com requisição travada: espera o prazo, força o fechamento, saída ≠ 0"
  "TestBinario_PanicoNaoDerruba|pedido que provoca pânico não derruba o serviço"
)

if [[ "${GITQUEST_LOCAL:-}" == 1 ]]; then
  [[ "$(gq_os)" != windows ]] || gq_die "o modo local precisa de sinais POSIX (Linux/macOS/WSL); no Windows use o modo Docker (padrão)."
  gq_require_cmd go "Instale Go >= 1.25 ou use o modo Docker (padrão)."
  gq_require_cmd make "Instale make ou use o modo Docker (padrão)."
  run() { "$@"; }
else
  IMAGE=$(gq_toolchain_image desafio-17 Dockerfile)
  GQ_DOCKER_EXTRA=(-v gitquest-desafio-17-gocache:/gocache)
  gq_require_docker
  gq_docker_ensure_image "$IMAGE" Dockerfile
  run() { gq_docker_run "$IMAGE" 300 "$@"; }
fi

gq_title "desafio-17 · go vet + go test -race (pode levar ~1 min na primeira vez)"
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
    $0 ~ "^=== (RUN|CONT|NAME) +" t "(/|$)" { on = 1; next }
    $0 ~ "^=== (RUN|CONT|NAME) " && $0 !~ "^=== (RUN|CONT|NAME) +" t "(/|$)" { on = 0 }
    on && /^ +[A-Za-z_]+_test\.go:[0-9]+:/ { sub(/^ +/, ""); print "        " $0; n++ }
    on && /^ {8,}[^ ]/ && !/_test\.go:/ && n > 0 && n < 40 { print "    " $0 }
  ' build/test.log | head -15
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
  printf '  %s✘%s a suíte estourou o tempo limite de 120s — algo não está parando\n' "$GQ_RED" "$GQ_RESET"
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
