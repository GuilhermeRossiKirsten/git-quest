-- Backoffice › Pedidos: listagem paginada, mais recentes primeiro.
--
-- Parâmetros
--   $1 text    cursor opaco devolvido pela página anterior (NULL = primeira página)
--   $2 integer tamanho da página
-- Colunas: id, customer_id, status, total_cents, created_at, cursor
--   (o cliente manda o "cursor" da última linha para pedir a próxima página)
--
-- O cursor é só a posição na lista, no formato "pos:<n>" — simples e sem estado.
SELECT id, customer_id, status, total_cents, created_at,
       'pos:' || (coalesce(split_part($1, ':', 2)::integer, 0) + row_number() OVER ()) AS cursor
FROM (
  SELECT id, customer_id, status, total_cents, created_at
  FROM orders
  ORDER BY created_at DESC, id DESC
  OFFSET coalesce(split_part($1, ':', 2)::integer, 0)
  LIMIT $2
) page
