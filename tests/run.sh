#!/usr/bin/env bash
# Compila cada tests/test_*.cpp num binário próprio (um arquivo que não compila não
# derruba os outros) e executa cada teste em um processo separado, com
# AddressSanitizer e UndefinedBehaviorSanitizer ligados. Explica cada falha.
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
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
  line=$(grep -m1 -A3 '^FALHA: ' "$log" | sed 's/^FALHA: //')
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'ERROR: AddressSanitizer' "$log"; then
    line=$(grep -m1 'ERROR: AddressSanitizer' "$log" | sed -E 's/.*ERROR: AddressSanitizer: (attempting )?([^ ]+).*/\2/')
    reasons+=("AddressSanitizer: $line em $(sed -n '/ERROR: AddressSanitizer/,$p' "$log" | first_frame)")
  fi
  if grep -q 'runtime error:' "$log"; then
    reasons+=("UBSan: $(grep -m1 'runtime error:' "$log" | sed -E 's#^[^ ]*/((src|tests)/)#\1#')")
  fi
  if grep -q 'ERROR: LeakSanitizer' "$log"; then
    line=$(grep -m1 'SUMMARY: AddressSanitizer: .* leaked' "$log" |
      sed -E 's/.*: ([0-9]+) byte\(s\) leaked in ([0-9]+) allocation\(s\).*/\1 byte(s) em \2 alocação(ões)/')
    reasons+=("LeakSanitizer: vazamento de $line; alocado em $(sed -n '/ERROR: LeakSanitizer/,$p' "$log" | first_frame)")
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

compile_hints() {  # compile_hints <log>
  local log=$1
  if grep -qE "Reading::Reading\(\)" "$log"; then
    echo "      dica: a fila tenta construir T() (construtor padrão), que Reading não tem."
    echo "            Um elemento só deveria ser construído quando entra na fila."
  fi
  if grep -qE '^tests/[^ ]*: +required from here' "$log"; then
    echo "      dica: o erro surge ao compilar tests/: siga as linhas 'required from' até a"
    echo "            linha de src/ring_buffer.h que a instanciação do template não consegue compilar."
  fi
}

printf '==> Compilando e testando (g++ %s, -std=c++17 -pedantic-errors, ASan + UBSan)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"

pass=0
fail=0
for src in tests/test_*.cpp; do
  base=$(basename "$src" .cpp)
  bin=build/bin/$base
  echo "--- $src"
  if ! make --no-print-directory "$bin" >"$LOGS/$base.compile.log" 2>&1; then
    bad "$src compila com o seu src/ring_buffer.h" "erros de compilação (log completo: $LOGS/$base.compile.log)"
    grep -E "error:|^(tests|src)/[^ ]*: +required from" "$LOGS/$base.compile.log" | head -8 | cut -c1-200 |
      sed 's/^/        /'
    compile_hints "$LOGS/$base.compile.log"
    # Cada teste do arquivo conta como reprovado.
    n=$(grep -cE '^TEST\(' "$src")
    fail=$((fail + n))
    continue
  fi
  while IFS=$'\t' read -r name desc; do
    [[ -z "$name" ]] && continue
    log="$LOGS/$name.log"
    rc=0
    timeout --kill-after=5 60 "$bin" "$name" >"$log" 2>&1 || rc=$?
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

total=$((pass + fail))
echo
if ((total == 0)); then
  echo "nenhum teste foi executado"
  exit 1
fi
if ((fail > 0)); then
  printf '%d de %d testes passaram. Detalhes: build/logs/<teste>.log (ou GQ_VERBOSE=1 ./test.sh)\n' "$pass" "$total"
  exit 1
fi
printf 'Todos os %d testes passaram: a fila circular está correta, sem heap e sem objetos esquecidos.\n' "$total"
