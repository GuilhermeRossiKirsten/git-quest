-- Índices do banco da loja, aplicados pelo usuário da aplicação ("app").
--
-- Histórico: criados ao longo do tempo, "um índice para cada coluna que
-- aparece num WHERE". O DBA reclama que mesmo assim o banco vive em 100% de CPU.
--
-- Este arquivo roda numa cópia limpa do banco (só tabelas, chaves primárias e
-- a UNIQUE de products.sku), depois o avaliador executa ANALYZE.

-- Listagem de pedidos por status no backoffice.
CREATE INDEX orders_status_idx ON orders (status);

-- Fila de separação: pedidos de cada centro de distribuição.
CREATE INDEX orders_warehouse_idx ON orders (warehouse_id);

-- Histórico de pedidos do cliente (e a chave estrangeira).
CREATE INDEX orders_customer_idx ON orders (customer_id);

-- Login por e-mail.
CREATE INDEX customers_email_idx ON customers (email);
