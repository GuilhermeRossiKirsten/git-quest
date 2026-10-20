#!/usr/bin/env bash
# Avaliação do desafio-5. Roda DENTRO do container gcc:14.2.0 (via test.sh) ou
# localmente com GITQUEST_LOCAL=1. Imprime um ✔/✘ por critério e termina com
# código 0 somente se todos passarem.
set -uo pipefail
cd "$(dirname "$0")/.."

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  GREEN=$'\033[32m' RED=$'\033[31m' BOLD=$'\033[1m' RESET=$'\033[0m'
else
  GREEN='' RED='' BOLD='' RESET=''
fi

passed=0
failed=0
LOG=build/logs
mkdir -p "$LOG"

# Resume um relatório do ASan/UBSan: o erro, os quadros da pilha em src/ e o SUMMARY.
san_excerpt() {
  grep -h -E 'ERROR: |runtime error:|SUMMARY:|^    #[0-9]+ .*src/|^(freed|previously allocated) by' "$1" |
    sed -E 's/^==[0-9]+==//; s/ at pc 0x[0-9a-f]+ bp 0x[0-9a-f]+ sp 0x[0-9a-f]+//; s/^ +(#[0-9]+) 0x[0-9a-f]+ in /    \1 /; s/std::basic_string_view<char, std::char_traits<char> >/std::string_view/g' |
    head -n 14
}

pass() { printf '%s✔%s %s\n' "$GREEN" "$RESET" "$1"; passed=$((passed + 1)); }
fail() {
  printf '%s✘%s %s\n' "$RED" "$RESET" "$1"
  failed=$((failed + 1))
  if [[ -n "${2:-}" && -s "$2" ]]; then
    head -n "${3:-25}" "$2" | sed 's/^/    │ /'
  fi
}
finish() {
  echo
  if ((failed == 0)); then
    printf '%s%d de %d critérios atendidos. Desafio concluído!%s\n' "$BOLD" "$passed" "$passed" "$RESET"
    exit 0
  fi
  printf '%s%d de %d critérios atendidos.%s Corrija os itens com ✘ e rode ./test.sh de novo.\n' \
    "$BOLD" "$passed" "$((passed + failed))" "$RESET"
  exit 1
}

printf '%sdesafio-5 · Erros que não se escondem%s\n\n' "$BOLD" "$RESET"

# 1. Toolchain: sem estes recursos o resto não faz sentido.
if make -s toolchain >"$LOG/toolchain.txt" 2>&1; then
  pass "toolchain: $(${CXX:-g++} --version | head -n1) com std::expected, <print>, ranges::to, views::enumerate e views::chunk_by"
else
  grep -h '#error' "$LOG/toolchain.txt" >"$LOG/toolchain-short.txt" || cp "$LOG/toolchain.txt" "$LOG/toolchain-short.txt"
  fail "toolchain: o compilador não suporta os recursos de C++23 exigidos (veja o README, seção Ambiente)" "$LOG/toolchain-short.txt"
  finish
fi

# 2. O projeto compila com -std=c++23 -pedantic-errors.
rm -rf build/loganalyzer build/tests build/asan
if make -s build >"$LOG/build.txt" 2>&1; then
  pass "compilação: src/ compila com -std=c++23 -pedantic-errors"
else
  fail "compilação: src/ não compila" "$LOG/build.txt" 30
  finish
fi

# 3. Contrato de tipos (static_assert).
if make -s contract >"$LOG/contract.txt" 2>&1; then
  pass "contrato de tipos: parse_line -> std::expected noexcept, ErrorKind enum class, Report com erros por linha"
else
  grep -h -E 'static assertion failed|error:' "$LOG/contract.txt" | sed -E 's/^.*error: //' | sort -u >"$LOG/contract-short.txt"
  fail "contrato de tipos: a API não cumpre o contrato (tests/contract_test.cpp)" "$LOG/contract-short.txt" 12
fi

# 4. Testes comportamentais.
declare -A GROUP_TITLE=(
  [parse]="parse_line: cada linha vira Record ou ParseError com o campo culpado"
  [analyze]="analyze: contagens, linhas 1-based, CRLF, percentis e top paths"
  [report]="relatório: formato exato de format_report"
  [exceptions]="sem exceções para controle de fluxo: linhas inválidas não criam exceções"
)
TEST_GROUPS=(parse analyze report exceptions)

if make -s build/tests >"$LOG/build-tests.txt" 2>&1; then
  for g in "${TEST_GROUPS[@]}"; do
    rc=0
    timeout 60 build/tests "$g" >"$LOG/$g.txt" 2>&1 || rc=$?
    if ((rc == 0)); then
      pass "${GROUP_TITLE[$g]}"
    else
      grep -E '^ {4,}|^grupo|^uso' "$LOG/$g.txt" | grep -v '^    FALHOU' >"$LOG/$g-short.txt"
      ((rc == 124)) && echo "      tempo esgotado (60 s) — laço infinito?" >>"$LOG/$g-short.txt"
      ((rc > 128 && rc != 124)) && echo "      o executável de testes terminou com o sinal $((rc - 128))" >>"$LOG/$g-short.txt"
      fail "${GROUP_TITLE[$g]}" "$LOG/$g-short.txt" 30
    fi
  done
else
  for g in "${TEST_GROUPS[@]}"; do
    fail "${GROUP_TITLE[$g]} (os testes não compilaram contra src/)" "$LOG/build-tests.txt" 8
  done
fi

# 5. Ferramenta de linha de comando: saída e códigos de saída.
cli_ok=1
: >"$LOG/cli.txt"
if [[ -x build/loganalyzer ]]; then
  rc=0
  timeout 10 build/loganalyzer tests/data/access.log >"$LOG/cli-out.txt" 2>"$LOG/cli-err.txt" || rc=$?
  if ! diff -a -u tests/data/access.expected "$LOG/cli-out.txt" >"$LOG/cli-diff.txt"; then
    cli_ok=0
    { echo "saída de 'loganalyzer tests/data/access.log' difere do esperado (- esperado, + obtido):"; cat "$LOG/cli-diff.txt"; } >>"$LOG/cli.txt"
  fi
  if ((rc != 1)); then
    cli_ok=0
    echo "log com linhas inválidas deveria terminar com código 1; terminou com $rc" >>"$LOG/cli.txt"
  fi
  sed 's/$/\r/' tests/data/access.log >"$LOG/access-crlf.log"
  rc=0
  timeout 10 build/loganalyzer <"$LOG/access-crlf.log" >"$LOG/cli-crlf.txt" 2>/dev/null || rc=$?
  if ! cmp -s tests/data/access.expected "$LOG/cli-crlf.txt"; then
    cli_ok=0
    echo "o mesmo log com CRLF, lido da entrada padrão, produziu outro relatório" >>"$LOG/cli.txt"
  fi
  rc=0
  timeout 10 build/loganalyzer tests/data/clean.log >/dev/null 2>&1 || rc=$?
  if ((rc != 0)); then
    cli_ok=0
    echo "log sem linhas inválidas deveria terminar com código 0; terminou com $rc" >>"$LOG/cli.txt"
  fi
  rc=0
  timeout 10 build/loganalyzer tests/data/nao-existe.log >"$LOG/cli-missing-out.txt" 2>"$LOG/cli-missing-err.txt" || rc=$?
  if ((rc != 2)) || [[ -s "$LOG/cli-missing-out.txt" || ! -s "$LOG/cli-missing-err.txt" ]]; then
    cli_ok=0
    echo "arquivo inexistente: esperado código 2, mensagem no stderr e nada no stdout (código obtido: $rc)" >>"$LOG/cli.txt"
  fi
else
  cli_ok=0
  echo "build/loganalyzer não foi gerado" >>"$LOG/cli.txt"
fi
if ((cli_ok)); then
  pass "loganalyzer: relatório exato, CRLF via stdin e códigos de saída 0/1/2"
else
  fail "loganalyzer: relatório ou códigos de saída incorretos" "$LOG/cli.txt" 30
fi

# 6. AddressSanitizer + UndefinedBehaviorSanitizer.
san_ok=1
: >"$LOG/san.txt"
if make -s build/asan/tests build/asan/loganalyzer >"$LOG/build-asan.txt" 2>&1; then
  export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
  for g in parse analyze report; do
    rc=0
    timeout 120 build/asan/tests "$g" >"$LOG/asan-$g.txt" 2>&1 || rc=$?
    if grep -q -E 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$LOG/asan-$g.txt"; then
      san_ok=0
      { echo "grupo $g:"; san_excerpt "$LOG/asan-$g.txt"; } >>"$LOG/san.txt"
    elif ((rc > 1)); then
      san_ok=0
      echo "grupo $g: o executável instrumentado terminou com código $rc" >>"$LOG/san.txt"
    fi
  done
  rc=0
  timeout 30 build/asan/loganalyzer tests/data/access.log >/dev/null 2>"$LOG/asan-cli.txt" || rc=$?
  if grep -q -E 'ERROR: |runtime error:' "$LOG/asan-cli.txt"; then
    san_ok=0
    { echo "loganalyzer:"; san_excerpt "$LOG/asan-cli.txt"; } >>"$LOG/san.txt"
  fi
else
  san_ok=0
  cp "$LOG/build-asan.txt" "$LOG/san.txt"
fi
if ((san_ok)); then
  pass "ASan/UBSan: nenhum acesso inválido, vazamento ou comportamento indefinido"
else
  fail "ASan/UBSan: o código instrumentado acusou problemas de memória ou UB" "$LOG/san.txt" 40
fi

finish
