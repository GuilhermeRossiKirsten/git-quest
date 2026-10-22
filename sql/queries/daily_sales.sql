-- Relatório diário de vendas (fechamento do caixa).
--
-- Parâmetros
--   $1 date  o dia (dia civil no fuso da sessão, America/Sao_Paulo)
-- Colunas: status, orders, revenue_cents — uma linha por status, em ordem de status
SELECT status,
       count(*)         AS orders,
       sum(total_cents) AS revenue_cents
FROM orders
WHERE date(created_at) = $1
GROUP BY status
ORDER BY status
