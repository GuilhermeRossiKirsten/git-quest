#!/usr/bin/env bash
# Executa cada teste em um processo separado, com AddressSanitizer,
# UndefinedBehaviorSanitizer e LeakSanitizer ligados, e explica as falhas.
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

BIN=build/test-runner
LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export LSAN_OPTIONS="report_objects=0"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"

ok()  { printf '  \342\234\224 %s\n' "$1"; }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; }

# Primeiro quadro da pilha que aponta para src/ ou tests/ (ignorando o operator
# new/delete instrumentado de tests/support/): "Função (arquivo:linha)".
first_frame() {
  grep -E '^ *#[0-9]+ .* in .*(src|tests)/[^ ]+:[0-9]+' | grep -v 'tests/support/' | head -1 |
    sed -E 's#tests/\.\./src/#src/#
            s/^ *#[0-9]+ 0x[0-9a-f]+ in (.+) ([^ ]+:[0-9]+(:[0-9]+)?)$/\1 (\2)/
            s#\([^ (]*/((src|tests)/)#(\1#' | cut -c1-180
}

explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 line
  local -a reasons=()
  line=$(grep -m1 '^FALHA: ' "$log" | sed 's/^FALHA: //')
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line em $(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)")
    line=$(sed -n '/freed by thread/,$p' "$log" | first_frame)
    [[ -n "$line" ]] && reasons+=("  (a memória foi liberada em $line)")
  fi
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#' | cut -c1-220)")
  fi
  if grep -q 'ERROR: LeakSanitizer' "$log"; then
    line=$(grep -m1 'SUMMARY: AddressSanitizer: .* leaked' "$log" |
      sed -E 's/.*: ([0-9]+) byte\(s\) leaked in ([0-9]+) allocation\(s\).*/\1 byte(s) em \2 alocação(ões)/')
    reasons+=("LeakSanitizer: vazamento de $line")
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

printf '==> Compilando src/ e tests/ (g++ %s, -std=c++20 -pedantic-errors, ASan + UBSan)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"
if ! make --no-print-directory "$BIN" >build/compile.log 2>&1; then
  bad "o projeto compila em C++20" "erros de compilação (log completo: build/compile.log)"
  grep -E "error:|^(tests|src)/[^ ]*: +required from" build/compile.log | head -12 | cut -c1-200 |
    sed 's#^tests/\.\./src/#src/#; s/^/        /'
  if grep -qE '^tests/[^ ]*:[0-9]+:[0-9]+: error' build/compile.log; then
    echo "      dica: o erro surge ao compilar tests/: o código em src/ não oferece algo"
    echo "            que o teste usa (veja a linha indicada em tests/ e o contrato nos headers)."
  fi
  exit 1
fi
ok "o projeto compila em C++20 com -pedantic-errors"

pass=1
fail=0

echo "==> Executando os testes (um processo por teste; logs em $LOGS/)"
while IFS=$'\t' read -r name desc; do
  [[ -z "$name" ]] && continue
  log="$LOGS/$name.log"
  rc=0
  timeout --kill-after=5 90 "$BIN" "$name" >"$log" 2>&1 || rc=$?
  if ((rc == 0)) && grep -qx 'OK' "$log"; then
    ok "$desc"
    pass=$((pass + 1))
  else
    bad "$desc [$name]" "$(explain "$log" "$rc")"
    fail=$((fail + 1))
    if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/        | /' "$log"; fi
  fi
done < <("$BIN" --list)

total=$((pass + fail))
echo
if ((total <= 1)); then
  echo "nenhum teste foi executado"
  exit 1
fi
if ((fail > 0)); then
  printf '%d de %d critérios passaram. Detalhes: build/logs/<teste>.log (ou GQ_VERBOSE=1 ./test.sh)\n' "$pass" "$total"
  exit 1
fi
printf 'Todos os %d critérios passaram: a arena alinha, destrói, não vaza e nada escapa para o heap global.\n' "$total"
