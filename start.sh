#!/usr/bin/env bash
# Prepara o ambiente de desafio-22. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh                     # padrão: toolchain Go 1.25.3 em Docker
#   GITQUEST_LOCAL=1 ./start.sh    # alternativa: Go >= 1.25 local
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-22 — Mensagens pela metade"

if [[ "${GITQUEST_LOCAL:-}" == 1 ]]; then
  gq_require_cmd go "Instale Go >= 1.25 (https://go.dev/dl/) ou use o modo Docker (padrão)."
  gq_require_cmd make "Instale make (ex.: sudo apt install make) ou use o modo Docker (padrão)."
  v=$(go env GOVERSION | sed 's/^go//')
  gq_version_ge "$v" "1.25" || gq_die "Go $v é antigo demais; é necessário >= 1.25."
  gq_ok "Go $v local"
  run() { "$@"; }
else
  IMAGE=$(gq_toolchain_image desafio-22 Dockerfile)
  # Cache de build do Go num volume nomeado: as próximas execuções ficam rápidas.
  GQ_DOCKER_EXTRA=(-v gitquest-desafio-22-gocache:/gocache)
  gq_require_docker
  gq_docker_build "$IMAGE" - Dockerfile
  run() { gq_docker_run "$IMAGE" 300 "$@"; }
fi

gq_info "Compilando o código inicial"
run make --no-print-directory build || gq_die "o código não compilou — veja os erros acima"
gq_ok "compilado (build/echod)"

gq_info "Demonstração: os incidentes da operação, um por um (cmd/demo)"
run make --no-print-directory demo || gq_warn "a demonstração terminou com erro"

gq_banner_next_steps "./test.sh" \
  "5. Reveja a demonstração:    ./start.sh (compare 'esperado' com 'obtido')"
