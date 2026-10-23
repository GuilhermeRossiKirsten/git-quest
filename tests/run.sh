#!/usr/bin/env bash
# Avaliador do desafio-28. Roda dentro do container (via `make test`) ou
# localmente com GITQUEST_LOCAL=1. Cada critério é reportado com ✔/✘.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

CXX=${CXX:-g++}
COMPILE=("$CXX" -std=c++20 -pedantic-errors -Isrc -Itests)
LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export LSAN_OPTIONS="report_objects=0"
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
# Primeiras linhas de erro do compilador, sem o ruído de notas.
first_errors() { grep -E 'error|static assertion' | head -n "${1:-4}" | sed -E 's#^.*/(src|tests)/#\1/#' | cut -c1-220; }

# Primeiro quadro da pilha em src/ ou tests/ (ignorando o contador de alocações).
first_frame() {
  grep -E '^ *#[0-9]+ .* in .*(src|tests)/[^ ]+:[0-9]+' | grep -v 'alloc_counter\.cpp' | head -1 |
    sed -E 's/^ *#[0-9]+ 0x[0-9a-f]+ in //; s#[^ ]*/((src|tests)/)#\1#'
}

# Explica por que um teste falhou, a partir do log do processo.
explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 line
  local -a reasons=()
  while IFS= read -r line; do reasons+=("${line#FALHA: }"); done < <(grep '^FALHA: ' "$log" | head -n 4)
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#' | cut -c1-200)")
  fi
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line em $(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)")
  fi
  if grep -q 'ERROR: LeakSanitizer' "$log"; then
    line=$(grep -m1 'SUMMARY: AddressSanitizer: .* leaked' "$log" |
      sed -E 's/.*: ([0-9]+) byte\(s\) leaked in ([0-9]+) allocation\(s\).*/\1 byte(s) em \2 alocação(ões)/')
    reasons+=("LeakSanitizer: vazamento de $line; alocado em $(sed -n '/ERROR: LeakSanitizer/,$p' "$log" | first_frame)")
  fi
  ((rc == 124 || rc == 137)) && reasons+=("tempo esgotado (10 s)")
  ((${#reasons[@]} == 0)) && reasons+=("o processo terminou com código $rc")
  printf '%s\n' "${reasons[@]}"
}

# run_group <binário> <prefixo>: executa, um processo por teste, os testes cujo id começa com o prefixo.
run_group() {
  local bin=$1 prefix=$2 id title rc
  while IFS=$'\t' read -r id title; do
    [[ -z "$id" || "$id" != "$prefix"* ]] && continue
    rc=0
    timeout 10 "$bin" "$id" >"$LOGS/$id.log" 2>&1 || rc=$?
    if ((rc == 0)); then
      ok "$title"
    else
      ko "$title  [$id]" "$(explain "$LOGS/$id.log" "$rc")"
    fi
  done < <("$bin" --list | tr -d '\r')
}

# ---------------------------------------------------------------------------
if [[ -n "${SAN+set}" && -z "$SAN" ]]; then san_label="sem sanitizers"; else san_label="ASan + UBSan + LSan"; fi
section "0. Compilação (g++ -std=c++20 -pedantic-errors -Wall -Wextra, $san_label)"
core_ok=0
if make --no-print-directory -s build/caixa >"$LOGS/compile_core.log" 2>&1; then
  if timeout 10 build/caixa >"$LOGS/caixa.log" 2>&1; then
    ok "o núcleo (src/promo/) e a demonstração src/main.cpp compilam e executam"
  else
    ko "build/caixa compila, mas terminou com erro" "$(explain "$LOGS/caixa.log" 1)"
  fi
else
  ko "o núcleo (src/promo/) ou src/main.cpp não compila" "$(first_errors 6 <"$LOGS/compile_core.log")"
fi
if make --no-print-directory -s build/core_tests >"$LOGS/compile_core_tests.log" 2>&1; then
  ok "tests/core_tests.cpp compila com o núcleo (os usos das promoções embutidas continuam válidos)"
  core_ok=1
else
  ko "tests/core_tests.cpp não compila com o núcleo" "$(first_errors 6 <"$LOGS/compile_core_tests.log")"
fi

# ---------------------------------------------------------------------------
section "1. Saídas de referência das promoções existentes"
if ((core_ok)); then run_group build/core_tests ref_; else ko "não executados" "corrija a compilação de tests/core_tests.cpp primeiro"; fi

# ---------------------------------------------------------------------------
section "2. Tipos novos sem editar o núcleo (plugins em tests/plugin_*.cpp)"
plugin_ok=1
for file in tests/plugin_*.cpp; do
  obj="build/${file%.cpp}.o"
  name=$(basename "$file" .cpp)
  if make --no-print-directory -s "$obj" >"$LOGS/compile_$name.log" 2>&1; then
    ok "$file compila usando só a API pública (promo/promotion.hpp, promo/checkout.hpp)"
  else
    plugin_ok=0
    ko "$file não compila contra a API pública" "$(first_errors 5 <"$LOGS/compile_$name.log")"
  fi
done
if ((plugin_ok)); then
  if make --no-print-directory -s build/plugin_tests >"$LOGS/link_plugins.log" 2>&1; then
    run_group build/plugin_tests plugin_
  else
    plugin_ok=0
    ko "os plugins não ligam com o núcleo" "$(first_errors 5 <"$LOGS/link_plugins.log")"
  fi
else
  ko "testes dos plugins não executados" "o núcleo precisa aceitar tipos novos sem que ninguém edite src/"
fi

# ---------------------------------------------------------------------------
section "3. Semântica de valor: cópias independentes, sem fatiamento"
if ((core_ok)); then run_group build/core_tests valor_; fi
if ((plugin_ok)); then run_group build/plugin_tests valor_; else ko "semântica de valor com tipos de plugin não verificada" "depende do critério 2"; fi

# ---------------------------------------------------------------------------
section "4. Memória: alocações razoáveis (e LeakSanitizer em todos os testes acima)"
if ((core_ok)); then run_group build/core_tests mem_; fi
if ((plugin_ok)); then run_group build/plugin_tests mem_; else ko "alocações com tipos de plugin não verificadas" "depende do critério 2"; fi

# ---------------------------------------------------------------------------
section "5. Contrato em tempo de compilação (cada caso compilado isoladamente)"
for file in tests/contracts/*.cpp; do
  title=$(head -n 1 "$file" | sed 's#^// CONTRATO: ##')
  if out=$("${COMPILE[@]}" -fsyntax-only "$file" 2>&1); then
    ok "$title"
  else
    msgs=$(printf '%s\n' "$out" | grep -o 'static assertion failed: .*' | sed 's/^static assertion failed: /violado: /' | head -n 4)
    [[ -z "$msgs" ]] && msgs=$(printf '%s\n' "$out" | first_errors 4)
    ko "$title  [$(basename "$file")]" "$msgs"
  fi
done
for file in tests/diagnostics/*.cpp; do
  title=$(head -n 1 "$file" | sed 's#^// DIAGNÓSTICO: ##')
  if out=$("${COMPILE[@]}" -fsyntax-only "$file" 2>&1); then
    ko "$title" "o arquivo compilou, mas uma regra sem label() deveria ser rejeitada"
  elif printf '%s\n' "$out" | grep -q 'PromotionRule'; then
    ok "$title"
  else
    ko "$title" "o erro não menciona o concept PromotionRule; primeiras linhas:
$(printf '%s\n' "$out" | first_errors 3)"
  fi
done

# ---------------------------------------------------------------------------
total=$((passed + failed))
printf '\nResultado: %d/%d critérios satisfeitos.\n' "$passed" "$total"
if ((failed > 0)); then
  printf 'Ainda faltam %d. Logs em build/logs/. Dica: ./scripts/gitquest hints 1\n' "$failed"
  exit 1
fi
printf 'O switch parou de crescer: tipos novos entram sem tocar no núcleo. Desafio concluído!\n'
