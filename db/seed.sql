-- Gera o banco-modelo shop_template (desafio-23). Não altere: é do avaliador.
--
--   psql -U gitquest -d postgres -v seed_version=<hash> -f db/seed.sql
--
-- Dados 100% determinísticos (generate_series + hashint8, sem random()):
-- 100 mil clientes, 200 mil produtos e 600 mil pedidos entre 01/01/2024 e
-- 30/12/2025. Sem índices secundários; cada execução do avaliador copia este
-- modelo (CREATE DATABASE ... TEMPLATE) e aplica sql/indexes.sql na cópia.
\set ON_ERROR_STOP 1
SET client_min_messages = warning;

SELECT 'CREATE ROLE app LOGIN'
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'app') \gexec

SELECT 'ALTER DATABASE shop_template IS_TEMPLATE false'
WHERE EXISTS (SELECT 1 FROM pg_database WHERE datname = 'shop_template') \gexec
DROP DATABASE IF EXISTS shop_template WITH (FORCE);
CREATE DATABASE shop_template;
\c shop_template
SET client_min_messages = warning;
-- O "dia" do negócio é o dia civil de São Paulo (fuso padrão do servidor).
SET timezone = 'America/Sao_Paulo';

\i /challenge/db/schema.sql

-- h(i, salt): inteiro pseudoaleatório >= 0, determinístico.
CREATE FUNCTION pg_temp.h(bigint, integer) RETURNS integer
  LANGUAGE sql IMMUTABLE AS $$ SELECT hashint8($1 * 7919 + $2) & 2147483647 $$;

-- Clientes: cadastrados de 2022 a 2025, e-mails em caixa mista.
INSERT INTO customers (id, email, name, created_at)
SELECT i,
       CASE pg_temp.h(i, 3) % 4
         WHEN 0 THEN lower(fn || '.' || ln || i || '@' || dom)
         WHEN 1 THEN fn || '.' || ln || i || '@' || dom
         WHEN 2 THEN upper(fn) || '.' || ln || i || '@' || upper(dom)
         ELSE lower(fn) || '_' || lower(ln) || i || '@' || initcap(dom)
       END,
       fn || ' ' || ln,
       timestamptz '2022-01-01 08:00' + i * interval '1262 seconds'
FROM (
  SELECT i,
         (ARRAY['Ana','Bruno','Carla','Diego','Elisa','Felipe','Gabriela','Heitor','Isabela','João',
                'Karina','Lucas','Mariana','Nicolas','Olívia','Paulo','Quésia','Rafael','Sofia','Tiago'])
           [1 + pg_temp.h(i, 1) % 20] AS fn,
         (ARRAY['Silva','Souza','Oliveira','Santos','Pereira','Lima','Costa','Ferreira','Almeida','Ribeiro',
                'Carvalho','Gomes','Martins','Araújo','Barbosa','Rocha'])
           [1 + pg_temp.h(i, 2) % 16] AS ln,
         (ARRAY['gmail.com','hotmail.com','outlook.com','yahoo.com.br','uol.com.br','empresa.com.br'])
           [1 + pg_temp.h(i, 4) % 6] AS dom
  FROM generate_series(1, 100000) AS i
) s;

-- Produtos: SKU <CATEGORIA>-<7 dígitos>; os últimos 4000 são kits KIT_nnnnn.
INSERT INTO products (id, sku, name, price_cents)
SELECT j,
       CASE WHEN j <= 196000
            THEN cat || '-' || lpad(((j * 7919) % 1000003)::text, 7, '0')
            ELSE 'KIT_' || lpad((j - 196000)::text, 5, '0')
       END,
       CASE WHEN j <= 196000 THEN 'Produto ' || cat || ' nº ' || j ELSE 'Kit promocional nº ' || (j - 196000) END,
       490 + pg_temp.h(j, 11) % 99000
FROM (
  SELECT j,
         (ARRAY['AUT','BEB','BEL','BRI','CAM','CAS','CEL','DEC','ELE','ESP','FER','GAM',
                'INF','JAR','KIT','LIV','MOD','MOV','MUS','PAP','PET','SAU','TEC','VID'])
           [1 + j % 24] AS cat
  FROM generate_series(1, 200000) AS j
) s;

-- Pedidos: ~105 s entre pedidos, em ordem de id (como uma tabela real cresce).
-- 1 em cada 8 grupos de 20 pedidos chega em lote (importação de marketplace):
-- os 20 têm exatamente o mesmo created_at. 1 em cada 997 pedidos cai
-- exatamente à meia-noite (horário de São Paulo).
INSERT INTO orders (id, customer_id, warehouse_id, status, total_cents, created_at)
SELECT i,
       CASE WHEN pg_temp.h(i, 21) % 100 < 8 THEN NULL   -- compra como convidado
            ELSE 1 + pg_temp.h(i, 22) % greatest(1, least(100000,
                   floor(extract(epoch FROM ts - timestamptz '2022-01-01 08:00') / 1262)::int))
       END,
       1 + pg_temp.h(i, 23) % 6,
       CASE WHEN ts >= timestamptz '2025-12-10' THEN
              CASE WHEN r < 150 THEN 'pending' WHEN r < 400 THEN 'paid' WHEN r < 700 THEN 'shipped'
                   WHEN r < 950 THEN 'delivered' ELSE 'cancelled' END
            ELSE
              CASE WHEN r < 20 THEN 'pending' WHEN r < 25 THEN 'paid' WHEN r < 40 THEN 'shipped'
                   WHEN r < 160 THEN 'cancelled' ELSE 'delivered' END
       END,
       990 + pg_temp.h(i, 24) % 250000,
       ts
FROM (
  SELECT i, pg_temp.h(i, 25) % 1000 AS r,
         CASE WHEN i % 997 = 0 THEN date_trunc('day', t) ELSE t END AS ts
  FROM (
    SELECT i, timestamptz '2024-01-01 00:00' + k * interval '105 seconds'
                                             + (pg_temp.h(k, 26) % 90) * interval '1 second' AS t
    FROM (
      SELECT i, CASE WHEN (i / 20) % 8 = 0 THEN (i / 20) * 20 ELSE i END AS k
      FROM generate_series(1, 600000) AS i
    ) a
  ) b
) c;

ALTER TABLE customers OWNER TO app;
ALTER TABLE products OWNER TO app;
ALTER TABLE orders OWNER TO app;
-- CREATE INDEX exige o privilégio CREATE no schema. (O avaliador confere que
-- sql/indexes.sql não cria nada além de índices.)
GRANT CREATE ON SCHEMA public TO app;

-- Amostra = tabela inteira (300 x 2000 linhas >= 600 mil): estatísticas determinísticas.
SET default_statistics_target = 2000;
VACUUM (FREEZE, ANALYZE);

\c postgres
COMMENT ON DATABASE shop_template IS :'seed_version';
-- Modelo: ninguém se conecta a ele (CREATE DATABASE ... TEMPLATE exige isso).
ALTER DATABASE shop_template WITH IS_TEMPLATE true ALLOW_CONNECTIONS false;
