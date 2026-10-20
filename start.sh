#!/usr/bin/env bash
# Prepara o ambiente de desafio-20. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh            # sobe o PostgreSQL, compila e roda a simulação
#   ./start.sh --down     # derruba o PostgreSQL e apaga o volume (banco descartável)
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

if [[ "${1:-}" == --down ]]; then
  gq_require_docker
  gq_info "Derrubando o ambiente ($GQ20_PROJECT)"
  compose down --volumes --remove-orphans
  gq_ok "ambiente removido"
  exit 0
fi
[[ $# -eq 0 ]] || gq_die "opção desconhecida: $1 (use ./start.sh ou ./start.sh --down)"

gq_title "GitQuest · desafio-20 — O consumidor que processava duas vezes"
gq20_require
gq_docker_build "$IMAGE" - Dockerfile
gq20_up

gq_info "Compilando o código inicial"
run make build || gq_die "o código não compilou — veja os erros acima"
gq_ok "compilado"

gq_info "Rodando a simulação (cmd/simulate): reentregas, broker instável e quedas"
run make --no-print-directory simulate || gq_warn "a simulação terminou com erro"

gq_banner_next_steps "./test.sh" \
  "5. Reveja a simulação:       ./start.sh" \
  "6. Inspecione o banco:       docker compose -p $GQ20_PROJECT exec postgres psql -U gitquest -d payments_sim" \
  "7. Ao terminar:              ./start.sh --down"
