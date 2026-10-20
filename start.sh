#!/usr/bin/env bash
# Prepara o ambiente de desafio-12. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh                     # padrão: imagem Docker gcc:14.2.0
#   GITQUEST_LOCAL=1 ./start.sh    # alternativa sem Docker: g++ >= 14 e make locais
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-12 — Corrotinas que geram"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  cxx=${CXX:-g++}
  gq_require_cmd "$cxx" "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make ou rode sem GITQUEST_LOCAL para usar o Docker."
  version=$("$cxx" -dumpfullversion 2>/dev/null || "$cxx" -dumpversion)
  gq_version_ge "$version" 14 || gq_die "$cxx $version é antigo demais; este desafio exige g++ >= 14 (ou use o Docker)."
  [[ "$(gq_os)" == windows ]] && export SAN=""
  gq_ok "compilador local: $cxx $version"
  gq_info "Compilando o código inicial"
  make --no-print-directory build || gq_die "o código inicial não compilou com $cxx $version"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh" \
    "5. Veja os sintomas:          ./build/logscan"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-12 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial (g++ 14.2, -std=c++23 -pedantic-errors)"
gq_docker_run "$IMAGE" 300 make --no-print-directory build ||
  gq_die "o código inicial não compilou — verifique o Docker e tente novamente"
gq_info "Saída atual do relatório build/logscan (sintomas):"
gq_docker_run "$IMAGE" 60 ./build/logscan || gq_warn "o relatório terminou com erro — isso faz parte dos sintomas"
gq_banner_next_steps "./test.sh"
