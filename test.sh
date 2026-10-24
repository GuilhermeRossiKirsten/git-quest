#!/usr/bin/env bash
# Avaliador de desafio-32: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                     roda no container gcc:14.2.0 (recomendado)
#   GITQUEST_LOCAL=1 ./test.sh    roda com gcc >= 14, make e python3 da sua máquina (Linux)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-32 — O pacote que derrubava o coletor"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  [[ "$(gq_os)" == linux || "$(gq_os)" == wsl ]] ||
    gq_die "o modo local precisa de Linux (ASan/LSan e open_memstream da glibc); use o modo Docker (padrão)."
  gq_require_cmd gcc "Instale gcc >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd python3 "Instale python3 (o avaliador é um script Python)."
  v=$(gcc -dumpfullversion 2>/dev/null || gcc -dumpversion)
  gq_version_ge "$v" 14 || gq_die "gcc $v é antigo demais; o desafio exige gcc >= 14 (ou use o Docker)."
  make --no-print-directory test
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-32 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 280 make --no-print-directory test
