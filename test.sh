#!/usr/bin/env bash
# Avaliador de desafio-5: código de saída 0 somente se todos os critérios passarem.
#   ./test.sh                      # roda no container gcc:14.2.0 (sem rede, com limites)
#   GITQUEST_LOCAL=1 ./test.sh     # sem Docker: usa g++ >= 14 e make locais
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd make "Instale make ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_warn "modo local: o resultado oficial é o do container gcc:14.2.0"
  exec bash tests/run.sh
fi

IMAGE=$(gq_toolchain_image desafio-5 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 280 bash tests/run.sh
