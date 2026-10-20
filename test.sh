#!/usr/bin/env bash
# Avaliador de desafio-2: código de saída 0 somente se todos os critérios passarem.
#   ./test.sh                    roda os critérios no container gcc:14.2.0
#   GITQUEST_LOCAL=1 ./test.sh   roda com o g++ local (>= 14, Linux/WSL, com ASan/UBSan)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

if [[ "${GITQUEST_LOCAL:-}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  v=$(g++ -dumpfullversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; é necessário >= 14 (ou use o Docker)."
  exec timeout --kill-after=10 300 bash tests/run.sh
fi

IMAGE=$(gq_toolchain_image desafio-2 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 300 make test
