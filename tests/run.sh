#!/usr/bin/env bash
# Avaliador do desafio-4. Roda dentro do container (via `make test`) ou
# localmente com GITQUEST_LOCAL=1. Cada critério é reportado com ✔/✘.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

CXX=${CXX:-g++}
COMPILE=("$CXX" -std=c++20 -pedantic-errors -Isrc -Itests)
BUILD=build
mkdir -p "$BUILD"

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

# ---------------------------------------------------------------------------
if [[ -n "${SAN+set}" && -z "$SAN" ]]; then san_label="sem sanitizers"; else san_label="ASan + UBSan"; fi
section "1. Compilação (g++ -std=c++20 -pedantic-errors, $san_label)"
runtime_ok=0
if out=$(make --no-print-directory -s "$BUILD/runtime_tests" 2>&1); then
  ok "tests/runtime_tests.cpp compila com a biblioteca"
  runtime_ok=1
else
  ko "tests/runtime_tests.cpp não compila com a biblioteca (usos VÁLIDOS da API precisam continuar funcionando)" \
     "$(printf '%s\n' "$out" | first_errors 6)"
fi
if out=$(make --no-print-directory -s "$BUILD/report" 2>&1); then
  if run=$(timeout 10 "$BUILD/report" 2>&1); then
    ok "src/report.cpp compila e executa"
  else
    ko "src/report.cpp compila, mas terminou com erro" "$(printf '%s\n' "$run" | tail -n 6)"
  fi
else
  ko "src/report.cpp não compila — os contratos novos apontaram usos inválidos nele?" \
     "$(printf '%s\n' "$out" | first_errors 6)"
fi

# ---------------------------------------------------------------------------
section "2. Comportamento em tempo de execução (um processo por teste)"
if ((runtime_ok)); then
  while IFS=$'\t' read -r id title; do
    [[ -z "$id" ]] && continue
    rc=0
    out=$(timeout 10 "$BUILD/runtime_tests" "$id" 2>&1) || rc=$?
    if ((rc == 0)); then
      ok "$title"
    elif ((rc == 124)); then
      ko "$title" "tempo esgotado (10 s)"
    elif ((rc > 128)); then
      ko "$title" "processo abortado pelo sinal $((rc - 128)) — divisão por zero? comportamento indefinido?
$(printf '%s\n' "$out" | grep -E 'runtime error|SUMMARY|linha' | head -n 3)"
    else
      ko "$title" "$(printf '%s\n' "$out" | head -n 5)"
    fi
  done < <("$BUILD/runtime_tests" --list | tr -d '\r')
else
  ko "testes de comportamento não executados" "corrija a compilação de tests/runtime_tests.cpp primeiro"
fi

# ---------------------------------------------------------------------------
section "3. Contratos em tempo de compilação (cada caso compilado isoladamente)"
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

# ---------------------------------------------------------------------------
section "4. Qualidade do diagnóstico"
for file in tests/diagnostics/*.cpp; do
  title=$(head -n 1 "$file" | sed 's#^// DIAGNÓSTICO: ##')
  if out=$("${COMPILE[@]}" -fsyntax-only "$file" 2>&1); then
    ko "$title" "o arquivo compilou, mas deveria ser rejeitado"
  elif printf '%s\n' "$out" | grep -q 'Numeric'; then
    ok "$title"
  else
    ko "$title" "o erro não menciona nenhum concept Numeric*; primeiras linhas:
$(printf '%s\n' "$out" | first_errors 3)"
  fi
done

# ---------------------------------------------------------------------------
total=$((passed + failed))
printf '\nResultado: %d/%d critérios satisfeitos.\n' "$passed" "$total"
if ((failed > 0)); then
  printf 'Ainda faltam %d. Dica: ./scripts/gitquest hints 1\n' "$failed"
  exit 1
fi
printf 'Todos os contratos estão de pé. Desafio concluído!\n'
