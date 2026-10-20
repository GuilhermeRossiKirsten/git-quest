#!/usr/bin/env bash
# Executa os critérios do desafio. Chamado por `make test` (dentro do container
# ou localmente). Código de saída 0 somente se todos os critérios passarem.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

export ASAN_OPTIONS="detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1:color=never"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:color=never"

failed=0
ok()   { printf '✔ %s\n' "$1"; }
fail() { printf '✘ %s\n' "$1"; [[ -n "${2:-}" ]] && printf '    motivo: %s\n' "$2"; failed=$((failed + 1)); }

# Resume um relatório de sanitizer: o erro, o acesso e os quadros da pilha que
# estão no código do desafio (src/ e tests/). O relatório completo fica no log.
show_report() {
  grep -E 'ERROR: |runtime error|READ of|WRITE of|freed by|previously allocated|is located|<== Memory|SUMMARY| (src|tests)/[^ ]*:[0-9]+' "$1" \
    | grep -v 'Shadow' | head -24 | sed 's/^/    | /'
  printf '    | (relatório completo: %s)\n' "$1"
}

mkdir -p build
log=build/compile.log
if make --no-print-directory build/test_config build/relay >"$log" 2>&1; then
  ok "o código compila com -std=c++17 -pedantic-errors e a API usa std::optional/std::variant"
else
  fail "o código compila com -std=c++17 -pedantic-errors e a API usa std::optional/std::variant" \
       "erro de compilação (os static_assert dos testes conferem as assinaturas de config.hpp)"
  grep -E 'error|static assertion' "$log" | head -20 | sed 's/^/    | /'
  printf '\n%d critério(s) não atendido(s).\n' "$failed"
  exit 1
fi

SAN_RE='ERROR: (AddressSanitizer|LeakSanitizer)|runtime error'

run_group() {
  local group=$1 title=$2 out=build/group-$1.log rc=0
  ./build/test_config "$group" >"$out" 2>&1 || rc=$?
  if ((rc == 0)); then
    ok "$title"
    return
  fi
  if grep -qE "$SAN_RE" "$out"; then
    # O processo foi abortado no meio de um caso: descobre qual pela ordem.
    local finished current
    finished=$(grep -cE '^    [✔✘] ' "$out" || true)
    current=$(./build/test_config --list "$group" | sed -n "$((finished + 1))p")
    fail "$title" "os sanitizers abortaram o caso \"$current\""
    grep -E '^    ✘|^        ' "$out" || true
    show_report "$out"
  elif grep -qE '^    ✘' "$out"; then
    fail "$title" "casos reprovados abaixo"
    grep -E '^    ✘|^        ' "$out"
  else
    fail "$title" "o processo de testes terminou com código $rc"
    tail -5 "$out" | sed 's/^/    | /'
  fi
}

run_group api        "API: ausência é representada por std::nullopt / std::monostate"
run_group presenca   "valores legítimos (-1, 0, 0.0, vazio) não são confundidos com ausência"
run_group conversao  "conversões numéricas rejeitam texto inválido e overflow sem lançar exceções"
run_group booleanos  "get_bool reconhece só true/yes/on/1 e false/no/off/0 (sem diferenciar maiúsculas)"
run_group tipos      "typed() escolhe a alternativa correta do std::variant"
run_group formato    "formato: tabs, CRLF, comentários, seções e chave duplicada (a última vence)"
run_group memoria    "std::string_view devolvidos apontam para dados vivos (AddressSanitizer)"

# Critério ponta a ponta: o serviço relay com a configuração de produção.
title="serviço relay lê tests/fixtures/relay.ini corretamente"
out=build/relay.out
if ./build/relay tests/fixtures/relay.ini >"$out" 2>&1 && diff -u tests/fixtures/relay.expected "$out" >build/relay.diff; then
  ok "$title"
else
  if grep -qE 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error|terminate called' "$out"; then
    fail "$title" "o serviço abortou (veja abaixo)"
    show_report "$out"
    grep -m3 'terminate called\|what()' "$out" | sed 's/^/    | /'
  else
    diff -u tests/fixtures/relay.expected "$out" >build/relay.diff || true
    fail "$title" "saída diferente da esperada (- esperado, + obtido)"
    tail -n +3 build/relay.diff | sed 's/^/    | /'
  fi
fi

echo
if ((failed == 0)); then
  echo "Todos os critérios foram atendidos."
  exit 0
fi
printf '%d critério(s) não atendido(s).\n' "$failed"
exit 1
