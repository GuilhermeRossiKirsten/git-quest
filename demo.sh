#!/usr/bin/env bash
# Roda o gerador de miniaturas (src/cmd/thumbs) com o detector de corridas,
# para você observar os sintomas. Os argumentos são repassados ao programa:
#   ./demo.sh                 # 500 fotos, 8 workers
#   ./demo.sh -fail 3         # 3 fotos corrompidas no lote
#   ./demo.sh -n 50 -workers 2
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=tests/local.sh
source tests/local.sh

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_check_local_go
  exec go run -race ./src/cmd/thumbs "$@"
fi

IMAGE=$(gq_toolchain_image desafio-7 Dockerfile)
# shellcheck disable=SC2034
GQ_DOCKER_EXTRA=(-v gitquest-go-cache:/cache)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 120 go run -race ./src/cmd/thumbs "$@"
