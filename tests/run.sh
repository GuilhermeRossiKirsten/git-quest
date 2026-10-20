#!/usr/bin/env bash
# Executa todos os critérios do desafio e imprime ✔/✘ com o motivo.
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
# Cada teste roda num processo separado com timeout próprio: um laço infinito
# reprova só aquele teste e não trava o avaliador.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

TEST_TIMEOUT=${GQ_TEST_TIMEOUT:-20}
PERF_TIMEOUT=${GQ_PERF_TIMEOUT:-20}
export ASAN_OPTIONS=detect_leaks=1
export UBSAN_OPTIONS=print_stacktrace=1

passed=0
failed=0
ok()  { printf '  ✔ %s\n' "$1"; passed=$((passed + 1)); }
bad() { printf '  ✘ %s — %s\n' "$1" "$2"; failed=$((failed + 1)); }
indent() { sed -n '1,25p' | sed 's/^/      /'; }

# Motivo de uma falha a partir do código de saída e da saída do teste.
reason() {
  local rc=$1 out=$2 timeout_s=$3 line
  if ((rc == 124 || rc == 137)); then
    echo "tempo esgotado (${timeout_s}s): laço infinito ou operação lenta demais"
    return
  fi
  line=$(grep -m1 -E '^FALHA' <<<"$out" | sed 's/^FALHA //')
  if [[ -n "$line" ]]; then echo "$line"; return; fi
  line=$(grep -m1 -E 'ERROR: (Address|Leak)Sanitizer|runtime error' <<<"$out")
  if [[ -n "$line" ]]; then echo "sanitizer: ${line#*: }"; return; fi
  echo "o processo terminou com código $rc"
}

echo "Compilação"
if build_log=$(make --no-print-directory build 2>&1); then
  ok "src/flat_map.hpp compila (C++17, g++ $(g++ -dumpfullversion))"
else
  bad "compilação" "erros abaixo"
  printf '%s\n' "$build_log" | grep -E 'error|erro' | indent
  echo
  echo "Resultado: o código não compila."
  exit 1
fi

echo
echo "Correção (ASan + UBSan, timeout de ${TEST_TIMEOUT}s por teste)"
mapfile -t tests < <(build/correctness --list)
for name in "${tests[@]}"; do
  rc=0
  out=$(timeout --kill-after=5 "$TEST_TIMEOUT" build/correctness "$name" 2>&1) || rc=$?
  if ((rc == 0)); then
    ok "$name"
  else
    bad "$name" "$(reason "$rc" "$out" "$TEST_TIMEOUT")"
    if ! grep -q '^FALHA' <<<"$out" && ((rc != 124 && rc != 137)); then
      printf '%s\n' "$out" | indent
    fi
  fi
done

echo
echo "Desempenho (-O2, sem sanitizers, timeout de ${PERF_TIMEOUT}s)"
rc=0
out=$(timeout --kill-after=5 "$PERF_TIMEOUT" build/perf 2>&1) || rc=$?
if ((rc == 0)); then
  ok "1M chaves (id << 32): inserir, buscar, buscar ausentes, apagar metade — ${out#OK }"
else
  bad "1M chaves (id << 32): inserir, buscar, buscar ausentes, apagar metade" "$(reason "$rc" "$out" "$PERF_TIMEOUT")"
fi

echo
echo "Regras"
if grep -rEn '#[[:space:]]*include[[:space:]]*<(unordered_map|unordered_set|map|set)>' src >/dev/null 2>&1; then
  bad "implementação própria" "src/ inclui um contêiner associativo da STL; a tabela deve ser implementada com endereçamento aberto"
else
  ok "implementação própria (sem std::unordered_map/std::map em src/)"
fi

total=$((passed + failed))
echo
if ((failed == 0)); then
  echo "Resultado: $passed de $total critérios atendidos. Desafio concluído!"
  exit 0
fi
echo "Resultado: $passed de $total critérios atendidos; $failed falharam."
exit 1
