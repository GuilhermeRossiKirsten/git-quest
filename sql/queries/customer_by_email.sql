-- Login: encontra o cliente pelo e-mail, sem diferenciar maiúsculas de minúsculas.
--
-- Parâmetros
--   $1 text  e-mail digitado pelo usuário
-- Colunas: id, email, name (em ordem de id)
SELECT id, email, name
FROM customers
WHERE lower(email) = lower($1)
ORDER BY id
