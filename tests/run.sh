#!/usr/bin/env bash
# Executa cada teste funcional em um processo separado (ASan + UBSan + asserções da
# libstdc++) e depois o teste de desempenho (-O2), explicando as falhas.
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

BIN=build/test-runner
PERF=build/heap-perf
LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"

ok()  { printf '  \342\234\224 %s\n' "$1"; }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; }

explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 line
  local -a reasons=()
  line=$(grep -m1 '^FALHA: ' "$log" | sed 's/^FALHA: //; s#^tests/##')
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'Assertion .* failed' "$log"; then
    line=$(grep -m1 'Assertion .* failed' "$log" | sed -E 's/.*(Assertion .* failed).*/\1/')
    reasons+=("libstdc++: $line — índice fora dos limites do std::vector (posição guardada no mapa já não existe?)")
  fi
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line (GQ_VERBOSE=1 ./test.sh mostra a pilha)")
  fi
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#')")
  fi
  if ((rc == 124 || rc == 137)); then
    reasons+=("tempo esgotado (laço infinito?)")
  fi
  if ((${#reasons[@]} == 0)); then
    reasons+=("o processo terminou com código $rc")
  fi
  local first=1 r
  for r in "${reasons[@]}"; do
    if ((first)); then printf '%s' "$r"; first=0; else printf '\n              %s' "$r"; fi
  done
}

printf '==> Compilando src/ e tests/ (g++ %s, -std=c++17 -pedantic-errors)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"
if ! make --no-print-directory "$BIN" "$PERF" >build/compile.log 2>&1; then
  bad "o projeto compila em C++17" "erros de compilação (log completo: build/compile.log)"
  grep -E "error:|^(tests|src)/[^ ]*: +required from" build/compile.log | head -12 | cut -c1-200 |
    sed -E 's#^tests/(\.\./)+src/#src/#; s/^/        /'
  exit 1
fi
ok "o projeto compila em C++17 com -pedantic-errors"

echo "==> Testes funcionais (um processo por teste, ASan + UBSan; logs em $LOGS/)"
pass=0
fail=0
while IFS=$'\t' read -r name desc; do
  [[ -z "$name" ]] && continue
  log="$LOGS/$name.log"
  rc=0
  timeout --kill-after=5 60 "$BIN" "$name" >"$log" 2>&1 || rc=$?
  if ((rc == 0)) && grep -qx 'OK' "$log"; then
    ok "$desc"
    pass=$((pass + 1))
  else
    bad "$desc [$name]" "$(explain "$log" "$rc")"
    fail=$((fail + 1))
    if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/        | /' "$log"; fi
  fi
done < <("$BIN" --list)

echo "==> Desempenho (-O2, sem sanitizers): 1 milhão de operações mistas"
desc="push/pop/update_priority/erase em O(log n): até 3x o tempo de um modelo std::set"
log="$LOGS/desempenho.log"
rc=0
timeout --kill-after=5 120 "$PERF" >"$log" 2>&1 || rc=$?
if ((rc == 0)) && grep -q '^OK' "$log"; then
  ok "$desc ($(sed -n 's/^OK //p' "$log"))"
  pass=$((pass + 1))
else
  bad "$desc [desempenho]" "$(explain "$log" "$rc")"
  fail=$((fail + 1))
fi

total=$((pass + fail))
echo
if ((fail > 0)); then
  printf '%d de %d critérios passaram. Detalhes: build/logs/<teste>.log (ou GQ_VERBOSE=1 ./test.sh)\n' "$pass" "$total"
  exit 1
fi
printf 'Todos os %d critérios passaram: o heap está correto, estável nos empates e rápido.\n' "$total"
