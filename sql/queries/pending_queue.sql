-- Fila de separação: os pedidos pendentes mais antigos de um centro de distribuição.
--
-- Parâmetros
--   $1 integer  warehouse_id
--   $2 integer  quantos pedidos buscar
-- Colunas: id, customer_id, total_cents, created_at — do mais antigo para o mais novo
--   (empate no created_at: menor id primeiro)
SELECT id, customer_id, total_cents, created_at
FROM orders
WHERE status = 'pending'
  AND warehouse_id = $1
ORDER BY created_at, id
LIMIT $2
