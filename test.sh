#!/usr/bin/env bash
# Avaliador de desafio-30: código de saída 0 somente se todos os critérios passarem.
#   ./test.sh                     roda no container gcc:14.2.0 + valgrind 3.19 (recomendado)
#   GITQUEST_LOCAL=1 ./test.sh    roda com g++ >= 14 e valgrind da sua máquina (Linux)
#   GQ_VERBOSE=1 ./test.sh        mostra o log completo dos testes que falharem
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-30 — Dados que não cabiam no cache"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd valgrind "Instale valgrind (ex.: sudo apt install valgrind) ou rode sem GITQUEST_LOCAL."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  GQ_VERBOSE=${GQ_VERBOSE:-0} make --no-print-directory test
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-30 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
# tests/perf.sh roda o valgrind sob `setarch -R` (números de cache idênticos entre execuções);
# o perfil seccomp padrão do Docker Engine bloqueia isso, gq_seccomp_tsan libera só essa chamada.
GQ_DOCKER_EXTRA=(--security-opt "$(gq_seccomp_tsan)" -e "GQ_VERBOSE=${GQ_VERBOSE:-0}")
gq_docker_run "$IMAGE" 280 make --no-print-directory test
