#!/usr/bin/env bash
# Executa cada teste em um processo separado, duas vezes:
#   1. ASan + UBSan (+ LeakSanitizer): uso após liberação, vazamentos, UB;
#   2. ThreadSanitizer: data races, uso de objetos destruídos entre threads.
# Cada execução tem prazo próprio (watchdog interno + timeout externo).
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

LOGS=build/logs
mkdir -p "$LOGS"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:color=never"
export LSAN_OPTIONS="report_objects=0"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:color=never"
export TSAN_OPTIONS="halt_on_error=1:exitcode=66:second_deadlock_stack=1:color=never"

ok()  { printf '  \342\234\224 %s\n' "$1"; }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; }

# Primeiro quadro de pilha em src/ (ou, na falta, em tests/): "Função arquivo:linha".
first_frame() {
  local text frame
  text=$(cat)
  frame=$(grep -m1 -E '#[0-9]+ .*[ /]src/[^ ]+:[0-9]+' <<<"$text")
  [[ -z "$frame" ]] && frame=$(grep -m1 -E '#[0-9]+ .*[ /]tests/[^ ]+:[0-9]+' <<<"$text")
  [[ -z "$frame" ]] && { echo "?"; return; }
  sed -E 's/^ *#[0-9]+ (0x[0-9a-f]+ in )?//; s/ \([^ ]*\+0x[0-9a-f]+\)$//; s#[^ ]*/((src|tests)/[^ ]+)$#\1#' <<<"$frame"
}

explain() {  # explain <log> <exit-code>
  local log=$1 rc=$2 line kind
  local -a reasons=()
  line=$(grep -m1 '^FALHA: ' "$log" | sed 's/^FALHA: //; s#^tests/\.\./src/#src/#')
  [[ -n "$line" ]] && reasons+=("$line")
  if grep -q 'FATAL: ThreadSanitizer: unexpected memory mapping' "$log"; then
    reasons+=("o ThreadSanitizer não conseguiu iniciar (ASLR do kernel); veja 'Ambiente' no README")
  elif grep -q 'WARNING: ThreadSanitizer:' "$log"; then
    kind=$(grep -m1 'WARNING: ThreadSanitizer:' "$log" | sed -E 's/.*WARNING: ThreadSanitizer: (.*) \(pid=.*/\1/')
    local report
    report=$(sed -n '/WARNING: ThreadSanitizer/,/^SUMMARY/p' "$log")
    if grep -q '^ *Previous ' <<<"$report"; then
      reasons+=("ThreadSanitizer: $kind em $(sed '/^ *Previous /,$d' <<<"$report" | first_frame)")
      reasons+=("conflita com o acesso anterior em $(sed -n '/^ *Previous /,$p' <<<"$report" | first_frame)")
    else
      reasons+=("ThreadSanitizer: $kind em $(first_frame <<<"$report")")
    fi
  fi
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
  if grep -q 'terminate called' "$log"; then
    reasons+=("std::terminate foi chamado: $(grep -m1 'terminate called' "$log")")
  fi
  if ((rc == 124 || rc == 137)) && ! grep -q '^FALHA: ' "$log"; then
    reasons+=("tempo esgotado: o processo não terminou (thread travada?)")
  fi
  if ((${#reasons[@]} == 0)); then
    reasons+=("o processo terminou com código $rc sem imprimir OK")
  fi
  local first=1 r
  for r in "${reasons[@]}"; do
    if ((first)); then printf '%s' "$r"; first=0; else printf '\n              %s' "$r"; fi
  done
}

compile() {  # compile <variante> <alvo>
  if ! make --no-print-directory "$2" >"build/compile-$1.log" 2>&1; then
    bad "o projeto compila em C++20 ($1)" "erros de compilação (log completo: build/compile-$1.log)"
    grep -E "error:|^(tests|src)/[^ ]*: +required from" "build/compile-$1.log" | head -12 | cut -c1-200 |
      sed 's#^tests/\.\./src/#src/#; s/^/        /'
    if grep -qE '^tests/[^ ]*: +required from here|^tests/[^ ]*:[0-9]+:[0-9]+: error' "build/compile-$1.log"; then
      echo "      dica: o erro surge ao compilar tests/: o código em src/ não oferece algo que o teste"
      echo "            usa (veja a linha indicada em tests/ e o contrato nos comentários dos headers)."
    fi
    return 1
  fi
}

pass=0
fail=0
run_suite() {  # run_suite <variante> <binário> <prazo-ms> [prefixo...]
  local variant=$1 bin=$2 deadline=$3
  shift 3
  local name desc log rc list
  if ! list=$("$@" "$bin" --list 2>"$LOGS/$variant-list.log") || [[ -z "$list" ]]; then
    bad "[$variant] o binário de testes inicia" "$(explain "$LOGS/$variant-list.log" 1)"
    fail=$((fail + 1))
    return
  fi
  while IFS=$'\t' read -r name desc; do
    [[ -z "$name" ]] && continue
    log="$LOGS/$variant-$name.log"
    rc=0
    GQ_TEST_DEADLINE_MS=$deadline timeout --kill-after=5 40 "$@" "$bin" "$name" >"$log" 2>&1 || rc=$?
    if ((rc == 0)) && grep -qx 'OK' "$log"; then
      ok "[$variant] $desc"
      pass=$((pass + 1))
    else
      bad "[$variant] $desc [$name]" "$(explain "$log" "$rc")"
      fail=$((fail + 1))
      if [[ "${GQ_VERBOSE:-0}" == 1 ]]; then sed 's/^/        | /' "$log" | head -80; fi
    fi
  done <<<"$list"
}

printf '==> Compilando src/ e tests/ (g++ %s, -std=c++20 -pedantic-errors)\n' \
  "$(g++ -dumpfullversion 2>/dev/null || echo '?')"
compile asan build/asan/test-runner || exit 1
compile tsan build/tsan/test-runner || exit 1
ok "o projeto compila em C++20 com -pedantic-errors (ASan+UBSan e TSan)"

echo "==> [asan] AddressSanitizer + UBSan + LeakSanitizer (um processo por teste; logs em $LOGS/)"
run_suite asan build/asan/test-runner 8000

# O TSan precisa de um espaço de endereços sem randomização agressiva (kernels com
# vm.mmap_rnd_bits alto, como o do WSL2 e o do Ubuntu 24.04, quebram o TSan do gcc 14).
# `setarch -R` desliga a randomização só para o processo do teste.
tsan_prefix=()
if setarch "$(uname -m)" -R true 2>/dev/null; then
  tsan_prefix=(setarch "$(uname -m)" -R)
else
  echo "aviso: setarch -R indisponível; o ThreadSanitizer pode falhar ao iniciar neste kernel" >&2
fi
echo "==> [tsan] ThreadSanitizer (um processo por teste)"
run_suite tsan build/tsan/test-runner 15000 "${tsan_prefix[@]}"

total=$((pass + fail))
echo
if ((total == 0)); then
  echo "nenhum teste foi executado"
  exit 1
fi
if ((fail > 0)); then
  printf '%d de %d verificações passaram. Detalhes: build/logs/<variante>-<teste>.log (ou GQ_VERBOSE=1 ./test.sh)\n' \
    "$pass" "$total"
  exit 1
fi
printf 'Todas as %d verificações passaram: encerramento rápido, nada perdido ou duplicado, nenhuma data race.\n' "$total"
