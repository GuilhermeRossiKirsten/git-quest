#!/usr/bin/env bash
# Prepara o ambiente de desafio-14. Idempotente: pode ser executado várias vezes.
#   GITQUEST_LOCAL=1 ./start.sh   usa o g++ >= 14 da sua máquina em vez do Docker
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-14 — Fila circular sem alocação"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  gq_ok "g++ $v local"
  gq_info "Compilando e executando a demonstração do log de telemetria"
  make --no-print-directory demo || gq_die "src/ring_buffer.h não compilou — veja os erros acima"
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh" "5. Rode a demonstração:      GITQUEST_LOCAL=1 make demo"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-14 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando e executando a demonstração do log de telemetria (C++17, gcc:14.2.0, ASan + UBSan)"
gq_docker_run "$IMAGE" 300 make --no-print-directory demo ||
  gq_die "src/ring_buffer.h não compilou — veja os erros acima"
gq_banner_next_steps "./test.sh" "5. Compare a demonstração acima com a saída esperada no README"
