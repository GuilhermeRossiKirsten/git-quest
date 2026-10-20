#!/usr/bin/env bash
# Prepara o ambiente de desafio-2. Idempotente: pode ser executado várias vezes.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

IMAGE=$(gq_toolchain_image desafio-2 Dockerfile)

gq_title "GitQuest · desafio-2 — Lambdas sem fronteiras"
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o pipeline inicial (C++14, gcc 14.2.0, ASan/UBSan)"
gq_docker_run "$IMAGE" 300 make build || gq_die "o código inicial não compilou — verifique o Docker e tente novamente"
gq_ok "build/pipeline compilado"
gq_banner_next_steps "./test.sh"
