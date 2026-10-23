#!/usr/bin/env bash
# Avaliação completa do desafio-30, chamada por `make test`:
#   1. testes funcionais (ASan + UBSan): cada tests/test_*.cpp vira um binário e
#      cada teste roda num processo separado; respostas comparadas ao gabarito;
#   2. medição de cache (tests/perf.sh): Ir e misses de D1/LL de cada consulta.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export LSAN_OPTIONS="report_objects=0"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"

ok()  { printf '  \342\234\224 %s\n' "$1"; }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; }

# Primeiro quadro da pilha que aponta para src/ ou tests/: "Função (arquivo:linha)".
first_frame() {
  grep -E '^ *#[0-9]+ .* in .*(src|tests)/[^ ]+:[0-9]+' | head -1 |
    sed -E 's/^ *#[0-9]+ 0x[0-9a-f]+ in ([^(]+)(\(.*\))? ([^ ]+)$/\1 (\3)/; s#\([^ (]*/((src|tests)/)#(\1#'
}

explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 line
  local -a reasons=()
  line=$(grep -m1 '^FALHA: ' "$log" | sed 's/^FALHA: //' | cut -c1-300)
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line em $(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)")
  fi
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#')")
  fi
  if grep -q 'ERROR: LeakSanitizer' "$log"; then
    reasons+=("LeakSanitizer: vazamento; alocado em $(sed -n '/ERROR: LeakSanitizer/,$p' "$log" | first_frame)")
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

printf '==> Testes funcionais (g++ %s, -std=c++20 -pedantic-errors, ASan + UBSan)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"

pass=0
fail=0
for src in tests/test_*.cpp; do
  base=$(basename "$src" .cpp)
  bin=build/san/bin/$base
  echo "--- $src"
  if ! make --no-print-directory "$bin" >"$LOGS/$base.compile.log" 2>&1; then
    bad "$src compila com o seu src/" "erros de compilação (log completo: $LOGS/$base.compile.log)"
    grep -E "error:" "$LOGS/$base.compile.log" | head -6 | cut -c1-200 | sed 's/^/        /'
    echo "      dica: a API pública descrita no topo de src/sales_store.h não pode mudar."
    n=$(grep -cE '^TEST\(' "$src")
    fail=$((fail + n))
    continue
  fi
  while IFS=$'\t' read -r name desc; do
    [[ -z "$name" ]] && continue
    log="$LOGS/$name.log"
    rc=0
    timeout --kill-after=5 120 "$bin" "$name" >"$log" 2>&1 || rc=$?
    if ((rc == 0)) && grep -qx 'OK' "$log"; then
      ok "$desc"
      pass=$((pass + 1))
    else
      bad "$desc [$name]" "$(explain "$log" "$rc")"
      fail=$((fail + 1))
      if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/        | /' "$log"; fi
    fi
  done < <("$bin" --list)
done

echo
bash tests/perf.sh
read -r ppass pfail <build/perf/result 2>/dev/null || { ppass=0; pfail=1; }
pass=$((pass + ppass))
fail=$((fail + pfail))

total=$((pass + fail))
echo
if ((total == 0)); then
  echo "nenhum critério foi avaliado"
  exit 1
fi
if ((fail > 0)); then
  printf '%d de %d critérios passaram. Detalhes: build/logs/ (ou GQ_VERBOSE=1 ./test.sh)\n' "$pass" "$total"
  exit 1
fi
printf 'Todos os %d critérios passaram: as respostas estão certas e os dados finalmente cabem na cache.\n' "$total"
