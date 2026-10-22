-- Schema do banco da loja (desafio-23). Não altere: é do avaliador.
--
-- As tabelas pertencem ao papel "app" (o usuário da aplicação), que executa
-- sql/indexes.sql e as consultas de sql/queries/. Nenhum índice secundário é
-- criado aqui: os índices da aplicação vivem em sql/indexes.sql.

CREATE TABLE customers (
  id         bigint      PRIMARY KEY,
  -- Gravado como o cliente digitou (maiúsculas e minúsculas misturadas).
  -- O login compara e-mails sem diferenciar maiúsculas de minúsculas.
  email      text        NOT NULL,
  name       text        NOT NULL,
  created_at timestamptz NOT NULL
);

CREATE TABLE products (
  id          bigint  PRIMARY KEY,
  -- Código do produto: letras maiúsculas, dígitos, '-' e '_' (kits: KIT_nnnnn).
  sku         text    NOT NULL UNIQUE,
  name        text    NOT NULL,
  price_cents integer NOT NULL CHECK (price_cents > 0)
);

CREATE TABLE orders (
  id           bigint      PRIMARY KEY,
  -- NULL = compra como convidado (guest checkout), sem cadastro.
  customer_id  bigint      REFERENCES customers (id),
  warehouse_id smallint    NOT NULL,
  status       text        NOT NULL
               CHECK (status IN ('pending', 'paid', 'shipped', 'delivered', 'cancelled')),
  total_cents  bigint      NOT NULL CHECK (total_cents > 0),
  created_at   timestamptz NOT NULL
);
