#!/usr/bin/env bash
# Prepara o ambiente de desafio-5. Idempotente: pode ser executado várias vezes.
#   ./start.sh                     # toolchain em container (gcc:14.2.0)
#   GITQUEST_LOCAL=1 ./start.sh    # sem Docker: usa g++ >= 14 e make locais
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-5 — Erros que não se escondem"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd "${CXX:-g++}" "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  version=$("${CXX:-g++}" -dumpfullversion -dumpversion)
  gq_version_ge "$version" 14 || gq_die "g++ $version é antigo demais: o desafio exige g++ >= 14 (C++23 com <print> e ranges::to). Rode sem GITQUEST_LOCAL para usar o Docker."
  gq_ok "g++ $version local"
  make -s toolchain || gq_die "o compilador local não tem os recursos de C++23 exigidos (veja a mensagem acima)"
  gq_info "Compilando o código inicial"
  make -s build || gq_die "o código em src/ não compilou"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh" "5. Experimente:              ./build/loganalyzer tests/data/access.log"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-5 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Verificando o suporte a C++23 do compilador do container"
gq_docker_run "$IMAGE" 120 make -s toolchain || gq_die "a imagem $IMAGE não tem os recursos de C++23 exigidos"
gq_info "Compilando o código inicial"
gq_docker_run "$IMAGE" 300 make -s build || gq_die "o código inicial não compilou — verifique o Docker e tente novamente"
gq_ok "build/loganalyzer pronto"
gq_banner_next_steps "./test.sh" "5. Log de exemplo:           tests/data/access.log (relatório esperado: tests/data/access.expected)"
