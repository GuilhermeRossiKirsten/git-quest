-- Busca de produtos por início do SKU (autocomplete do backoffice).
--
-- Parâmetros
--   $1 text     o início do SKU digitado (3 caracteres ou mais), ex.: 'ELE-00', 'KIT_01'
--   $2 integer  máximo de resultados
-- Colunas: sku, name, price_cents — SKUs que COMEÇAM com $1, em ordem de sku
--   (ordem binária, byte a byte, como COLLATE "C": SKUs são códigos ASCII)
SELECT sku, name, price_cents
FROM products
WHERE sku LIKE $1 || '%'
ORDER BY sku
LIMIT $2
