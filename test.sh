#!/usr/bin/env bash
# Avaliador de desafio-27: código de saída 0 somente se os 7 critérios passarem.
#   ./test.sh                     roda no container (gcc:14.2.0 + CMake 3.31.6 + Ninja 1.12.1)
#   GITQUEST_LOCAL=1 ./test.sh    usa g++ >= 14, cmake >= 3.25 e ninja da sua máquina (Linux)
#   GQ_VERBOSE=1 ./test.sh        mostra o log completo das etapas que falharem
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-27 — Só compila na minha máquina"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd cmake "Instale CMake >= 3.25 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd ninja "Instale o Ninja ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  GQ_VERBOSE=${GQ_VERBOSE:-0} make --no-print-directory test
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-27 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
GQ_DOCKER_EXTRA=(-e "GQ_VERBOSE=${GQ_VERBOSE:-0}")
gq_docker_run "$IMAGE" 280 make --no-print-directory test
