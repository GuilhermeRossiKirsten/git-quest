#!/usr/bin/env bash
# Prepara o ambiente de desafio-33. Idempotente: pode ser executado várias vezes.
#   GITQUEST_LOCAL=1 ./start.sh   usa o g++ >= 14 da sua máquina (Linux) em vez do Docker
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-33 — A fila sem trava que travava"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  [[ "$(gq_os)" == linux || "$(gq_os)" == wsl ]] ||
    gq_die "o modo local exige Linux (os testes usam o ThreadSanitizer e o LeakSanitizer); rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd setarch "Instale util-linux (o ThreadSanitizer roda via 'setarch -R')."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  gq_ok "g++ $v local"
  gq_info "Compilando o código inicial (C++20, ASan+UBSan, TSan e -O2)"
  make --no-print-directory build || gq_die "o código em src/ não compilou — veja os erros acima"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-33 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o código inicial (C++20, gcc:14.2.0, ASan+UBSan, TSan e -O2)"
gq_docker_run "$IMAGE" 300 make --no-print-directory build ||
  gq_die "o código em src/ não compilou — veja os erros acima"
gq_banner_next_steps "./test.sh" "5. Log completo de um teste: GQ_VERBOSE=1 ./test.sh"
