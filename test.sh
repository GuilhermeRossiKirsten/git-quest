#!/usr/bin/env bash
# Avaliador de desafio-22: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                      # padrão: Docker (go vet + go test -race)
#   GITQUEST_LOCAL=1 ./test.sh     # alternativa: Go >= 1.25 local (cgo para -race)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

# Critério -> teste (função de nível superior em tests/).
CRITERIA=(
  "TestServer_EcoBasico|eco de mensagens pequenas (o caminho feliz)"
  "TestFrame_LeiturasParciais|ReadFrame monta frames com leituras parciais (1 byte, metade, pedaços)"
  "TestFrame_EOF|fim limpo do fluxo é io.EOF; frame cortado no meio é io.ErrUnexpectedEOF"
  "TestFrame_TamanhoMaximo|frame acima de MaxSize vira ErrTooLarge sem alocar o tamanho anunciado"
  "TestFrame_FormatoNoFio|cabeçalho uint32 big-endian: mensagens > 64 KiB chegam intactas"
  "TestServer_FragmentacaoTCP|servidor ecoa frames fatiados e atrasados por um proxy TCP"
  "TestServer_PipelineEmOrdem|3000 pedidos em pipeline: ecos íntegros e na mesma ordem"
  "TestServer_IdleTimeout|conexão muda e slowloris caem; cliente lento mas ativo continua"
  "TestServer_Backpressure|cliente que não lê é contido (memória limitada); os outros seguem atendidos"
  "TestServer_ConsumidorLento|consumidor que não lê é desconectado depois de WriteTimeout"
  "TestServer_CloseEncerraTudo|Close fecha as conexões, Serve devolve ErrServerClosed e nada vaza"
)

if [[ "${GITQUEST_LOCAL:-}" == 1 ]]; then
  gq_require_cmd go "Instale Go >= 1.25 ou use o modo Docker (padrão)."
  gq_require_cmd make "Instale make ou use o modo Docker (padrão)."
  run() { "$@"; }
else
  IMAGE=$(gq_toolchain_image desafio-22 Dockerfile)
  GQ_DOCKER_EXTRA=(-v gitquest-desafio-22-gocache:/gocache)
  gq_require_docker
  gq_docker_ensure_image "$IMAGE" Dockerfile
  run() { gq_docker_run "$IMAGE" 300 "$@"; }
fi

gq_title "desafio-22 · go vet + go test -race (pode levar ~1 min na primeira vez)"
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
    $0 ~ "^=== (RUN|CONT|NAME|PAUSE) +" t "(/|$)" { on = 1; next }
    $0 ~ "^=== (RUN|CONT|NAME|PAUSE) " && $0 !~ "^=== (RUN|CONT|NAME|PAUSE) +" t "(/|$)" { on = 0 }
    on && /^ +[A-Za-z_]+_test\.go:[0-9]+:/ { sub(/^ +/, ""); print "        " $0; n++ }
    on && /^ {8,}[^ ]/ && !/_test\.go:/ && n > 0 && n < 40 { print "    " $0 }
  ' build/test.log | head -12
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
  printf '  %s✘%s a suíte estourou o tempo limite de 240s — algo não está parando\n' "$GQ_RED" "$GQ_RESET"
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
