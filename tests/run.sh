#!/usr/bin/env bash
# Avaliador do desafio-12. Roda dentro do container (via `make test`) ou
# localmente com GITQUEST_LOCAL=1. Cada critério é reportado com ✔/✘.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

BUILD=build
LOGS=$BUILD/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"

passed=0
failed=0
ok() { printf '  ✔ %s\n' "$1"; passed=$((passed + 1)); }
ko() {
  printf '  ✘ %s\n' "$1"
  if [[ -n "${2:-}" ]]; then printf '%s\n' "$2" | sed 's/^/      /'; fi
  failed=$((failed + 1))
}
section() { printf '\n%s\n' "$1"; }
first_errors() { grep -E 'error' | head -n "${1:-6}" | sed -E 's#^.*/(src|tests)/#\1/#' | cut -c1-220; }

# Primeiro quadro da pilha em src/ ou tests/: "função (arquivo:linha)".
first_frame() {
  grep -E '^ *#[0-9]+ .* in .*(src|tests)/[^ ]+:[0-9]+' | head -1 |
    sed -E 's/^ *#[0-9]+ 0x[0-9a-f]+ in (.*) ([^ ]*\/)?((src|tests)\/[^ ]+)$/\1 (\3)/' | cut -c1-200
}

explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 kind
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    kind=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    printf 'AddressSanitizer: %s em %s\n' "$kind" "$(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)"
  elif grep -q 'ERROR: LeakSanitizer' "$log"; then
    printf 'LeakSanitizer: vazamento; alocado em %s\n' "$(sed -n '/ERROR: LeakSanitizer/,$p' "$log" | first_frame)"
  elif grep -q 'runtime error:' "$log"; then
    printf 'UBSan: %s\n' "$(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#')"
  elif ((rc == 124 || rc == 137)); then
    printf 'tempo esgotado (30 s) — o trabalho cresce mais rápido que a entrada?\n'
    grep -m3 '^linha ' "$log"
  elif grep -q '^linha ' "$log"; then
    grep '^linha ' "$log" | head -n 4 | cut -c1-240
  else
    printf 'o processo terminou com código %d\n' "$rc"
    tail -n 3 "$log"
  fi
}

if [[ -n "${SAN+set}" && -z "$SAN" ]]; then san_label="sem sanitizers"; else san_label="ASan + UBSan"; fi

# ---------------------------------------------------------------------------
section "1. Compilação (g++ $(${CXX:-g++} -dumpfullversion 2>/dev/null || echo '?'), -std=c++23 -pedantic-errors, $san_label)"
runtime_ok=0
if out=$(make --no-print-directory -s "$BUILD/runtime_tests" 2>&1); then
  ok "tests/runtime_tests.cpp compila com src/logscan.cpp"
  runtime_ok=1
else
  ko "tests/runtime_tests.cpp não compila com src/logscan.cpp (as assinaturas de src/gq/logscan.hpp precisam continuar valendo)" \
     "$(printf '%s\n' "$out" | first_errors 8)"
fi
if out=$(make --no-print-directory -s "$BUILD/logscan" 2>&1); then
  if timeout 30 "$BUILD/logscan" >"$LOGS/logscan.log" 2>&1; then
    ok "o relatório build/logscan executa sem erros"
  else
    ko "o relatório build/logscan terminou com erro" "$(explain "$LOGS/logscan.log" $?)"
  fi
else
  ko "src/main.cpp não compila com src/logscan.cpp" "$(printf '%s\n' "$out" | first_errors 6)"
fi

# ---------------------------------------------------------------------------
section "2. Comportamento (um processo por teste; logs em $LOGS/)"
if ((runtime_ok)); then
  while IFS=$'\t' read -r id title; do
    [[ -z "$id" ]] && continue
    log="$LOGS/$id.log"
    rc=0
    timeout --kill-after=5 30 "$BUILD/runtime_tests" "$id" >"$log" 2>&1 || rc=$?
    if ((rc == 0)); then
      ok "$title"
    else
      ko "$title  [$id]" "$(explain "$log" "$rc")"
      if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/      | /' "$log" | head -n 60; fi
    fi
  done < <("$BUILD/runtime_tests" --list | tr -d '\r')
else
  ko "testes de comportamento não executados" "corrija a compilação primeiro"
fi

# ---------------------------------------------------------------------------
total=$((passed + failed))
printf '\nResultado: %d/%d critérios satisfeitos.\n' "$passed" "$total"
if ((failed > 0)); then
  printf 'Ainda faltam %d. Log completo: GQ_VERBOSE=1 ./test.sh · Dicas: ./scripts/gitquest hints 1\n' "$failed"
  exit 1
fi
printf 'O leitor é preguiçoso, fecha o que abre e não esconde erros. Desafio concluído!\n'
