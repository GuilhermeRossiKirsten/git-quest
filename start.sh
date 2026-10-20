#!/usr/bin/env bash
# Prepara o ambiente de desafio-3. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh                     usa Docker (gcc:14.2.0) — recomendado
#   GITQUEST_LOCAL=1 ./start.sh    usa o g++ >= 14 da sua máquina (Linux/WSL/macOS)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-3 — O valor que não estava lá"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  ver=$(g++ -dumpfullversion -dumpversion)
  gq_version_ge "$ver" 14 || gq_die "g++ $ver é antigo demais; é necessário g++ >= 14 (ou use o Docker)."
  gq_ok "g++ $ver local (modo GITQUEST_LOCAL)"
  run() { "$@"; }
else
  IMAGE=$(gq_toolchain_image desafio-3 Dockerfile)
  gq_require_docker
  gq_docker_build "$IMAGE" - Dockerfile
  run() { gq_docker_run "$IMAGE" 300 "$@"; }
fi

gq_info "Compilando o serviço relay (C++17, AddressSanitizer + UBSan)"
run make --no-print-directory build || gq_die "o código não compilou — veja os erros acima"
gq_ok "build/relay compilado"

gq_info "Rodando o relay com examples/relay.ini (compare com o arquivo!)"
if ! run make --no-print-directory run; then
  gq_warn "o relay terminou com erro — investigar isso faz parte do desafio"
fi

gq_banner_next_steps "./test.sh" \
  "5. Sem Docker (g++ >= 14):    GITQUEST_LOCAL=1 ./test.sh"
