#!/usr/bin/env bash
# Prepara o ambiente de desafio-27. Idempotente: pode ser executado várias vezes.
#   GITQUEST_LOCAL=1 ./start.sh   usa g++ >= 14, cmake >= 3.25 e ninja da sua máquina (Linux)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-27 — Só compila na minha máquina"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd cmake "Instale CMake >= 3.25 (https://cmake.org/download/) ou use o Docker."
  gq_require_cmd ninja "Instale o Ninja (ex.: sudo apt install ninja-build) ou use o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  cv=$(cmake --version | head -1 | sed 's/[^0-9.]//g')
  gq_version_ge "$cv" 3.25 || gq_die "cmake $cv é antigo demais; o desafio exige cmake >= 3.25 (ou use o Docker)."
  gq_ok "g++ $v, cmake $cv e ninja $(ninja --version) locais"
  gq_info "Compilando o código inicial"
  make --no-print-directory build || gq_die "textstats/ não compilou — veja os erros acima"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-27 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial (gcc 14.2, CMake 3.31.6, Ninja 1.12.1)"
gq_docker_run "$IMAGE" 300 make --no-print-directory build ||
  gq_die "textstats/ não compilou — veja os erros acima"
gq_banner_next_steps "./test.sh" \
  "5. Shell com cmake e ninja:   docker run --rm -it -v \"\$PWD\":/work -w /work $IMAGE bash"
