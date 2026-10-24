#!/usr/bin/env bash
# Prepara o ambiente de desafio-32. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh                     # padrão: toolchain gcc:14.2.0 em Docker
#   GITQUEST_LOCAL=1 ./start.sh    # alternativa: Linux com gcc >= 14, make e python3
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
  gq_ok "gcc $v local"
  run() { "$@"; }
else
  IMAGE=$(gq_toolchain_image desafio-32 Dockerfile)
  gq_require_docker
  gq_docker_build "$IMAGE" - Dockerfile
  run() { gq_docker_run "$IMAGE" 120 "$@"; }
fi

gq_info "Compilando o código inicial (C17, -Werror, ASan + UBSan)"
run make --no-print-directory build || gq_die "o código não compilou — veja os erros acima"

gq_info "Demonstração: três pacotes que chegaram do campo"
run make --no-print-directory demo || gq_warn "a demonstração terminou com erro"

gq_banner_next_steps "./test.sh" \
  "5. Reveja a demonstração:    ./start.sh (ou: make demo dentro do container)"
