-- Campanha de reativação: clientes cadastrados antes de $1 que NÃO fizeram
-- nenhuma compra paga (status paid, shipped ou delivered) desde $1.
--
-- Parâmetros
--   $1 timestamptz  início do período
-- Colunas: id, email (em ordem de id)
SELECT c.id, c.email
FROM customers c
WHERE c.created_at < $1
  AND c.id NOT IN (
        SELECT o.customer_id
        FROM orders o
        WHERE o.status = 'paid' OR o.status = 'shipped' OR o.status = 'delivered'
          AND o.created_at >= $1
      )
ORDER BY c.id
