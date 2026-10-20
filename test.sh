#!/usr/bin/env bash
# Avaliador de desafio-7: código de saída 0 somente se todos os critérios passarem.
#   ./test.sh                     # padrão: toolchain Go 1.25.3 em Docker
#   GITQUEST_LOCAL=1 ./test.sh    # alternativa: Go >= 1.25.3 local com cgo
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=tests/local.sh
source tests/local.sh

gq_title "GitQuest · desafio-7 — critérios de conclusão"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_check_local_go
  bash tests/run.sh
  exit $?
fi

IMAGE=$(gq_toolchain_image desafio-7 Dockerfile)
# Volume nomeado com o cache de compilação do Go (lido por gq_docker_run).
# shellcheck disable=SC2034
GQ_DOCKER_EXTRA=(-v gitquest-go-cache:/cache)

gq_require_docker
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  gq_info "Imagem da toolchain ausente; preparando o ambiente (o mesmo que ./start.sh faz)"
  gq_docker_build "$IMAGE" - Dockerfile
fi
gq_docker_run "$IMAGE" 240 bash tests/run.sh
