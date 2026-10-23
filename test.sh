#!/usr/bin/env bash
# Avaliador de desafio-13: código de saída 0 somente se todos os critérios passarem.
#   ./test.sh                     roda no container gcc:14.2.0 (recomendado)
#   GITQUEST_LOCAL=1 ./test.sh    roda com o g++ >= 14 da sua máquina (Linux)
#   GQ_VERBOSE=1 ./test.sh        mostra o log dos testes que falharem
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-13 — jthread e stop tokens"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  [[ "$(gq_os)" == linux || "$(gq_os)" == wsl ]] ||
    gq_die "o modo local exige Linux (os testes leem /proc e usam o ThreadSanitizer); rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  GQ_VERBOSE=${GQ_VERBOSE:-0} make --no-print-directory test
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-13 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
# O TSan roda com `setarch -R` (veja tests/run.sh). O perfil seccomp padrão do Docker
# Engine bloqueia essa chamada; gq_seccomp_tsan usa o padrão + só personality(ADDR_NO_RANDOMIZE).
# Os limites de CPU/memória/PIDs continuam valendo.
GQ_DOCKER_EXTRA=(--security-opt "$(gq_seccomp_tsan)" -e "GQ_VERBOSE=${GQ_VERBOSE:-0}")
gq_docker_run "$IMAGE" 280 make --no-print-directory test
