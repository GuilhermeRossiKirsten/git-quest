#!/usr/bin/env bash
# Avalia uma fórmula com o seu src/formula e mostra a AST com todos os parênteses.
#
#   ./calc.sh '2 ^ 3 ^ 2'
#   ./calc.sh 'preco * (1 - desconto)' preco=80 desconto=0.1
#   GITQUEST_LOCAL=1 ./calc.sh '1 2'      # com Go local
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

(($# >= 1)) || gq_die "uso: ./calc.sh 'fórmula' [nome=valor ...]"

if [[ "${GITQUEST_LOCAL:-}" == 1 ]]; then
  gq_require_cmd go "Instale Go >= 1.25 ou use o modo Docker (padrão)."
  go run ./cmd/calc "$@"
else
  IMAGE=$(gq_toolchain_image desafio-31 Dockerfile)
  GQ_DOCKER_EXTRA=(-v gitquest-desafio-31-gocache:/gocache)
  gq_require_docker
  gq_docker_ensure_image "$IMAGE" Dockerfile
  gq_docker_run "$IMAGE" 60 go run ./cmd/calc "$@"
fi
