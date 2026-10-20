#!/usr/bin/env bash
# Prepara o ambiente de desafio-11. Idempotente: pode ser executado várias vezes.
#   GITQUEST_LOCAL=1 ./start.sh   usa o g++ >= 14 da sua máquina em vez do Docker
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-11 — Move semantics sem cópias escondidas"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  gq_ok "g++ $v local"
  gq_info "Compilando o código inicial"
  make --no-print-directory build || gq_die "o código em src/ não compilou — veja os erros acima"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-11 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial (C++11, gcc:14.2.0, ASan + UBSan)"
gq_docker_run "$IMAGE" 300 make --no-print-directory build ||
  gq_die "o código em src/ não compilou — veja os erros acima"
gq_banner_next_steps "./test.sh"
