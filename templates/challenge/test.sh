#!/usr/bin/env bash
# Avaliador de __ID__: código de saída 0 somente se todos os critérios passarem.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

IMAGE=$(gq_toolchain_image __ID__ Dockerfile)

gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 300 make test
