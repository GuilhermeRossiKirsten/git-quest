# Objetivos de aprendizagem — desafio-23

Ao concluir este desafio você será capaz de:

1. Ler planos de `EXPLAIN (ANALYZE, BUFFERS)` — tipo de nó, linhas reais,
   linhas descartadas pelo filtro, buffers — e usar **buffers** como medida
   estável de custo.
2. Escrever predicados *sargable*: deixar a coluna sozinha num lado da
   comparação (intervalo meio-aberto em vez de `date(col) = d`) ou indexar
   exatamente a expressão usada (`lower(email)`), entendendo por que só
   expressões `IMMUTABLE` podem ser indexadas.
3. Projetar índices compostos e parciais na ordem certa — igualdades primeiro,
   depois a coluna do `ORDER BY` — para que filtro + ordenação + `LIMIT` leiam
   só as linhas devolvidas.
4. Usar `LIKE 'prefixo%'` com índice B-tree (collation `"C"` ou
   `text_pattern_ops`) e tratar o texto do usuário como dado, escapando os
   curingas `_`, `%` e o caractere de escape.
5. Substituir paginação por `OFFSET` por paginação por *keyset* com cursor
   `(created_at, id)`: custo constante por página e resultado estável quando a
   lista muda entre as requisições.
6. Corrigir a semântica de `NOT IN` com `NULL` (lógica de três valores) e da
   precedência `AND`/`OR`, preferindo `NOT EXISTS`, que o PostgreSQL executa
   como anti-join.
7. Tratar índices como custo: cada um ocupa espaço e encarece escritas, então
   cada índice deve servir a uma consulta concreta.

## Como os testes verificam cada objetivo

Cada execução de `./test.sh` copia o banco-modelo determinístico, aplica
`sql/indexes.sql` + `ANALYZE` (amostra completa) e executa cada consulta como o
papel `app`, com parâmetros via `EXECUTE ... USING` (planejados com os valores
reais, como um driver com *prepared statements*).

| Objetivo | Verificação (`tests/checks.sql`, `tests/run.sh`) |
|---|---|
| 1. Ler planos / buffers | Todo critério de plano usa `EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON)`: soma `Shared Hit Blocks` + `Shared Read Blocks` do nó raiz e procura nós `Seq Scan` em `orders`, `customers` e `products`; na falha, mostra o plano resumido nó a nó. |
| 2. Predicados *sargable* | `daily_sales`: resultado igual a `date(created_at) = $1` em 10 dias (com pedidos exatamente à meia-noite, a véspera de cada um, primeiro/último dia, dias sem pedidos) e plano sem `Seq Scan`, ≤ 150 buffers. `customer_by_email`: e-mails com a caixa original, minúscula e maiúscula, inexistente e vazio; plano ≤ 20 buffers. |
| 3. Índices compostos/parciais | `pending_queue`: 6 combinações (inclusive centro inexistente, limite 0 e limite maior que a fila) comparadas com a referência; plano para 20 pedidos ≤ 60 buffers. |
| 4. `LIKE` com índice e escape | `products_search`: 9 prefixos, inclusive `KIT_000`, `KIT_04` (o `_` literal), `BEL%9` e `KIT\`, comparados com `starts_with(sku, $1)` em ordem binária; plano ≤ 60 buffers para `ELE-00` e `KIT_000`. |
| 5. Keyset | `orders_page`: primeira página, 25 páginas de 7 seguindo o cursor (atravessando lotes com o mesmo `created_at`), página 1001, última página e página vazia depois dela; 12 páginas de 50 com 5 inserções e 2 remoções entre páginas devem listar os 600 pedidos iniciais exatamente uma vez, na ordem; página 1 ≤ 100 buffers e página 1001 ≤ 2 × página 1 + 20. |
| 6. `NOT IN`/`NULL`, `AND`/`OR` | `inactive_customers`: 5 datas (inclusive antes de todos os clientes e depois de todos os pedidos) comparadas com a referência com `NOT EXISTS`; antes, `EXPLAIN` (sem executar) reprova qualquer `SubPlan` não-*hashed* (subconsulta reexecutada por linha). |
| 7. Índices enxutos | No máximo 6 índices criados pelo participante e 64 MB somados (a referência usa 5 e ~36 MB); `sql/indexes.sql` não pode mudar colunas, restrições, dados, RLS, funções nem configurações por papel/banco (impressão digital antes/depois). |
| Regras | Toda execução confere se a consulta alterou configurações do planejador (`enable_*`, custos, `work_mem`, fuso…); cada arquivo é embutido como subconsulta, então precisa ser um único `SELECT`. |
