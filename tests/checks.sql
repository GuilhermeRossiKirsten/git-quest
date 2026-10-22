-- Critérios do desafio-23, um bloco DO por critério. Cada bloco grava em
-- gq_result: ok/falha, título e detalhes (diferenças, plano resumido, buffers).
--
-- Correção: cada consulta é comparada, para vários parâmetros (inclusive
-- bordas), com uma consulta de referência simples e correta escrita aqui.
-- Plano: EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) — sem Seq Scan nas tabelas
-- grandes e no máximo N buffers (shared hit + read) por execução. Os limites
-- são folgados: a solução de referência usa de 5 a 10 vezes menos.

-- Referências -----------------------------------------------------------------

CREATE FUNCTION pg_temp.ref_orders(p_offset bigint, p_limit bigint) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT id, customer_id, status, total_cents, created_at
    FROM orders ORDER BY created_at DESC, id DESC OFFSET p_offset LIMIT p_limit) t
$$;

CREATE FUNCTION pg_temp.ref_daily_sales(d date) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT status, count(*) AS orders, sum(total_cents) AS revenue_cents
    FROM orders WHERE date(created_at) = d GROUP BY status ORDER BY status) t
$$;

CREATE FUNCTION pg_temp.ref_customer_by_email(e text) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT id, email, name FROM customers WHERE lower(email) = lower(e) ORDER BY id) t
$$;

CREATE FUNCTION pg_temp.ref_products_search(prefix text, lim integer) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT sku, name, price_cents FROM products
    WHERE starts_with(sku, prefix) ORDER BY sku COLLATE "C" LIMIT lim) t
$$;

CREATE FUNCTION pg_temp.ref_pending_queue(wh integer, lim integer) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT id, customer_id, total_cents, created_at FROM orders
    WHERE status = 'pending' AND warehouse_id = wh ORDER BY created_at, id LIMIT lim) t
$$;

CREATE FUNCTION pg_temp.ref_inactive_customers(since timestamptz) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(to_jsonb(t)), '[]') FROM (
    SELECT c.id, c.email FROM customers c
    WHERE c.created_at < since
      AND NOT EXISTS (SELECT 1 FROM orders o
                      WHERE o.customer_id = c.id
                        AND o.status IN ('paid', 'shipped', 'delivered')
                        AND o.created_at >= since)
    ORDER BY c.id) t
$$;

CREATE FUNCTION pg_temp.gq_strip_cursor(r jsonb) RETURNS jsonb LANGUAGE sql AS $$
  SELECT coalesce(jsonb_agg(e - 'cursor' ORDER BY i), '[]')
  FROM jsonb_array_elements(r) WITH ORDINALITY AS x(e, i)
$$;

CREATE FUNCTION pg_temp.gq_cursor_ok(r jsonb) RETURNS boolean LANGUAGE sql AS $$
  SELECT NOT jsonb_path_exists(r, '$[*] ? (!exists(@.cursor) || @.cursor == null)')
$$;

-- Critério de plano genérico: sem Seq Scan nas tabelas grandes e buffers <= limite.
CREATE FUNCTION pg_temp.gq_plan_check(label text, p jsonb, max_buffers integer) RETURNS text[]
LANGUAGE plpgsql AS $$
DECLARE seq text[] := pg_temp.gq_seqscans(p); b bigint := pg_temp.gq_buffers(p); d text[] := '{}';
BEGIN
  IF cardinality(seq) > 0 THEN
    d := d || format('%s: Seq Scan em %s (a tabela inteira é lida)', label, array_to_string(seq, ', '));
  END IF;
  IF b > max_buffers THEN
    d := d || format('%s: %s buffers lidos (limite: %s)', label, b, max_buffers);
  END IF;
  IF cardinality(d) > 0 THEN
    d := d || (E'  plano:\n' || pg_temp.gq_plan_summary(p));
  END IF;
  RETURN d;
END $$;

-- 1. orders_page — correção -----------------------------------------------------
DO $$
DECLARE
  d text[] := '{}'; got jsonb; acc jsonb := '[]'; c text; c2 text; n bigint; i integer;
BEGIN
  BEGIN
    got := pg_temp.gq_rows2('orders_page', NULL::text, 50);
    d := d || pg_temp.gq_cmp(pg_temp.gq_strip_cursor(got), pg_temp.ref_orders(0, 50), 'primeira página (50)');
    IF NOT pg_temp.gq_cursor_ok(got) THEN
      d := d || 'toda linha precisa da coluna "cursor" preenchida'::text;
    END IF;
    d := d || pg_temp.gq_cmp(pg_temp.gq_strip_cursor(pg_temp.gq_rows2('orders_page', NULL::text, 1)),
                             pg_temp.ref_orders(0, 1), 'primeira página (1 linha)');

    -- 25 páginas de 7, seguindo o cursor da última linha de cada página.
    c := NULL;
    FOR i IN 1 .. 25 LOOP
      got := pg_temp.gq_rows2('orders_page', c, 7);
      EXIT WHEN jsonb_array_length(got) = 0;
      acc := acc || pg_temp.gq_strip_cursor(got);
      c := got -> -1 ->> 'cursor';
    END LOOP;
    d := d || pg_temp.gq_cmp(acc, pg_temp.ref_orders(0, 175), '25 páginas de 7 seguidas pelo cursor');

    -- Página 1001 (de 50): cursor da linha 50 000.
    c := pg_temp.gq_last_cursor(NULL, 50000);
    d := d || pg_temp.gq_cmp(pg_temp.gq_strip_cursor(pg_temp.gq_rows2('orders_page', c, 50)),
                             pg_temp.ref_orders(50000, 50), 'página 1001 (linhas 50 001 a 50 050)');

    -- Fim da lista: as 10 últimas linhas e, depois delas, uma página vazia.
    SELECT count(*) INTO n FROM orders;
    c := pg_temp.gq_last_cursor(NULL, (n - 10)::integer);
    got := pg_temp.gq_rows2('orders_page', c, 50);
    d := d || pg_temp.gq_cmp(pg_temp.gq_strip_cursor(got), pg_temp.ref_orders(n - 10, 50),
                             'última página (só 10 linhas)');
    c2 := got -> -1 ->> 'cursor';
    IF c2 IS NOT NULL THEN
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows2('orders_page', c2, 50), '[]', 'página depois da última');
    END IF;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'orders_page — páginas corretas (primeira, seguintes pelo cursor, página 1001, última)', d);
END $$;

-- 2. orders_page — estabilidade com escritas entre as páginas ------------------
-- Entre uma página e a próxima chegam pedidos novos (no topo da lista) e alguns
-- pedidos já exibidos são removidos. Quem segue o cursor deve ver cada pedido
-- que existia no início exatamente uma vez, na ordem. Tudo é desfeito no fim.
DO $$
DECLARE
  d text[] := '{}'; want bigint[]; seen bigint[] := '{}'; got jsonb; c text; i integer;
  max_id bigint; max_ts timestamptz; pos integer;
BEGIN
  BEGIN
    SELECT array_agg(id ORDER BY created_at DESC, id DESC) INTO want
    FROM (SELECT id, created_at FROM orders ORDER BY created_at DESC, id DESC LIMIT 600) t;
    c := NULL;
    FOR i IN 1 .. 12 LOOP
      got := pg_temp.gq_rows2('orders_page', c, 50);
      EXIT WHEN jsonb_array_length(got) = 0;
      seen := seen || ARRAY(SELECT (e ->> 'id')::bigint FROM jsonb_array_elements(got) WITH ORDINALITY x(e, o) ORDER BY o);
      c := got -> -1 ->> 'cursor';
      -- 5 pedidos novos (mais recentes que todos) ...
      SELECT max(id), max(created_at) INTO max_id, max_ts FROM orders;
      INSERT INTO orders (id, customer_id, warehouse_id, status, total_cents, created_at)
      SELECT max_id + g, 1, 1, 'pending', 1000, max_ts + g * interval '1 second' FROM generate_series(1, 5) g;
      -- ... e 2 pedidos desta página são removidos.
      DELETE FROM orders WHERE id IN ((got -> 2 ->> 'id')::bigint, (got -> 3 ->> 'id')::bigint);
    END LOOP;
    IF seen IS DISTINCT FROM want THEN
      pos := (SELECT min(j) FROM generate_series(1, greatest(cardinality(seen), cardinality(want))) j
              WHERE seen[j] IS DISTINCT FROM want[j]);
      d := d || format('12 páginas de 50: esperados %s pedidos distintos na ordem; vieram %s linhas, %s ids distintos. '
                       'Primeira divergência na posição %s (página %s): esperado id %s, veio id %s',
                       cardinality(want), cardinality(seen),
                       (SELECT count(DISTINCT x) FROM unnest(seen) x), pos, (pos - 1) / 50 + 1,
                       coalesce(want[pos]::text, '-'), coalesce(seen[pos]::text, '-'));
    END IF;
    RAISE EXCEPTION USING ERRCODE = 'GQROL';  -- desfaz inserções e remoções
  EXCEPTION
    WHEN SQLSTATE 'GQROL' THEN NULL;
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'orders_page — estável com inserções e remoções entre as páginas (sem duplicar nem pular pedidos)', d);
END $$;

-- 3. orders_page — plano ---------------------------------------------------------
DO $$
DECLARE d text[] := '{}'; p1 jsonb; p2 jsonb; b1 bigint; b2 bigint; c text;
BEGIN
  BEGIN
    p1 := pg_temp.gq_plan2('orders_page', NULL::text, 50);
    c := pg_temp.gq_last_cursor(NULL, 50000);
    p2 := pg_temp.gq_plan2('orders_page', c, 50);
    b1 := pg_temp.gq_buffers(p1); b2 := pg_temp.gq_buffers(p2);
    d := d || pg_temp.gq_plan_check('página 1', p1, 100);
    d := d || pg_temp.gq_plan_check('página 1001', p2, 100);
    IF b2 > 2 * b1 + 20 THEN
      d := d || format('a página 1001 custa %s buffers e a página 1, %s: o custo cresce com a profundidade', b2, b1);
      IF cardinality(pg_temp.gq_plan_check('página 1001', p2, 100)) = 0 THEN
        d := d || (E'  plano da página 1001:\n' || pg_temp.gq_plan_summary(p2));
      END IF;
    END IF;
    IF cardinality(d) = 0 THEN
      d := ARRAY[format('página 1: %s buffers; página 1001: %s buffers', b1, b2)];
      PERFORM pg_temp.gq_record(true, 'orders_page — plano: sem Seq Scan, até 100 buffers; página 1001 custa o mesmo que a página 1', d);
      RETURN;
    END IF;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(false, 'orders_page — plano: sem Seq Scan, até 100 buffers; página 1001 custa o mesmo que a página 1', d);
END $$;

-- 4. daily_sales — correção ------------------------------------------------------
DO $$
DECLARE d text[] := '{}'; day date;
BEGIN
  BEGIN
    FOR day IN
      SELECT x FROM (VALUES (date '2024-01-01'), (date '2025-12-30'), (date '2023-12-31'), (date '2026-01-15')) v(x)
      UNION ALL
      -- dias com pedidos exatamente à meia-noite, e a véspera de cada um
      SELECT dd - k FROM (SELECT DISTINCT date(created_at) AS dd FROM orders
                          WHERE created_at = date_trunc('day', created_at) ORDER BY 1 OFFSET 200 LIMIT 3) m,
                         (VALUES (0), (1)) kk(k)
    LOOP
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows1('daily_sales', day), pg_temp.ref_daily_sales(day), 'dia ' || day);
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'daily_sales — totais corretos (dias com pedidos à meia-noite, primeiro/último dia, dias sem pedidos)', d);
END $$;

-- 5. daily_sales — plano ---------------------------------------------------------
DO $$
DECLARE d text[] := '{}'; p jsonb;
BEGIN
  BEGIN
    p := pg_temp.gq_plan1('daily_sales', date '2025-03-27');
    d := pg_temp.gq_plan_check('dia 2025-03-27', p, 150);
    PERFORM pg_temp.gq_record(cardinality(d) = 0,
      'daily_sales — plano: sem Seq Scan em orders, no máximo 150 buffers',
      CASE WHEN cardinality(d) = 0 THEN ARRAY[format('%s buffers', pg_temp.gq_buffers(p))] ELSE d END);
    RETURN;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(false, 'daily_sales — plano: sem Seq Scan em orders, no máximo 150 buffers', d);
END $$;

-- 6. customer_by_email — correção ------------------------------------------------
DO $$
DECLARE d text[] := '{}'; e text;
BEGIN
  BEGIN
    FOR e IN
      SELECT v FROM customers c,
             LATERAL (VALUES (c.email), (lower(c.email)), (upper(c.email))) x(v)
      WHERE c.id IN (3, 5, 6, 77777)
      UNION ALL VALUES ('nao.existe@exemplo.com'), ('')
    LOOP
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows1('customer_by_email', e), pg_temp.ref_customer_by_email(e),
                               format('e-mail %L', e));
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'customer_by_email — encontra o cliente com qualquer combinação de maiúsculas/minúsculas', d);
END $$;

-- 7. customer_by_email — plano ---------------------------------------------------
DO $$
DECLARE d text[] := '{}'; p jsonb; e text;
BEGIN
  BEGIN
    SELECT upper(email) INTO e FROM customers WHERE id = 77777;
    p := pg_temp.gq_plan1('customer_by_email', e);
    d := pg_temp.gq_plan_check(format('e-mail %L', e), p, 20);
    PERFORM pg_temp.gq_record(cardinality(d) = 0,
      'customer_by_email — plano: sem Seq Scan em customers, no máximo 20 buffers',
      CASE WHEN cardinality(d) = 0 THEN ARRAY[format('%s buffers', pg_temp.gq_buffers(p))] ELSE d END);
    RETURN;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(false, 'customer_by_email — plano: sem Seq Scan em customers, no máximo 20 buffers', d);
END $$;

-- 8. products_search — correção --------------------------------------------------
DO $$
DECLARE d text[] := '{}'; r record;
BEGIN
  BEGIN
    FOR r IN SELECT * FROM (VALUES ('KIT_000', 20), ('KIT_04', 50), ('KIT', 30), ('ELE-00', 20),
                                   ('ELE-000', 1000), ('LIV-0', 5), ('BEL%9', 20), ('XYZ', 20),
                                   ('KIT\', 10)) v(prefix, lim)
    LOOP
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows2('products_search', r.prefix::text, r.lim),
                               pg_temp.ref_products_search(r.prefix, r.lim),
                               format('prefixo %L, limite %s', r.prefix, r.lim));
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'products_search — só SKUs que começam com o texto digitado (inclusive com _ e %), em ordem', d);
END $$;

-- 9. products_search — plano -----------------------------------------------------
DO $$
DECLARE d text[] := '{}'; p1 jsonb; p2 jsonb;
BEGIN
  BEGIN
    p1 := pg_temp.gq_plan2('products_search', 'ELE-00'::text, 20);
    p2 := pg_temp.gq_plan2('products_search', 'KIT_000'::text, 20);
    d := pg_temp.gq_plan_check('prefixo ''ELE-00''', p1, 60) || pg_temp.gq_plan_check('prefixo ''KIT_000''', p2, 60);
    PERFORM pg_temp.gq_record(cardinality(d) = 0,
      'products_search — plano: sem Seq Scan em products, no máximo 60 buffers',
      CASE WHEN cardinality(d) = 0
           THEN ARRAY[format('''ELE-00'': %s buffers; ''KIT_000'': %s buffers', pg_temp.gq_buffers(p1), pg_temp.gq_buffers(p2))]
           ELSE d END);
    RETURN;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(false, 'products_search — plano: sem Seq Scan em products, no máximo 60 buffers', d);
END $$;

-- 10. pending_queue — correção ---------------------------------------------------
DO $$
DECLARE d text[] := '{}'; r record;
BEGIN
  BEGIN
    FOR r IN SELECT * FROM (VALUES (1, 20), (4, 20), (6, 1), (2, 5000), (7, 20), (3, 0)) v(wh, lim) LOOP
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows2('pending_queue', r.wh, r.lim),
                               pg_temp.ref_pending_queue(r.wh, r.lim),
                               format('warehouse %s, limite %s', r.wh, r.lim));
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'pending_queue — pedidos pendentes mais antigos do centro de distribuição, na ordem', d);
END $$;

-- 11. pending_queue — plano ------------------------------------------------------
DO $$
DECLARE d text[] := '{}'; p jsonb;
BEGIN
  BEGIN
    p := pg_temp.gq_plan2('pending_queue', 4, 20);
    d := pg_temp.gq_plan_check('warehouse 4, limite 20', p, 60);
    PERFORM pg_temp.gq_record(cardinality(d) = 0,
      'pending_queue — plano: sem Seq Scan em orders, no máximo 60 buffers',
      CASE WHEN cardinality(d) = 0 THEN ARRAY[format('%s buffers', pg_temp.gq_buffers(p))] ELSE d END);
    RETURN;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(false, 'pending_queue — plano: sem Seq Scan em orders, no máximo 60 buffers', d);
END $$;

-- 12/13. inactive_customers — plano (antes) e correção -----------------------------
-- O plano é verificado primeiro (EXPLAIN sem ANALYZE): uma subconsulta
-- reexecutada a cada cliente levaria minutos, então essa execução é pulada.
CREATE TEMP TABLE gq_inactive_params (since timestamptz, per_row text[]);
INSERT INTO gq_inactive_params (since)
VALUES ('2025-11-01'), ('2025-06-15'), ('2024-03-01'), ('2021-06-01'), ('2026-02-01');

DO $$
DECLARE d text[] := '{}'; r record; p jsonb; s text[];
BEGIN
  BEGIN
    FOR r IN SELECT since FROM gq_inactive_params ORDER BY since LOOP
      p := pg_temp.gq_plan1('inactive_customers', r.since, false);
      s := pg_temp.gq_per_row_subplans(p);
      UPDATE gq_inactive_params SET per_row = s WHERE since = r.since;
      IF cardinality(s) > 0 THEN
        d := d || (format('desde %s: %s reexecutada para cada cliente (não é anti-join nem hash)',
                          r.since::date, array_to_string(s, ', '))
                   || E'\n  plano:\n' || pg_temp.gq_plan_summary(p));
      END IF;
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'inactive_customers — plano: nenhuma subconsulta reexecutada a cada linha', d);
END $$;

DO $$
DECLARE d text[] := '{}'; r record;
BEGIN
  BEGIN
    FOR r IN SELECT since, per_row FROM gq_inactive_params ORDER BY since LOOP
      IF cardinality(r.per_row) > 0 THEN
        d := d || format('desde %s: não executada (subconsulta por linha — veja o critério de plano)', r.since::date);
        CONTINUE;
      END IF;
      d := d || pg_temp.gq_cmp(pg_temp.gq_rows1('inactive_customers', r.since),
                               pg_temp.ref_inactive_customers(r.since), format('desde %s', r.since::date));
    END LOOP;
  EXCEPTION
    WHEN query_canceled THEN d := d || 'tempo esgotado (60 s)'::text;
    WHEN OTHERS THEN d := d || pg_temp.gq_err(SQLERRM);
  END;
  PERFORM pg_temp.gq_record(cardinality(d) = 0,
    'inactive_customers — lista correta (convidados com customer_id NULL, só compras pagas no período)', d);
END $$;
