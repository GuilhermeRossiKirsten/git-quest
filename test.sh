#!/usr/bin/env bash
# Avaliador de desafio-3: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                      roda no container gcc:14.2.0 (recomendado)
#   GITQUEST_LOCAL=1 ./test.sh     roda com o g++ >= 14 local (precisa de ASan/UBSan)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "Avaliando desafio-3 — O valor que não estava lá"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  ver=$(g++ -dumpfullversion -dumpversion)
  gq_version_ge "$ver" 14 || gq_die "g++ $ver é antigo demais; é necessário g++ >= 14 (ou use o Docker)."
  timeout --kill-after=10 240 make --no-print-directory test
else
  IMAGE=$(gq_toolchain_image desafio-3 Dockerfile)
  gq_require_docker
  gq_docker_ensure_image "$IMAGE" Dockerfile
  gq_docker_run "$IMAGE" 240 make --no-print-directory test
fi
