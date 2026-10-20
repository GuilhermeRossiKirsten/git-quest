#!/usr/bin/env bash
# Avaliador do desafio-4: código de saída 0 somente se todos os critérios passarem.
#
#   ./test.sh                      # padrão: roda no container gcc:14.2.0
#   GITQUEST_LOCAL=1 ./test.sh     # alternativa sem Docker: g++ >= 14 e make locais
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-4 — Contratos de template"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  cxx=${CXX:-g++}
  gq_require_cmd "$cxx" "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make ou rode sem GITQUEST_LOCAL para usar o Docker."
  version=$("$cxx" -dumpfullversion 2>/dev/null || "$cxx" -dumpversion)
  gq_version_ge "$version" 14 || gq_die "$cxx $version é antigo demais; este desafio exige g++ >= 14 (ou use o Docker)."
  if [[ "$(gq_os)" == windows ]]; then
    export SAN=""
    gq_warn "modo local no Windows: sanitizers indisponíveis; o resultado oficial é o do container."
  fi
  gq_info "Modo local: $cxx $version"
  exec make --no-print-directory test
fi

IMAGE=$(gq_toolchain_image desafio-4 Dockerfile)
gq_require_docker
gq_docker_ensure_image "$IMAGE" Dockerfile
gq_docker_run "$IMAGE" 280 make --no-print-directory test
