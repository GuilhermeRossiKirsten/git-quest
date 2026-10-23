#!/usr/bin/env bash
# Avaliador do desafio-29. Roda dentro do container (via `make test`) ou
# localmente com GITQUEST_LOCAL=1. Cada critério é reportado com ✔/✘.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

CXX=${CXX:-g++}
COMPILE=(timeout 60 "$CXX" -std=c++20 -pedantic-errors -fsyntax-only -Isrc -Itests)
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

# Executa cada teste de um binário minitest em um processo separado.
run_suite() {
  local binary=$1 id title rc out
  while IFS=$'\t' read -r id title; do
    [[ -z "$id" ]] && continue
    rc=0
    out=$(timeout 10 "$binary" "$id" 2>&1) || rc=$?
    if ((rc == 0)); then
      ok "$title"
    elif ((rc == 124)); then
      ko "$title" "tempo esgotado (10 s)"
    elif ((rc > 128)); then
      ko "$title" "processo abortado pelo sinal $((rc - 128))
$(printf '%s\n' "$out" | grep -E 'runtime error|SUMMARY|linha' | head -n 3)"
    else
      ko "$title" "$(printf '%s\n' "$out" | head -n 6)"
    fi
  done < <("$binary" --list | tr -d '\r')
}

# ---------------------------------------------------------------------------
if [[ -n "${SAN+set}" && -z "$SAN" ]]; then san_label="sem sanitizers"; else san_label="ASan + UBSan"; fi
section "1. Compilação (g++ -std=c++20 -pedantic-errors, $san_label)"
declare -A built=()
for target in unit_tests mission_tests; do
  if out=$(make --no-print-directory -s "$BUILD/$target" 2>&1); then
    ok "tests/$target.cpp compila com a biblioteca"
    built[$target]=1
  else
    ko "tests/$target.cpp não compila (usos VÁLIDOS da API precisam continuar funcionando)" \
       "$(printf '%s\n' "$out" | first_errors 6)"
  fi
done
if out=$(make --no-print-directory -s "$BUILD/telemetry" 2>&1); then
  if run=$(timeout 10 "$BUILD/telemetry" 2>&1); then
    ok "src/telemetry.cpp compila e executa"
  else
    ko "src/telemetry.cpp compila, mas terminou com erro" "$(printf '%s\n' "$run" | tail -n 6)"
  fi
else
  ko "src/telemetry.cpp não compila — a biblioteca mais rígida apontou usos sem unidade nele?" \
     "$(printf '%s\n' "$out" | first_errors 6)"
fi

# ---------------------------------------------------------------------------
section "2. Valores em tempo de execução (um processo por teste)"
for target in unit_tests mission_tests; do
  if [[ -n "${built[$target]:-}" ]]; then
    run_suite "$BUILD/$target"
  else
    ko "testes de tests/$target.cpp não executados" "corrija a compilação de tests/$target.cpp primeiro"
  fi
done

# ---------------------------------------------------------------------------
section "3. Contratos em tempo de compilação (static_assert; cada arquivo compilado isoladamente)"
for file in tests/contracts/*.cpp; do
  title=$(head -n 1 "$file" | sed 's#^// CONTRATO: ##')
  if out=$("${COMPILE[@]}" "$file" 2>&1); then
    ok "$title"
  else
    msgs=$(printf '%s\n' "$out" | grep -o 'static assertion failed: .*' | sed 's/^static assertion failed: /violado: /' | sort -u | head -n 4)
    [[ -z "$msgs" ]] && msgs=$(printf '%s\n' "$out" | first_errors 4)
    ko "$title  [$(basename "$file")]" "$msgs"
  fi
done

# ---------------------------------------------------------------------------
section "4. Usos sem sentido físico NÃO compilam (forma válida com -DGQ_VALID precisa compilar)"
for file in tests/rejects/*.cpp; do
  title="recusa: $(head -n 1 "$file" | sed 's#^// REJEITA: ##')"
  line=$(grep -n '← não deve compilar' "$file" | head -n 1 | cut -d: -f1)
  if ! out=$("${COMPILE[@]}" -DGQ_VALID "$file" 2>&1); then
    ko "$title  [$(basename "$file")]" "a forma VÁLIDA (-DGQ_VALID) também não compila — a API correta precisa continuar funcionando:
$(printf '%s\n' "$out" | first_errors 3)"
  elif out=$("${COMPILE[@]}" "$file" 2>&1); then
    ko "$title  [$(basename "$file")]" "compilou, mas deveria ser recusado (linha $line)"
  elif ! printf '%s\n' "$out" | grep -q "$(basename "$file"):$line:"; then
    ko "$title  [$(basename "$file")]" "foi recusado, mas o erro não aponta a linha $line — o motivo parece outro:
$(printf '%s\n' "$out" | first_errors 3)"
  else
    ok "$title"
  fi
done

# ---------------------------------------------------------------------------
total=$((passed + failed))
printf '\nResultado: %d/%d critérios satisfeitos.\n' "$passed" "$total"
if ((failed > 0)); then
  printf 'Ainda faltam %d. Dica: ./scripts/gitquest hints 1\n' "$failed"
  exit 1
fi
printf 'Metros não somam mais com segundos. Desafio concluído!\n'
