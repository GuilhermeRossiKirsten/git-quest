#!/usr/bin/env bash
# Prepara o ambiente de desafio-23. Idempotente: pode ser executado várias vezes.
#
#   ./start.sh            # sobe o PostgreSQL, gera os dados (1ª vez) e recria o banco "shop"
#   ./start.sh --down     # derruba o PostgreSQL e apaga o volume (banco descartável)
#
# Rode de novo depois de editar sql/indexes.sql para recriar "shop" com os novos índices.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

if [[ "${1:-}" == --down ]]; then
  gq_require_docker
  gq_info "Derrubando o ambiente ($GQ23_PROJECT)"
  compose down --volumes --remove-orphans
  gq_ok "ambiente removido"
  exit 0
fi
[[ $# -eq 0 ]] || gq_die "opção desconhecida: $1 (use ./start.sh ou ./start.sh --down)"

gq_title "GitQuest · desafio-23 — A consulta que varria a tabela"
gq23_require
gq23_up
gq23_seed
gq23_shop

gq_info "Medindo as consultas da aplicação no banco 'shop'"
./explain.sh --summary || gq_warn "alguma consulta falhou (veja acima)"

gq_banner_next_steps "./test.sh" \
  "5. Plano de uma consulta:     ./explain.sh orders_page NULL 50   (veja ./explain.sh --help)" \
  "6. psql no banco:             docker compose -p $GQ23_PROJECT exec postgres psql -U gitquest -d shop" \
  "7. Novos índices no 'shop':   edite sql/indexes.sql e rode ./start.sh de novo" \
  "8. Ao terminar:               ./start.sh --down"
