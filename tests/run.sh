#!/usr/bin/env bash
# Avaliador do desafio-23 — roda DENTRO do container postgres (chamado pelo
# test.sh via "docker compose exec"). Imprime um critério por linha:
#   R|1|título   (satisfeito)      R|0|título   (pendente)
#   D|detalhe    (linhas de detalhe do critério anterior)
#
# 1. Copia o banco-modelo shop_template para shop_test (dados idênticos sempre).
# 2. Aplica sql/indexes.sql como o papel "app" e roda ANALYZE.
# 3. Confere que o arquivo de índices só criou índices, dentro do orçamento.
# 4. Executa tests/checks.sql (como "app"): correção e plano de cada consulta.
set -euo pipefail
export PGOPTIONS="-c client_min_messages=warning"
cd /challenge || exit 1

DB=shop_test
MAX_INDEXES=6
MAX_INDEX_MB=64
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

admin() { psql -X -q -v ON_ERROR_STOP=1 -U gitquest "$@"; }
crit() { printf 'R|%s|%s\n' "$1" "$2"; }
detail() { printf '%s\n' "$@" | sed 's/^/D|/'; }

# Um "ALTER ROLE app SET ..." de uma execução anterior não sobrevive.
admin -d postgres -c "ALTER ROLE app RESET ALL" -c "DROP DATABASE IF EXISTS $DB WITH (FORCE)" \
  -c "CREATE DATABASE $DB TEMPLATE shop_template STRATEGY FILE_COPY" >/dev/null

# Impressão digital do que indexes.sql NÃO pode mudar: colunas, restrições,
# gatilhos, regras, RLS, opções das tabelas, dados e configurações por papel/banco.
fingerprint() {
  admin -d "$DB" -At <<'SQL'
SELECT string_agg(x, E'\n' ORDER BY x) FROM (
  SELECT 'col ' || c.relname || '.' || a.attname || ' ' || format_type(a.atttypid, a.atttypmod)
         || CASE WHEN a.attnotnull THEN ' not null' ELSE '' END
         || coalesce(' collate ' || nullif(a.attcollation::regcollation::text, 'default'), '') AS x
  FROM pg_attribute a JOIN pg_class c ON c.oid = a.attrelid
  WHERE c.relname IN ('orders', 'customers', 'products') AND c.relkind = 'r' AND a.attnum > 0 AND NOT a.attisdropped
  UNION ALL
  SELECT 'constraint ' || conrelid::regclass || ' ' || conname || ' ' || pg_get_constraintdef(oid)
  FROM pg_constraint WHERE connamespace = 'public'::regnamespace
  UNION ALL
  SELECT 'table ' || relname || ' rls=' || relrowsecurity || '/' || relforcerowsecurity
         || ' options=' || coalesce(array_to_string(reloptions, ','), '') || ' kind=' || relkind::text
  FROM pg_class WHERE relnamespace = 'public'::regnamespace AND relkind NOT IN ('i', 'S')
  UNION ALL SELECT 'triggers ' || count(*) FROM pg_trigger WHERE NOT tgisinternal
  UNION ALL SELECT 'rules ' || count(*) FROM pg_rules WHERE schemaname = 'public'
  UNION ALL SELECT 'policies ' || count(*) FROM pg_policy
  UNION ALL SELECT 'functions ' || count(*) FROM pg_proc WHERE pronamespace = 'public'::regnamespace
  UNION ALL SELECT 'role/db settings ' || count(*) FROM pg_db_role_setting
  UNION ALL SELECT 'data orders ' || count(*) || ' ' || sum(hashtext(o::text)) FROM orders o
  UNION ALL SELECT 'data customers ' || count(*) || ' ' || sum(hashtext(c::text)) FROM customers c
  UNION ALL SELECT 'data products ' || count(*) || ' ' || sum(hashtext(p::text)) FROM products p
) s;
SQL
}

fingerprint >"$work/before"

# --- índices do participante ------------------------------------------------------
if [[ ! -f sql/indexes.sql ]]; then
  crit 0 "sql/indexes.sql aplicado sem erros"
  detail "arquivo sql/indexes.sql não encontrado"
elif psql -X -q -v ON_ERROR_STOP=1 -U app -d "$DB" -f sql/indexes.sql >"$work/indexes.log" 2>&1; then
  crit 1 "sql/indexes.sql aplicado sem erros"
else
  crit 0 "sql/indexes.sql aplicado sem erros"
  detail "o psql parou no primeiro erro (os índices anteriores a ele foram criados):" \
    "$(sed 's/^/    /' "$work/indexes.log" | head -8)"
fi
# Amostra = tabela inteira (300 x 2000 >= 600 mil linhas): estatísticas, e
# portanto planos, iguais em toda execução.
admin -d "$DB" -c "SET default_statistics_target = 2000" -c "ANALYZE" >/dev/null

fingerprint >"$work/after"
if cmp -s "$work/before" "$work/after"; then
  crit 1 "sql/indexes.sql só cria índices (tabelas, dados e configurações intactos)"
else
  crit 0 "sql/indexes.sql só cria índices (tabelas, dados e configurações intactos)"
  detail "o arquivo de índices mudou algo além de índices (ex.: ALTER TABLE/ROLE, SET, DELETE):" \
    "$(diff "$work/before" "$work/after" | grep -E '^[-+][^-+]' | sed 's/^-/    antes:  /; s/^+/    depois: /' | head -8)"
fi

# Orçamento: índices criados pelo participante (não contam PK e UNIQUE do schema).
budget=$(admin -d "$DB" -At -F'|' <<SQL
SELECT count(*), coalesce(sum(pg_relation_size(i.indexrelid)), 0),
       coalesce(string_agg(format('%s (%s)', i.indexrelid::regclass, pg_size_pretty(pg_relation_size(i.indexrelid))),
                           ', ' ORDER BY i.indexrelid::regclass::text), 'nenhum')
FROM pg_index i
WHERE i.indrelid IN ('orders'::regclass, 'customers'::regclass, 'products'::regclass)
  AND NOT EXISTS (SELECT 1 FROM pg_constraint c WHERE c.conindid = i.indexrelid);
SQL
)
IFS='|' read -r n_idx idx_bytes idx_list <<<"$budget"
idx_mb=$(( (idx_bytes + 1048575) / 1048576 ))
title="índices enxutos: no máximo $MAX_INDEXES índices e $MAX_INDEX_MB MB somados"
if ((n_idx <= MAX_INDEXES && idx_bytes <= MAX_INDEX_MB * 1048576)); then
  crit 1 "$title"
  detail "$n_idx índice(s), ${idx_mb} MB: $idx_list"
else
  crit 0 "$title"
  detail "$n_idx índice(s), ${idx_mb} MB: $idx_list" \
    "cada índice custa espaço e deixa INSERT/UPDATE mais lentos — mantenha só os que alguma consulta usa"
fi

# --- consultas -------------------------------------------------------------------
{
  for q in orders_page daily_sales customer_by_email products_search pending_queue inactive_customers; do
    # Comando do psql: a crase executa o cat (é texto literal para o Bash).
    # shellcheck disable=SC2016
    printf '\\set q `cat sql/queries/%s.sql 2>/dev/null`\n' "$q"
    printf "INSERT INTO gq_query VALUES ('%s', :'q');\n" "$q"
  done
} >"$work/load.sql"

if ! psql -X -q -At -v ON_ERROR_STOP=1 -U app -d "$DB" \
     -c "SET client_min_messages = warning" \
     -f tests/lib.sql -f "$work/load.sql" -f tests/checks.sql \
     -c "SELECT line FROM (
           SELECT seq, 0 AS k, 'R|' || CASE WHEN ok THEN 1 ELSE 0 END || '|' || title AS line FROM gq_result
           UNION ALL
           SELECT seq, k, 'D|' || replace(d, E'\n', E'\nD|') FROM gq_result, unnest(details) WITH ORDINALITY u(d, k)
         ) s ORDER BY seq, k" 2>"$work/checks.err"; then
  crit 0 "avaliador das consultas executado até o fim"
  detail "$(head -5 "$work/checks.err")"
fi

admin -d postgres -c "DROP DATABASE IF EXISTS $DB WITH (FORCE)" >/dev/null
