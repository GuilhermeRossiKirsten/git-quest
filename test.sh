#!/usr/bin/env bash
# Avaliador de desafio-6: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                     roda no container gcc:14.2.0 (sem rede, com limites)
#   GITQUEST_LOCAL=1 ./test.sh    roda com o g++ local (>= 14, com ASan/UBSan)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-6 — A tabela que esquecia"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd timeout "Instale GNU coreutils (timeout)."
  v=$(g++ -dumpfullversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; é necessário >= 14."
  exec make --no-print-directory test
fi

IMAGE=$(gq_toolchain_image desafio-6 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 280 make --no-print-directory test
