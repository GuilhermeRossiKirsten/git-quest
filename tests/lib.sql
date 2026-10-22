-- Funções auxiliares do avaliador (desafio-23). Tudo vive em pg_temp: some
-- quando a sessão termina e não precisa de privilégio de CREATE no banco.
--
-- As consultas de sql/queries/ são executadas como o papel "app" com
-- EXECUTE ... USING: os parâmetros chegam como $1, $2 (como num driver com
-- prepared statements) e cada execução é planejada com os valores reais.

CREATE TEMP TABLE gq_query (name text PRIMARY KEY, body text);
CREATE TEMP TABLE gq_result (seq serial, ok boolean, title text, details text[]);

-- Configurações do planejador que nenhuma consulta pode alterar.
CREATE FUNCTION pg_temp.gq_gucs() RETURNS text LANGUAGE sql AS $$
  SELECT string_agg(name || '=' || setting, ', ' ORDER BY name)
  FROM pg_settings
  WHERE name LIKE 'enable\_%'
     OR name IN ('random_page_cost', 'seq_page_cost', 'cpu_tuple_cost', 'cpu_index_tuple_cost',
                 'cpu_operator_cost', 'effective_cache_size', 'work_mem', 'hash_mem_multiplier',
                 'plan_cache_mode', 'from_collapse_limit', 'join_collapse_limit', 'geqo',
                 'default_statistics_target', 'constraint_exclusion', 'cursor_tuple_fraction',
                 'max_parallel_workers_per_gather', 'jit', 'search_path', 'TimeZone', 'DateStyle',
                 'statement_timeout')
$$;

-- Valores padrão explícitos (ignora o que um ALTER ROLE/DATABASE ... SET tenha posto).
CREATE FUNCTION pg_temp.gq_reset_gucs() RETURNS void LANGUAGE sql AS $$
  SELECT set_config(name, boot_val, false)
  FROM pg_settings
  WHERE name LIKE 'enable\_%'
     OR name IN ('random_page_cost', 'seq_page_cost', 'cpu_tuple_cost', 'cpu_index_tuple_cost',
                 'cpu_operator_cost', 'effective_cache_size', 'work_mem', 'hash_mem_multiplier',
                 'plan_cache_mode', 'from_collapse_limit', 'join_collapse_limit', 'geqo',
                 'default_statistics_target', 'constraint_exclusion', 'cursor_tuple_fraction',
                 'search_path');
  SELECT set_config('max_parallel_workers_per_gather', '0', false);
  SELECT set_config('jit', 'off', false);
  SELECT set_config('TimeZone', 'America/Sao_Paulo', false);
  SELECT set_config('DateStyle', 'ISO, MDY', false);
  SELECT set_config('statement_timeout', '60s', false);
$$;
DO $$
BEGIN
  PERFORM pg_temp.gq_reset_gucs();
  PERFORM set_config('gq.baseline', pg_temp.gq_gucs(), false);
END $$;

-- Depois de cada execução de uma consulta: ela não pode ter mudado o planejador
-- (ex.: set_config('enable_seqscan', 'off', ...) escondido num SELECT).
CREATE FUNCTION pg_temp.gq_guard(p_name text) RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  IF pg_temp.gq_gucs() IS DISTINCT FROM current_setting('gq.baseline') THEN
    RAISE EXCEPTION 'REGRA: %.sql alterou configurações do planejador/sessão (set_config/SET não são permitidos)', p_name;
  END IF;
END $$;

-- Texto da consulta, sem ';' e espaços finais (ela é embutida como subconsulta).
CREATE FUNCTION pg_temp.gq_sql(p_name text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE b text;
BEGIN
  SELECT body INTO b FROM gq_query WHERE name = p_name;
  IF b IS NULL OR btrim(b, E' \t\r\n') = '' THEN
    RAISE EXCEPTION 'sql/queries/%.sql ausente ou vazio', p_name;
  END IF;
  RETURN regexp_replace(b, '[\s;]+$', '');
END $$;

-- Resultado de uma consulta como jsonb (array de objetos, na ordem devolvida).
-- Embutir como subconsulta também garante "um único SELECT por arquivo".
CREATE FUNCTION pg_temp.gq_rows1(p_name text, a anyelement) RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE r jsonb;
BEGIN
  EXECUTE format(E'SELECT coalesce(jsonb_agg(to_jsonb(t)), ''[]'') FROM (%s\n) t', pg_temp.gq_sql(p_name))
    INTO r USING a;
  PERFORM pg_temp.gq_guard(p_name);
  RETURN r;
END $$;

CREATE FUNCTION pg_temp.gq_rows2(p_name text, a anyelement, b anycompatible) RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE r jsonb;
BEGIN
  EXECUTE format(E'SELECT coalesce(jsonb_agg(to_jsonb(t)), ''[]'') FROM (%s\n) t', pg_temp.gq_sql(p_name))
    INTO r USING a, b;
  PERFORM pg_temp.gq_guard(p_name);
  RETURN r;
END $$;

-- Cursor da última linha de uma página grande (sem materializar a página em jsonb).
CREATE FUNCTION pg_temp.gq_last_cursor(p_cursor text, p_size integer) RETURNS text LANGUAGE plpgsql AS $$
DECLARE c text;
BEGIN
  EXECUTE format(E'SELECT t.cursor::text FROM (%s\n) t OFFSET $2 - 1 LIMIT 1', pg_temp.gq_sql('orders_page'))
    INTO c USING p_cursor, p_size;
  PERFORM pg_temp.gq_guard('orders_page');
  IF c IS NULL THEN
    RAISE EXCEPTION 'orders_page(%, %) não devolveu % linhas com cursor', coalesce(p_cursor, 'NULL'), p_size, p_size;
  END IF;
  RETURN c;
END $$;

-- EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) — ou só o plano, com p_analyze = false.
CREATE FUNCTION pg_temp.gq_plan1(p_name text, a anyelement, p_analyze boolean DEFAULT true)
RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE r json;
BEGIN
  EXECUTE CASE WHEN p_analyze THEN 'EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) ' ELSE 'EXPLAIN (FORMAT JSON) ' END
          || pg_temp.gq_sql(p_name)
    INTO r USING a;
  PERFORM pg_temp.gq_guard(p_name);
  RETURN r::jsonb;
END $$;

CREATE FUNCTION pg_temp.gq_plan2(p_name text, a anyelement, b anycompatible)
RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE r json;
BEGIN
  EXECUTE 'EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) ' || pg_temp.gq_sql(p_name) INTO r USING a, b;
  PERFORM pg_temp.gq_guard(p_name);
  RETURN r::jsonb;
END $$;

-- Buffers lidos (shared hit + read) pela execução inteira, sem o planejamento.
CREATE FUNCTION pg_temp.gq_buffers(p jsonb) RETURNS bigint LANGUAGE sql AS $$
  SELECT (p -> 0 -> 'Plan' ->> 'Shared Hit Blocks')::bigint
       + (p -> 0 -> 'Plan' ->> 'Shared Read Blocks')::bigint
$$;

-- Tabelas grandes lidas por Seq Scan em algum ponto do plano.
CREATE FUNCTION pg_temp.gq_seqscans(p jsonb) RETURNS text[] LANGUAGE sql AS $$
  SELECT coalesce(array_agg(DISTINCT n ->> 'Relation Name'), '{}')
  FROM jsonb_path_query(p, 'strict $.**') AS n
  WHERE jsonb_typeof(n) = 'object'
    AND n ->> 'Node Type' LIKE '%Seq Scan'
    AND n ->> 'Relation Name' IN ('orders', 'customers', 'products')
$$;

-- Subconsultas reexecutadas para cada linha (SubPlan que não é "hashed").
CREATE FUNCTION pg_temp.gq_per_row_subplans(p jsonb) RETURNS text[] LANGUAGE sql AS $$
  SELECT coalesce(array_agg(DISTINCT s), '{}')
  FROM (SELECT n ->> 'Subplan Name' AS s
        FROM jsonb_path_query(p, 'strict $.**') AS n
        WHERE jsonb_typeof(n) = 'object' AND n ->> 'Parent Relationship' = 'SubPlan') x
  WHERE position('hashed ' || s IN p::text) = 0
$$;

-- Resumo do plano, um nó por linha: "Limit > Sort > Seq Scan on orders (linhas=...)".
CREATE FUNCTION pg_temp.gq_plan_lines(n jsonb, depth integer) RETURNS SETOF text LANGUAGE plpgsql AS $$
DECLARE child jsonb; line text;
BEGIN
  line := repeat('  ', depth) || '-> ' || (n ->> 'Node Type')
       || coalesce(' on ' || (n ->> 'Relation Name'), '')
       || coalesce(' using ' || (n ->> 'Index Name'), '')
       || coalesce(' (linhas=' || (n ->> 'Actual Rows')
                   || coalesce(', removidas pelo filtro=' || (n ->> 'Rows Removed by Filter'), '')
                   || ', buffers=' || ((n ->> 'Shared Hit Blocks')::bigint + (n ->> 'Shared Read Blocks')::bigint)
                   || ')', '');
  IF n ? 'Index Cond' THEN line := line || '  Index Cond: ' || left(n ->> 'Index Cond', 90); END IF;
  IF n ? 'Filter' THEN line := line || '  Filter: ' || left(n ->> 'Filter', 90); END IF;
  RETURN NEXT line;
  FOR child IN SELECT jsonb_array_elements(coalesce(n -> 'Plans', '[]')) LOOP
    RETURN QUERY SELECT * FROM pg_temp.gq_plan_lines(child, depth + 1);
  END LOOP;
END $$;

CREATE FUNCTION pg_temp.gq_plan_summary(p jsonb) RETURNS text LANGUAGE sql AS $$
  SELECT string_agg(l, E'\n') FROM (
    SELECT '    ' || l AS l FROM pg_temp.gq_plan_lines(p -> 0 -> 'Plan', 0) AS l LIMIT 14
  ) s
$$;

-- Compara duas listas de linhas (jsonb arrays). Devolve '{}' se iguais, senão
-- uma mensagem curta com a primeira diferença.
CREATE FUNCTION pg_temp.gq_cmp(got jsonb, want jsonb, label text) RETURNS text[] LANGUAGE plpgsql AS $$
DECLARE i integer; ng integer := jsonb_array_length(got); nw integer := jsonb_array_length(want);
BEGIN
  IF got = want THEN RETURN '{}'; END IF;
  FOR i IN 0 .. greatest(ng, nw) - 1 LOOP
    IF got -> i IS DISTINCT FROM want -> i THEN
      RETURN ARRAY[format('%s: esperadas %s linha(s), vieram %s; primeira diferença na linha %s:%s'
                          || E'\n      esperado: %s\n      veio:     %s',
                          label, nw, ng, i + 1, '',
                          coalesce(left((want -> i)::text, 160), '(nenhuma linha)'),
                          coalesce(left((got -> i)::text, 160), '(nenhuma linha)'))];
    END IF;
  END LOOP;
  RETURN ARRAY[label || ': resultados diferentes'];
END $$;

CREATE FUNCTION pg_temp.gq_record(ok boolean, title text, details text[] DEFAULT '{}') RETURNS void
LANGUAGE sql AS $$ INSERT INTO gq_result (ok, title, details) VALUES (ok, title, details) $$;

-- Mensagem de erro amigável para exceções capturadas nos critérios.
CREATE FUNCTION pg_temp.gq_err(msg text) RETURNS text LANGUAGE sql AS $$
  SELECT CASE WHEN msg LIKE 'REGRA:%' THEN msg ELSE 'erro ao executar: ' || msg END
$$;
