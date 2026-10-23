#!/usr/bin/env bash
# Avaliador do desafio-27: trata textstats/ como um projeto CMake qualquer e testa o
# COMPORTAMENTO do build, como faria outra máquina ou outra equipe:
#
#   - copia o projeto para um caminho com espaços (nunca compila na sua árvore);
#   - configura/compila fora da árvore, roda o ctest e o wordfreq;
#   - instala num prefixo temporário, MOVE o prefixo e esconde fonte e build;
#   - compila o projeto consumidor de tests/consumer contra o pacote instalado.
#
# Chamado por `make test` (dentro do container ou com GITQUEST_LOCAL=1).
#   GQ_VERBOSE=1   imprime o log completo de cada etapa que falhar
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
REPO=$PWD
LOGS="$REPO/build/logs"
rm -rf "$LOGS"
mkdir -p "$LOGS"

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/gq27.XXXXXX")
# shellcheck disable=SC2317  # chamada apenas pelo trap
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT

W="$TMPROOT/pasta com espacos"
SRC_PARENT="$W/meu projeto"
SRC="$SRC_PARENT/textstats"
BLD="$W/build estatico"
PREFIX="$W/prefixo instalado"
MOVED="$W/outro lugar/prefixo movido"
SH_SRC="$W/outra copia/textstats"
SH_BLD="$W/build compartilhado"
SH_PREFIX="$W/prefixo compartilhado"
SH_MOVED="$W/outro lugar/compartilhado movido"
CONSUMER="$W/consumidor"
ORIG="$TMPROOT/original"
mkdir -p "$W/outro lugar"

TITLES=(
  ""
  "configura e compila fora da árvore, num caminho com espaços, sem escrever nada na árvore de origem"
  "cmake --install + find_package(TextStats 1.4 CONFIG) e TextStats::textstats num projeto consumidor (prefixo movido)"
  "o consumidor da biblioteca instalada não enxerga headers privados"
  "requisitos de uso propagados: o consumidor recebe C++20 sem configurar nada (e pode pedir C++23)"
  "build incremental: o segundo build é no-op e tocar um .cpp recompila só esse arquivo"
  "BUILD_SHARED_LIBS=OFF gera e instala biblioteca estática; =ON gera e instala compartilhada"
  "warnings viram erro ao compilar o projeto, mas não vazam para o consumidor"
)
declare -a STATUS REASON
fail() { STATUS[$1]=0; REASON[$1]="${REASON[$1]:+${REASON[$1]}$'\n'}$2"; }
pass() { [[ "${STATUS[$1]:-}" == 0 ]] || STATUS[$1]=1; }

step() { printf '==> %s\n' "$*"; }

# Troca caminhos temporários por rótulos curtos nas mensagens.
tidy() {
  local line
  while IFS= read -r line; do
    line=${line//"$ORIG"/<original>}
    line=${line//"$SRC"/<fonte>}
    line=${line//"$BLD"/<build>}
    line=${line//"$SH_BLD"/<build-shared>}
    line=${line//"$SH_SRC"/<fonte-shared>}
    line=${line//"$MOVED"/<prefixo>}
    line=${line//"$SH_MOVED"/<prefixo-shared>}
    line=${line//"$PREFIX"/<prefixo-original>}
    line=${line//"$CONSUMER"/<consumidor>}
    line=${line//"$W"/<tmp>}
    printf '%s\n' "${line:0:220}"
  done
}

# Primeiras linhas úteis de um log de CMake/compilação.
explain_log() {
  local log=$1 out
  out=$(grep -m1 -A6 'CMake Error' "$log" | grep -v '^ *$' | head -5)
  [[ -z "$out" ]] && out=$(grep -m3 -E 'error:|undefined reference|relocation .* can not be used|cannot find -l|No such file or directory|FAILED:' "$log" | grep -v '^FAILED:' | head -3)
  [[ -z "$out" ]] && out=$(grep -m3 -E 'FAILED:|Error' "$log" | head -3)
  [[ -z "$out" ]] && out=$(tail -3 "$log")
  printf '%s\n' "$out" | tidy | sed 's/^[[:space:]]*/  /'
}

verbose_dump() {
  [[ "${GQ_VERBOSE:-0}" == 1 ]] || return 0
  printf '      ----- %s -----\n' "${1#"$REPO"/}"
  tidy <"$1" | sed 's/^/      | /'
}

run_log() {  # run_log <nome> <comando...>  (log em build/logs/<nome>.log)
  local log="$LOGS/$1.log"
  shift
  local rc=0
  timeout --kill-after=5 180 "$@" >"$log" 2>&1 || rc=$?
  return "$rc"
}

configure() {  # configure <nome> <fonte> <build> [args...]
  local name=$1 src=$2 bld=$3
  shift 3
  run_log "$name" cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF "$@"
}

# Cópia do projeto sem artefatos de builds in-source que estejam na sua árvore.
copy_project() {
  mkdir -p "$1"
  cp -R textstats/. "$1"/
  local -a junk
  mapfile -t junk < <(cd "$1" && find . \( -name CMakeCache.txt -o -name CMakeFiles -o -name cmake_install.cmake \
    -o -name build.ninja -o -name '.ninja_*' -o -name CTestTestfile.cmake -o -name compile_commands.json \
    -o -name Testing -o -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.so.*' -o -path ./src/config.h \
    -o \( -type d -name 'build*' \) \) -prune -print | sed 's#^\./##' | sort)
  if ((${#junk[@]} > 0)); then
    (cd "$1" && rm -rf -- "${junk[@]}")
    if [[ -z "${JUNK_WARNED:-}" ]]; then
      JUNK_WARNED=1
      printf 'aviso: textstats/ contém artefatos de build in-source (ignorados pelo avaliador): %s\n' "${junk[*]:0:6}" >&2
    fi
  fi
  freeze_mtimes "$1"
}

# Relógios de VMs (Docker Desktop/WSL2) às vezes andam para trás alguns segundos.
# Para que o diagnóstico de build incremental não dependa disso, as fontes copiadas
# recebem uma data fixa no passado e cada arquivo "tocado" pelo avaliador recebe uma
# data posterior a tudo o que já existe no diretório de build.
freeze_mtimes() { find "$1" -exec touch -h -d '2020-01-01 00:00:00 UTC' {} +; }
bump() {  # bump <arquivo> <diretório de build>
  local newest now
  newest=$(find "$2" -type f -printf '%T@\n' 2>/dev/null | sort -n | tail -1)
  newest=${newest%.*}
  now=$(date +%s)
  ((${newest:-0} > now)) && now=$newest
  touch -d "@$((now + 2))" "$1"
}

# Espera o relógio passar do arquivo mais novo dos diretórios dados (no máximo 90 s):
# se o relógio voltou no meio do build, saídas "do futuro" confundiriam o Ninja.
settle() {
  local newest i
  for ((i = 0; i < 90; i++)); do
    newest=$(find "$@" -type f -printf '%T@
' 2>/dev/null | sort -n | tail -1)
    newest=${newest%.*}
    (($(date +%s) > ${newest:-0})) && return 0
    sleep 1
  done
}

snapshot() {  # lista de diretórios + sha256 de cada arquivo
  (cd "$1" && find . -type d | LC_ALL=C sort && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum)
}

GOT_STATIC=0
# ---------------------------------------------------------------------------
step "Copiando textstats/ para um caminho com espaços: <tmp> = ${W}"
copy_project "$SRC"
cp -R "$SRC" "$ORIG"
snapshot "$SRC" >"$TMPROOT/before.txt"

# ---------------------------------------------------------------------------
step "[1] Configurando e compilando (Ninja, Release, BUILD_SHARED_LIBS=OFF, fora da árvore)"
built=0
if ! configure 01-configure "$SRC" "$BLD" -DBUILD_SHARED_LIBS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
  fail 1 "cmake falhou ao configurar num caminho com espaços (log: build/logs/01-configure.log):"
  fail 1 "$(explain_log "$LOGS/01-configure.log")"
  verbose_dump "$LOGS/01-configure.log"
elif ! run_log 01-build cmake --build "$BLD"; then
  fail 1 "a compilação falhou num caminho com espaços (log: build/logs/01-build.log):"
  fail 1 "$(explain_log "$LOGS/01-build.log")"
  verbose_dump "$LOGS/01-build.log"
else
  built=1
fi

snapshot "$SRC" >"$TMPROOT/after.txt"
changed=$(diff "$TMPROOT/before.txt" "$TMPROOT/after.txt" | sed -n 's/^> //p' |
  sed -E 's/^[0-9a-f]{64}  //' | sed 's#^\./##' | grep -v '^\.$' | LC_ALL=C sort -u | head -8 | tr '\n' ' ')
if [[ -n "$changed" ]]; then
  fail 1 "o build escreveu na árvore de origem (copiada para <fonte>): ${changed}"
fi

if ((built)); then
  if ! run_log 01-ctest ctest --test-dir "$BLD" --output-on-failure --timeout 60; then
    fail 1 "ctest falhou (log: build/logs/01-ctest.log):"
    fail 1 "$(grep -m3 -E 'FALHA|Failed|Not Run|Could not find' "$LOGS/01-ctest.log" | tidy | sed 's/^/  /')"
    verbose_dump "$LOGS/01-ctest.log"
  elif grep -q 'No tests were found' "$LOGS/01-ctest.log"; then
    fail 1 "ctest não encontrou nenhum teste: os testes de test/ precisam continuar registrados"
  fi
  wordfreq=$(find "$BLD" -type f -name wordfreq -perm -u+x | head -1)
  if [[ -z "$wordfreq" ]]; then
    fail 1 "o executável wordfreq não foi gerado no diretório de build"
  elif ! "$wordfreq" -n 3 "$REPO/tests/data/sample.txt" >"$LOGS/01-wordfreq.out" 2>&1; then
    fail 1 "wordfreq terminou com erro: $(head -2 "$LOGS/01-wordfreq.out" | tidy | tr '\n' ' ')"
  elif ! diff -u "$REPO/tests/data/sample.expected" "$LOGS/01-wordfreq.out" >"$LOGS/01-wordfreq.diff"; then
    fail 1 "a saída de 'wordfreq -n 3 tests/data/sample.txt' difere do esperado (build/logs/01-wordfreq.diff)"
  fi
fi
pass 1

if ((!built)); then
  # Sem build no caminho com espaços: tenta um caminho sem espaços para que os
  # demais critérios ainda deem um diagnóstico útil.
  W2="$TMPROOT/fallback"
  SRC_PARENT="$W2/fonte"
  SRC="$SRC_PARENT/textstats"
  BLD="$W2/build"
  ORIG="$W2/original"
  SH_SRC="$W2/outra-copia/textstats"
  SH_BLD="$W2/build-shared"
  copy_project "$SRC"
  cp -R "$SRC" "$ORIG"
  if configure 01-fallback-configure "$SRC" "$BLD" -DBUILD_SHARED_LIBS=OFF &&
    run_log 01-fallback-build cmake --build "$BLD"; then
    built=1
    printf '    (critério 1 falhou; os demais usam uma cópia num caminho SEM espaços)\n'
  else
    printf '    (o projeto também não compila num caminho sem espaços; veja build/logs/01-fallback-*.log)\n'
  fi
fi

if ((!built)); then
  for n in 2 3 4 5 6 7; do fail "$n" "não avaliado: o projeto não compila (veja o critério 1)"; done
else
  # -------------------------------------------------------------------------
  step "[5] Build incremental"
  # Até 3 builds seguidos: o primeiro ainda pode acomodar diferenças de relógio.
  noop=0
  for _ in 1 2 3; do
    settle "$BLD"
    run_log 05-second-build cmake --build "$BLD" || break
    if grep -q 'no work to do' "$LOGS/05-second-build.log"; then noop=1; break; fi
  done
  if ((!noop)); then
    fail 5 "um segundo 'cmake --build' sem nenhuma mudança ainda executou passos:"
    fail 5 "$(grep -E '^\[[0-9]+/[0-9]+\]' "$LOGS/05-second-build.log" | head -4 | tidy | sed 's/^/  /')"
  fi
  bump "$SRC/src/tokenizer.cpp" "$BLD"
  if run_log 05-touch-build cmake --build "$BLD"; then
    compiled=$(grep -oE 'Building CXX object [^ ]+' "$LOGS/05-touch-build.log" | sed 's/Building CXX object //' | tr '\n' ' ')
    count=$(grep -c 'Building CXX object' "$LOGS/05-touch-build.log")
    if [[ "$count" != 1 || "$compiled" != *tokenizer.cpp* ]]; then
      fail 5 "depois de tocar src/tokenizer.cpp foram recompilados $count objeto(s): ${compiled:-nenhum}(esperado: só tokenizer.cpp)"
    fi
  else
    fail 5 "o build depois de tocar src/tokenizer.cpp falhou (build/logs/05-touch-build.log)"
  fi
  pass 5

  # -------------------------------------------------------------------------
  step "[7a] Um warning novo no código do projeto precisa quebrar o build"
  printf '\nint gq_probe_library_warning() { int gq_unused_in_library = 0; return 1; }\n' >>"$SRC/src/tokenizer.cpp"
  printf '\nint gq_probe_app_warning() { int gq_unused_in_app = 0; return 1; }\n' >>"$SRC/app/main.cpp"
  bump "$SRC/src/tokenizer.cpp" "$BLD"
  bump "$SRC/app/main.cpp" "$BLD"
  if run_log 07-warning-probe cmake --build "$BLD" -- -k 0; then
    fail 7 "um 'int x;' não usado em src/tokenizer.cpp e em app/main.cpp NÃO quebrou o build: -Wall -Wextra precisam valer como erro no projeto"
  else
    for probe in gq_unused_in_library:src/tokenizer.cpp gq_unused_in_app:app/main.cpp; do
      if ! grep -E "${probe%%:*}" "$LOGS/07-warning-probe.log" | grep -qE 'error:|\[-Werror'; then
        fail 7 "uma variável não usada em ${probe#*:} não virou erro (-Wall -Wextra como erro no projeto)"
      fi
    done
  fi
  cp "$ORIG/src/tokenizer.cpp" "$SRC/src/tokenizer.cpp"
  cp "$ORIG/app/main.cpp" "$SRC/app/main.cpp"
  bump "$SRC/src/tokenizer.cpp" "$BLD"
  bump "$SRC/app/main.cpp" "$BLD"
  run_log 07-restore-build cmake --build "$BLD" || fail 7 "o build não voltou a passar depois de remover as sondas"

  # -------------------------------------------------------------------------
  step "[2] Instalando em <tmp>/prefixo instalado e movendo o prefixo para outro lugar"
  installed=0
  if ! run_log 02-install cmake --install "$BLD" --prefix "$PREFIX"; then
    fail 2 "cmake --install falhou (build/logs/02-install.log):"
    fail 2 "$(explain_log "$LOGS/02-install.log")"
  elif [[ -z "$(find "$PREFIX" -name 'TextStatsConfig.cmake' -o -name 'textstats-config.cmake' 2>/dev/null)" ]]; then
    fail 2 "cmake --install não instalou nenhum TextStatsConfig.cmake: find_package(TextStats CONFIG) não tem o que achar"
    fail 2 "  instalado: $( (cd "$PREFIX" 2>/dev/null && find . -type f | sed 's#^\./##' | head -6 | tr '\n' ' ') || echo nada)"
  else
    installed=1
  fi
  mv "$PREFIX" "$MOVED" 2>/dev/null || mkdir -p "$MOVED"
  freeze_mtimes "$MOVED"
  # Fonte e build somem durante o teste do consumidor: o pacote tem que se bastar.
  mv "$SRC_PARENT" "$SRC_PARENT.escondido"
  mv "$BLD" "$BLD.escondido"
  cp -R "$REPO/tests/consumer" "$CONSUMER"
  freeze_mtimes "$CONSUMER"

  consumer_ok=0
  if ((installed)); then
    step "[2] Consumidor com find_package(TextStats 1.4 CONFIG REQUIRED)"
    if ! configure 02-consumer-configure "$CONSUMER" "$W/consumidor A" -DCMAKE_PREFIX_PATH="$MOVED" -DCMAKE_CXX_STANDARD=20; then
      fail 2 "o consumidor não configurou (build/logs/02-consumer-configure.log):"
      fail 2 "$(explain_log "$LOGS/02-consumer-configure.log")"
      verbose_dump "$LOGS/02-consumer-configure.log"
    elif ! run_log 02-consumer-build cmake --build "$W/consumidor A"; then
      fail 2 "o consumidor encontrou o pacote mas não compilou/linkou (build/logs/02-consumer-build.log):"
      fail 2 "$(explain_log "$LOGS/02-consumer-build.log")"
      verbose_dump "$LOGS/02-consumer-build.log"
    elif ! run_log 02-consumer-run "$W/consumidor A/consumer" || ! grep -q GQ_CONSUMER_OK "$LOGS/02-consumer-run.log"; then
      fail 2 "o consumidor compilou mas falhou ao rodar: $(grep -m3 -E 'GQ_FAIL|error' "$LOGS/02-consumer-run.log" | tidy | tr '\n' ' ')"
    else
      consumer_ok=1
      grep -q 'GQ_TARGET_TYPE=STATIC_LIBRARY' "$LOGS/02-consumer-configure.log" && GOT_STATIC=1
    fi
  else
    fail 2 "consumidor não avaliado: não há pacote instalado"
  fi
  pass 2

  if ((installed)) && grep -q 'GQ_PACKAGE_VERSION=' "$LOGS/02-consumer-configure.log" 2>/dev/null; then
    step "[3/4] Consumidor sem padrão de C++ definido, com sondas de C++23 e de headers privados"
    if ! configure 34-consumer-configure "$CONSUMER" "$W/consumidor B" -DCMAKE_PREFIX_PATH="$MOVED" -DGQ_PROBES=ON; then
      fail 3 "o consumidor com sondas não configurou (build/logs/34-consumer-configure.log)"
      fail 4 "o consumidor com sondas não configurou (build/logs/34-consumer-configure.log)"
    else
      # [3] headers privados
      if ! run_log 03-probe-private cmake --build "$W/consumidor B" --target probe_private; then
        leaks=$(grep -oE 'GQ_LEAK [a-z0-9.]+' "$LOGS/03-probe-private.log" | sed 's/GQ_LEAK //' | sort -u | tr '\n' ' ')
        if [[ -n "$leaks" ]]; then
          fail 3 "o consumidor consegue incluir header(s) interno(s): ${leaks}(via include path exportado ou arquivos instalados)"
        else
          fail 3 "a sonda de headers privados não compilou (build/logs/03-probe-private.log):"
          fail 3 "$(explain_log "$LOGS/03-probe-private.log")"
        fi
      fi
      private_files=$( (cd "$MOVED" && find . -type f \( -name tokenizer.h -o -name utf8.h -o -name config.h -o -name '*.cpp' \)) | sed 's#^\./##' | tr '\n' ' ')
      [[ -n "$private_files" ]] && fail 3 "o prefixo instalado contém arquivos internos: ${private_files}"
      # [4] padrão propagado
      # Só compilação (o link já é avaliado no critério 2).
      if ! run_log 04-consumer-build cmake --build "$W/consumidor B" --target CMakeFiles/consumer.dir/main.cpp.o; then
        fail 4 "sem pedir nenhum padrão, o consumidor não compila com <textstats/textstats.h>:"
        fail 4 "$(explain_log "$LOGS/04-consumer-build.log")"
        verbose_dump "$LOGS/04-consumer-build.log"
      fi
      if ! run_log 04-consumer-cxx23 cmake --build "$W/consumidor B" --target CMakeFiles/consumer_cxx23.dir/cxx23.cpp.o; then
        if grep -q GQ_CXX23 "$LOGS/04-consumer-cxx23.log"; then
          fail 4 "um consumidor com CXX_STANDARD 23 foi rebaixado para um padrão anterior: a biblioteca força uma flag -std em vez de declarar um requisito mínimo"
        else
          fail 4 "o consumidor em C++23 não compilou (build/logs/04-consumer-cxx23.log):"
          fail 4 "$(explain_log "$LOGS/04-consumer-cxx23.log")"
        fi
      fi
    fi
  else
    fail 3 "não avaliado: o consumidor não encontrou o pacote (veja o critério 2)"
    fail 4 "não avaliado: o consumidor não encontrou o pacote (veja o critério 2)"
  fi
  pass 3
  pass 4

  if ((installed)) && ((consumer_ok)); then
    step "[7b] Consumidor com warnings próprios"
    if ! configure 07-consumer-configure "$CONSUMER" "$W/consumidor C" -DCMAKE_PREFIX_PATH="$MOVED" \
      -DCMAKE_CXX_STANDARD=20 -DGQ_NOISY=ON || ! run_log 07-consumer-build cmake --build "$W/consumidor C"; then
      fail 7 "os warnings do código do CONSUMIDOR viraram erro: flags de warning/-Werror vazaram pelo pacote:"
      log7="$LOGS/07-consumer-build.log"
      [[ -f "$log7" ]] || log7="$LOGS/07-consumer-configure.log"
      fail 7 "$(explain_log "$log7")"
    fi
  else
    fail 7 "consumidor com warnings não avaliado: o consumidor básico do critério 2 não funcionou"
  fi
  pass 7

  # Fonte e build voltam para o lugar.
  mv "$SRC_PARENT.escondido" "$SRC_PARENT"
  mv "$BLD.escondido" "$BLD"

  # -------------------------------------------------------------------------
  step "[6] BUILD_SHARED_LIBS=ON: configurar, compilar, testar, instalar e consumir"
  if ((!GOT_STATIC)); then
    if ((consumer_ok)); then
      fail 6 "com BUILD_SHARED_LIBS=OFF o alvo instalado TextStats::textstats não é uma biblioteca estática"
    else
      fail 6 "variante estática não confirmada: o consumidor do critério 2 não funcionou"
    fi
  elif [[ -z "$(find "$MOVED" -name 'libtextstats.a')" ]]; then
    fail 6 "com BUILD_SHARED_LIBS=OFF não foi instalada nenhuma libtextstats.a"
  fi
  copy_project "$SH_SRC"
  if ! configure 06-configure "$SH_SRC" "$SH_BLD" -DBUILD_SHARED_LIBS=ON; then
    fail 6 "BUILD_SHARED_LIBS=ON: a configuração falhou (build/logs/06-configure.log):"
    fail 6 "$(explain_log "$LOGS/06-configure.log")"
  elif ! run_log 06-build cmake --build "$SH_BLD"; then
    fail 6 "BUILD_SHARED_LIBS=ON: a compilação falhou (build/logs/06-build.log):"
    fail 6 "$(explain_log "$LOGS/06-build.log")"
    verbose_dump "$LOGS/06-build.log"
  elif ! run_log 06-ctest ctest --test-dir "$SH_BLD" --timeout 60; then
    fail 6 "BUILD_SHARED_LIBS=ON: ctest falhou (build/logs/06-ctest.log)"
  elif ! run_log 06-install cmake --install "$SH_BLD" --prefix "$SH_PREFIX"; then
    fail 6 "BUILD_SHARED_LIBS=ON: cmake --install falhou (build/logs/06-install.log)"
  else
    mkdir -p "$SH_PREFIX"   # um install que não instala nada nem cria o prefixo
    mv "$SH_PREFIX" "$SH_MOVED"
    freeze_mtimes "$SH_MOVED"
    if [[ -z "$(find "$SH_MOVED" -name 'libtextstats.so*')" ]]; then
      fail 6 "BUILD_SHARED_LIBS=ON não instalou nenhuma libtextstats.so: a biblioteca continua estática"
    fi
    if ((installed)); then
      if ! configure 06-consumer-configure "$CONSUMER" "$W/consumidor D" -DCMAKE_PREFIX_PATH="$SH_MOVED" -DCMAKE_CXX_STANDARD=20; then
        fail 6 "BUILD_SHARED_LIBS=ON: o consumidor não encontrou o pacote (build/logs/06-consumer-configure.log)"
      else
        grep -q 'GQ_TARGET_TYPE=SHARED_LIBRARY' "$LOGS/06-consumer-configure.log" ||
          fail 6 "BUILD_SHARED_LIBS=ON: o alvo instalado TextStats::textstats não é SHARED_LIBRARY ($(grep -oE 'GQ_TARGET_TYPE=[A-Z_]+' "$LOGS/06-consumer-configure.log"))"
        if ! run_log 06-consumer-build cmake --build "$W/consumidor D"; then
          fail 6 "BUILD_SHARED_LIBS=ON: o consumidor não compilou/linkou (build/logs/06-consumer-build.log):"
          fail 6 "$(explain_log "$LOGS/06-consumer-build.log")"
        elif ! run_log 06-consumer-run "$W/consumidor D/consumer" || ! grep -q GQ_CONSUMER_OK "$LOGS/06-consumer-run.log"; then
          fail 6 "BUILD_SHARED_LIBS=ON: o consumidor falhou ao rodar: $(head -2 "$LOGS/06-consumer-run.log" | tidy | tr '\n' ' ')"
        fi
      fi
    else
      fail 6 "BUILD_SHARED_LIBS=ON: consumidor não avaliado (não há pacote instalável, veja o critério 2)"
    fi
  fi
  pass 6
fi

# ---------------------------------------------------------------------------
echo
echo "Resultado por critério:"
passed=0
for n in 1 2 3 4 5 6 7; do
  if [[ "${STATUS[$n]:-0}" == 1 ]]; then
    printf '  \342\234\224 %d. %s\n' "$n" "${TITLES[$n]}"
    passed=$((passed + 1))
  else
    printf '  \342\234\230 %d. %s\n' "$n" "${TITLES[$n]}"
    printf '%s\n' "${REASON[$n]:-motivo desconhecido}" | sed 's/^/      /'
  fi
done
echo
if ((passed == 7)); then
  echo "Todos os 7 critérios passaram: o projeto compila — e é consumível — em qualquer máquina."
  exit 0
fi
printf '%d de 7 critérios passaram. Logs de cada etapa: build/logs/ (ou GQ_VERBOSE=1 ./test.sh)\n' "$passed"
exit 1
