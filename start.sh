#!/usr/bin/env bash
# Prepara o ambiente de desafio-7. Idempotente: pode ser executado várias vezes.
#   ./start.sh                     # padrão: toolchain Go 1.25.3 em Docker
#   GITQUEST_LOCAL=1 ./start.sh    # alternativa: Go >= 1.25.3 local com cgo
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=tests/local.sh
source tests/local.sh

IMAGE=$(gq_toolchain_image desafio-7 Dockerfile)
# Volume nomeado com o cache de compilação do Go (lido por gq_docker_run).
# shellcheck disable=SC2034
GQ_DOCKER_EXTRA=(-v gitquest-go-cache:/cache)

gq_title "GitQuest · desafio-7 — O pool que transbordou"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_check_local_go
  gq_info "Compilando o código inicial"
  go build ./... || gq_die "o código não compilou — veja os erros acima"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh" \
    "5. Observe os sintomas:      GITQUEST_LOCAL=1 ./demo.sh -fail 3"
  exit 0
fi

gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial (Go 1.25.3 no container)"
gq_docker_run "$IMAGE" 300 go build ./... || gq_die "o código não compilou — veja os erros acima"
gq_ok "código inicial compilado"
gq_banner_next_steps "./test.sh" \
  "5. Observe os sintomas:      ./demo.sh -fail 3"
