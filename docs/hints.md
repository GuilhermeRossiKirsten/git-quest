# Dicas — desafio-23

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece medindo, não adivinhando: `./explain.sh --summary` e depois
`./explain.sh <consulta> ...` para cada uma. Em cada plano, pergunte:

- Qual nó lê mais **buffers**? Ele lê a tabela inteira (`Seq Scan`) ou um
  pedaço de um índice? Quantas linhas ele descarta ("Rows Removed by Filter")?
- Existe um índice nessa coluna? Se existe, por que o planejador não o usa?
  O que está escrito no `WHERE` é *exatamente* o que está no índice?
- Quem decide a ordem do resultado: um índice já ordenado ou um `Sort` de
  centenas de milhares de linhas para devolver 20?
- Na paginação: compare `./explain.sh orders_page pos:0 50` com
  `pos:50000 50`. O que muda? E se chegarem 5 pedidos novos entre duas
  páginas, para onde "anda" a posição 50?
- Na campanha: o que vale `5 NOT IN (1, 2, NULL)`? Rode no `psql`.

## Nível 2

Onde olhar (um tema por consulta):

- `daily_sales` e `customer_by_email`: uma função aplicada **à coluna**
  (`date(created_at)`, `lower(email)`) esconde a coluna do índice. Ou a
  coluna fica sozinha de um lado da comparação, ou o índice é sobre a
  mesma expressão. (`date()` de um `timestamptz` depende do fuso: o PostgreSQL
  não deixa indexá-la. Por quê?)
- `products_search`: `LIKE 'abc%'` só vira intervalo num B-tree cuja ordem é
  a dos bytes — collation `"C"` ou `text_pattern_ops`; a `UNIQUE` em `sku`
  usa `en_US.utf8`. E `_` e `%` digitados pelo usuário são curingas do `LIKE`.
  Depois, olhe o `ORDER BY ... LIMIT`: o índice que filtra também ordena?
- `pending_queue`: o índice certo tem a igualdade primeiro e a coluna do
  `ORDER BY` depois. Só ~2% dos pedidos estão pendentes — o índice precisa
  guardar os outros 98%?
- `orders_page`: `OFFSET n` lê e descarta n linhas, e "posição" é instável
  quando a lista muda. A alternativa é lembrar **onde a página parou** (a chave
  de ordenação da última linha) e continuar dali — com desempate pelo `id`.
- `inactive_customers`: há **dois** erros de semântica. Um é `NULL` dentro do
  `NOT IN` (compras como convidado). O outro está na mistura de `AND` e `OR`.
- `sql/indexes.sql`: algum desses índices antigos ajuda alguma consulta?
  Confira em `pg_stat_user_indexes` (coluna `idx_scan`) depois de rodar as consultas.

## Nível 3

Abordagem (sem código pronto):

1. `daily_sales`: troque o predicado por um intervalo meio-aberto
   `[início do dia, início do dia seguinte)` sobre `created_at` (cuidado com
   `BETWEEN`: ele inclui o pedido da meia-noite do dia seguinte) e indexe
   `created_at` — de preferência num índice que também sirva à paginação.
2. `customer_by_email`: mantenha `lower(email) = lower($1)` e crie um índice
   de expressão em `lower(email)`. (Trocar por `email = $1` quebra o login de
   quem digita com outra caixa.)
3. `products_search`: escape `\`, `%` e `_` do texto (`replace`) antes de
   concatenar o `%`; filtre e ordene com `COLLATE "C"` e crie o índice com a
   mesma collation, para que ele entregue filtro **e** ordem e o `LIMIT` pare cedo.
4. `pending_queue`: índice parcial `WHERE status = 'pending'` com
   `(warehouse_id, created_at, id)`.
5. `orders_page`: cursor = `created_at` e `id` da última linha (num texto que
   você saiba decompor); a próxima página é `(created_at, id) < (cursor)` com
   `ORDER BY created_at DESC, id DESC LIMIT $2`, servida por um índice em
   `(created_at, id)` lido de trás para frente.
6. `inactive_customers`: `NOT EXISTS (SELECT 1 FROM orders o WHERE o.customer_id = c.id AND ...)`
   com os status num `IN (...)` — vira um anti-join.
7. Remova os índices antigos que nenhuma consulta usa.

## Nível 4

Armadilhas comuns quando quase tudo passa:

- Keyset com `created_at < x OR (created_at = x AND id < y)` devolve as linhas
  certas, mas o planejador não usa esse `OR` como intervalo do índice: a
  página 1001 volta a ler milhares de buffers. A comparação de valores de
  linha `(created_at, id) < (x, y)` vira uma condição de índice.
- Keyset só com `created_at < x`, sem o `id`, pula os pedidos que chegaram no
  mesmo segundo (há lotes importados com o mesmo `created_at`).
- `text_pattern_ops` + `ORDER BY sku` (collation padrão) filtra pelo índice,
  mas ainda precisa ordenar — e para alguns prefixos o planejador prefere
  percorrer a `UNIQUE` inteira esperando achar 20 linhas.
- `NOT IN (... WHERE customer_id IS NOT NULL)` acerta o resultado, mas quando
  a subconsulta é grande demais para um hash ela é reexecutada para cada cliente.
- `set_config('enable_seqscan', 'off', ...)` numa consulta ou `ALTER ROLE ... SET`
  no arquivo de índices são detectados e reprovam: o objetivo é que o
  planejador **escolha** o índice por ser o melhor plano.
