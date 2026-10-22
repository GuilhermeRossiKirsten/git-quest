#!/usr/bin/env bash
# Prepara o ambiente de desafio-24. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh            # sobe o Redis, compila e roda a demonstração
#   ./start.sh --down     # derruba o Redis e remove o cache de build do Go
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

if [[ "${1:-}" == --down ]]; then
  gq_require_docker
  gq_info "Derrubando o ambiente ($GQ24_PROJECT)"
  compose down --volumes --remove-orphans
  docker volume rm gitquest-desafio-24-gocache >/dev/null 2>&1 || true
  gq_ok "ambiente removido"
  exit 0
fi
[[ $# -eq 0 ]] || gq_die "opção desconhecida: $1 (use ./start.sh ou ./start.sh --down)"

gq_title "GitQuest · desafio-24 — O cache que mentia"
gq24_require
gq_docker_build "$IMAGE" - Dockerfile
gq24_up

gq_info "Compilando o código inicial"
run make build || gq_die "o código não compilou — veja os erros acima"
gq_ok "compilado"

gq_info "Rodando a demonstração (cmd/demo): cenários de produção contra o Redis"
run make --no-print-directory demo || gq_warn "a demonstração terminou com erro"

gq_banner_next_steps "./test.sh" \
  "5. Reveja a demonstração:    ./start.sh" \
  "6. Inspecione o Redis:       docker compose -p $GQ24_PROJECT exec redis redis-cli -n 1" \
  "7. Ao terminar:              ./start.sh --down"
