#!/usr/bin/env bash
# Prepara o ambiente de desafio-6. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh                     usa Docker (gcc:14.2.0) — recomendado
#   GITQUEST_LOCAL=1 ./start.sh    usa o g++ local (>= 14, com ASan/UBSan; Linux/macOS)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-6 — A tabela que esquecia"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd timeout "Instale GNU coreutils (timeout)."
  v=$(g++ -dumpfullversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; é necessário >= 14 (ou rode sem GITQUEST_LOCAL para usar o Docker)."
  gq_ok "g++ $v local"
  gq_info "Compilando o código inicial"
  make --no-print-directory build || gq_die "o código inicial não compilou com o g++ local — use o Docker (./start.sh sem GITQUEST_LOCAL)"
else
  IMAGE=$(gq_toolchain_image desafio-6 Dockerfile)
  gq_require_docker
  gq_docker_build "$IMAGE" - Dockerfile
  gq_info "Compilando o código inicial (ASan/UBSan + -O2)"
  gq_docker_run "$IMAGE" 300 make --no-print-directory build || gq_die "o código inicial não compilou — verifique o Docker e tente novamente"
fi

gq_banner_next_steps "./test.sh" \
  "5. Os testes de correção rodam com AddressSanitizer/UBSan; o de desempenho, com -O2."
