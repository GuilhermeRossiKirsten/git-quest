#!/usr/bin/env bash
# Ferramenta de diagnóstico: executa uma consulta de sql/queries/ no banco "shop"
# (criado pelo ./start.sh) como a aplicação faria — PREPARE com os tipos do
# contrato + EXECUTE com os seus parâmetros — e mostra o plano real.
#
#   ./explain.sh <consulta> [parâmetros...]         EXPLAIN (ANALYZE, BUFFERS)
#   ./explain.sh --rows <consulta> [parâmetros...]  só o resultado
#   ./explain.sh --summary                          tempo e buffers de todas, com parâmetros de exemplo
#
# Exemplos:
#   ./explain.sh orders_page NULL 50
#   ./explain.sh orders_page pos:50000 50
#   ./explain.sh daily_sales 2025-03-27
#   ./explain.sh products_search KIT_000 20
#   ./explain.sh --rows customer_by_email SOFIA.BARBOSA3@OUTLOOK.COM
#
# Cada parâmetro vira um literal SQL; a palavra NULL vira NULL.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

# Tipos dos parâmetros de cada consulta (o contrato da aplicação).
param_types() {
  case "$1" in
    orders_page) echo "text, integer" ;;
    daily_sales) echo "date" ;;
    customer_by_email) echo "text" ;;
    products_search) echo "text, integer" ;;
    pending_queue) echo "integer, integer" ;;
    inactive_customers) echo "timestamptz" ;;
    *) return 1 ;;
  esac
}

usage() {
  sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
  echo "Consultas: orders_page daily_sales customer_by_email products_search pending_queue inactive_customers"
}

# run_query <modo: explain|rows|json> <consulta> [parâmetros...]
run_query() {
  local mode=$1 name=$2 types arg args="" sql
  shift 2
  types=$(param_types "$name") || gq_die "consulta desconhecida: $name (veja ./explain.sh --help)"
  [[ -f "sql/queries/$name.sql" ]] || gq_die "arquivo sql/queries/$name.sql não encontrado"
  for arg in "$@"; do
    [[ -n "$args" ]] && args+=", "
    if [[ "$arg" == NULL ]]; then args+="NULL"; else args+="'${arg//\'/\'\'}'"; fi
  done
  local exec="EXECUTE q($args)"
  case "$mode" in
    explain) exec="EXPLAIN (ANALYZE, BUFFERS) $exec" ;;
    summary) exec="EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF) $exec" ;;
  esac
  sql=$(printf 'SET client_min_messages = warning;\nPREPARE q(%s) AS\n%s\n;\n%s;\n' \
    "$types" "$(cat "sql/queries/$name.sql")" "$exec")
  printf '%s\n' "$sql" | pg_exec psql -X -q -v ON_ERROR_STOP=1 -U app -d shop
}

summary() {
  local rc=0 line name out
  local -a calls=(
    "orders_page NULL 50"
    "daily_sales 2025-03-27"
    "customer_by_email SOFIA.BARBOSA3@OUTLOOK.COM"
    "products_search ELE-00 20"
    "pending_queue 4 20"
    "inactive_customers 2025-11-01"
  )
  printf '  %-48s %12s %10s  %s\n' "consulta(parâmetros)" "tempo" "buffers" "Seq Scan em"
  for line in "${calls[@]}"; do
    read -r name _ <<<"$line"
    # shellcheck disable=SC2086 # os parâmetros de exemplo não têm espaços
    if ! out=$(run_query summary $line 2>&1); then
      printf '  %-48s %s\n' "$line" "ERRO: $(grep -m1 'ERROR' <<<"$out" || echo "$out" | head -1)"
      rc=1
      continue
    fi
    awk -v call="$line" '
      /Buffers: shared/ && !buf { buf = $0; sub(/.*Buffers: shared /, "", buf);
        n = 0; split(buf, parts, " "); for (i in parts) { split(parts[i], kv, "="); if (kv[1] == "hit" || kv[1] == "read") n += kv[2] } buf = n }
      /Seq Scan on (orders|customers|products)/ { t = $0; sub(/.*Seq Scan on /, "", t); sub(/ .*/, "", t); seq = seq (seq ? "," : "") t }
      /Execution Time/ { tm = $3 " ms" }
      END { printf "  %-48s %12s %10s  %s\n", call, tm, buf, (seq ? seq : "-") }' <<<"$out"
  done
  return "$rc"
}

case "${1:-}" in
  "" | -h | --help) usage; exit 0 ;;
esac
compose ps --status running --services 2>/dev/null | grep -qx postgres ||
  gq_die "o PostgreSQL do desafio não está rodando — rode ./start.sh"

case "$1" in
  --summary) summary ;;
  --rows) shift; [[ $# -ge 1 ]] || gq_die "uso: ./explain.sh --rows <consulta> [parâmetros...]"; run_query rows "$@" ;;
  *) run_query explain "$@" ;;
esac
