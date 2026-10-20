#!/usr/bin/env bash
# Prepara o ambiente de __ID__. Idempotente: pode ser executado várias vezes.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

IMAGE=$(gq_toolchain_image __ID__ Dockerfile)

gq_title "GitQuest · __ID__ — __TITLE__"
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial"
gq_docker_run "$IMAGE" 300 make build || gq_die "o código inicial não compilou — verifique o Docker e tente novamente"
gq_banner_next_steps "./test.sh"
