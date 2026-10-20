#!/usr/bin/env bash
# Avaliador do desafio-2. Roda dentro do container de toolchain (ou localmente
# com GITQUEST_LOCAL=1). Cada critério é um executável separado, compilado em
# C++14 estrito com AddressSanitizer e UndefinedBehaviorSanitizer: um crash ou
# um erro de compilação reprova só o próprio critério.
set -uo pipefail
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
STD_FLAGS=(-std=c++14 -pedantic-errors)
SAN_FLAGS=(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
export ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1:color=never
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:color=never
OUT=build/tests
mkdir -p "$OUT"

if [[ -z "${NO_COLOR:-}" && -t 1 ]]; then
  OK=$'\033[32m✔\033[0m' KO=$'\033[31m✘\033[0m'
else
  OK='✔' KO='✘'
fi

passed=0
failed=0

# report <ok|fail> <descrição> [motivo...]
report() {
  local status=$1 desc=$2
  shift 2
  if [[ $status == ok ]]; then
    printf '  %s %s\n' "$OK" "$desc"
    passed=$((passed + 1))
  else
    printf '  %s %s\n' "$KO" "$desc"
    local line
    for line in "$@"; do printf '      %s\n' "$line"; done
    failed=$((failed + 1))
  fi
}

# Primeiras linhas relevantes de um log (erros de compilação ou falhas/sanitizers).
summarize() {
  grep -E "$2" "$1" | head -n "${3:-6}" | cut -c1-220
}

# compile <saída> <fonte> [flags extras...] — imprime o resumo do erro, se houver
compile() {
  local exe=$1 src=$2
  shift 2
  local log="$exe.compile.log"
  if "$CXX" "${STD_FLAGS[@]}" -Wall -Wextra "${SAN_FLAGS[@]}" -Isrc -Itests "$@" "$src" -o "$exe" >"$log" 2>&1; then
    return 0
  fi
  summarize "$log" 'error|required from here' 6
  return 1
}

# run_exe <executável> — imprime o resumo da falha, se houver
run_exe() {
  local exe=$1 log="$1.run.log" rc=0
  timeout --kill-after=5 30 "$exe" >"$log" 2>&1 || rc=$?
  ((rc == 0)) && return 0
  if ((rc == 124 || rc == 137)); then
    echo "tempo esgotado (30 s): há um laço infinito ou um bloqueio?"
  fi
  summarize "$log" '^FALHA|^SUMMARY|ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error' 10
  echo "(log completo: $log)"
  return 1
}

# criterion <id> <descrição>
criterion() {
  local id=$1 desc=$2 exe="$OUT/test_$1" msg
  if ! msg=$(compile "$exe" "tests/test_$id.cpp"); then
    mapfile -t lines <<<"$msg"
    report fail "$desc" "não compila:" "${lines[@]}"
    return
  fi
  if msg=$(run_exe "$exe"); then
    report ok "$desc"
  else
    mapfile -t lines <<<"$msg"
    report fail "$desc" "${lines[@]}"
  fi
}

printf '\nGitQuest · desafio-2 — critérios (%s, %s)\n\n' "$("$CXX" -dumpfullversion)" "${STD_FLAGS[*]}"

# 1. O pipeline de demonstração compila sem avisos e roda limpo nos sanitizers.
if msg=$(compile "$OUT/pipeline" src/main.cpp -Werror); then
  if msg=$(run_exe "$OUT/pipeline"); then
    report ok "pipeline (src/main.cpp) compila em C++14 estrito sem avisos e roda limpo no ASan/UBSan"
  else
    mapfile -t lines <<<"$msg"
    report fail "pipeline (src/main.cpp) compila em C++14 estrito sem avisos e roda limpo no ASan/UBSan" "${lines[@]}"
  fi
else
  mapfile -t lines <<<"$msg"
  report fail "pipeline (src/main.cpp) compila em C++14 estrito sem avisos e roda limpo no ASan/UBSan" "não compila:" "${lines[@]}"
fi

criterion compose   "compose: ordem f(g(x)), variádico, tipos mistos, callables move-only, sem cópias extras"
criterion by        "by: comparador genérico por projeção, sem copiar os elementos"
criterion apply_n   "apply_n: tipo de retorno deduzido (double, long long), sem truncar para int"
criterion counter   "make_counter: cada contador (e cada cópia) tem estado próprio"
criterion ownership "bind_sink: o callable é dono do sink (vive com ele, destruído uma vez)"
criterion captures  "tagger/at_least: closures não referenciam variáveis que já morreram"
criterion traits    "is_small_v: variable template correta (pequeno E trivialmente copiável)"

total=$((passed + failed))
printf '\n%d/%d critérios satisfeitos.\n' "$passed" "$total"
if ((failed > 0)); then
  echo "Dica: ./scripts/gitquest hints 1"
  exit 1
fi
echo "Desafio concluído!"
