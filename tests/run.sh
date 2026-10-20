#!/usr/bin/env bash
# Executa cada teste em um processo separado e explica as falhas.
#   1. correção: -O1 com AddressSanitizer + UndefinedBehaviorSanitizer
#   2. desempenho: -O2 sem sanitizers, tempo comparado a uma referência
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"

pass=0
fail=0
ok()  { printf '  \342\234\224 %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; fail=$((fail + 1)); }

# Primeiro quadro da pilha que aponta para src/ ou tests/: "Função (arquivo:linha)".
first_frame() {
  grep -E '^ *#[0-9]+ .* in .*(src|tests)/[^ ]+:[0-9]+' | head -1 |
    sed -E 's/^ *#[0-9]+ 0x[0-9a-f]+ in ([^(]+)(\(.*\))? ([^ ]+)$/\1 (\3)/; s#\([^ (]*/((src|tests)/)#(\1#'
}

explain() {  # explain <log> <exit-code> <timeout-s>
  local log=$1 rc=$2 limit=$3 line
  local -a reasons=()
  line=$(grep -m1 '^FALHA: ' "$log" | sed 's/^FALHA: //')
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line em $(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)")
  fi
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#')")
  fi
  if ((rc == 124 || rc == 137)); then
    reasons+=("tempo esgotado ($limit s) — um laço que não termina, ou um custo que cresce muito mais rápido que a entrada?")
  elif ((rc == 139)) && ! grep -q 'ERROR: AddressSanitizer' "$log"; then
    reasons+=("falha de segmentação (SIGSEGV) — estouro de pilha por recursão profunda?")
  fi
  if ((rc != 0)) && ! grep -q '^FALHA: ' "$log"; then
    line=$(grep '^PASSO: ' "$log" | tail -1 | sed 's/^PASSO: //')
    [[ -n "$line" ]] && reasons+=("último passo alcançado: $line")
  fi
  if ((${#reasons[@]} == 0)); then
    reasons+=("o processo terminou com código $rc")
  fi
  local first=1 r
  for r in "${reasons[@]}"; do
    if ((first)); then printf '%s' "$r"; first=0; else printf '\n              %s' "$r"; fi
  done
}

compile() {  # compile <alvo> <descrição>
  if make --no-print-directory "$1" >"build/compile-${1##*/}.log" 2>&1; then
    ok "$2"
    return 0
  fi
  bad "$2" "erros de compilação (log completo: build/compile-${1##*/}.log)"
  grep -E "error:" "build/compile-${1##*/}.log" | head -10 | cut -c1-200 | sed 's/^/        /'
  return 1
}

run_suite() {  # run_suite <binário> <timeout-s>
  local bin=$1 limit=$2 name desc log rc
  while IFS=$'\t' read -r name desc; do
    [[ -z "$name" ]] && continue
    log="$LOGS/$name.log"
    rc=0
    timeout --kill-after=5 "$limit" "$bin" "$name" >"$log" 2>&1 || rc=$?
    if ((rc == 0)) && grep -qx 'OK' "$log"; then
      ok "$desc"
      if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then grep '^PASSO: ' "$log" | sed 's/^PASSO: /        · /'; fi
    else
      bad "$desc [$name]" "$(explain "$log" "$rc" "$limit")"
      if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/        | /' "$log"; fi
    fi
  done < <("$bin" --list)
}

printf '==> 1. Correção (g++ %s, -std=c++17 -pedantic-errors, ASan + UBSan; um processo por teste)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"
if compile build/test-runner "src/ e tests/ compilam em C++17 com sanitizers"; then
  run_suite build/test-runner 60
fi

echo
echo "==> 2. Desempenho (-O2, sem sanitizers; limites relativos a uma referência medida na mesma máquina)"
if compile build/perf-runner "src/ e tests/ compilam em C++17 com -O2"; then
  run_suite build/perf-runner 45
fi

total=$((pass + fail))
echo
if ((fail > 0)); then
  printf '%d de %d critérios passaram. Detalhes: build/logs/<teste>.log (ou GQ_VERBOSE=1 ./test.sh)\n' "$pass" "$total"
  exit 1
fi
printf 'Todos os %d critérios passaram: rotas exatas, buscas corretas e consultas rápidas.\n' "$total"
