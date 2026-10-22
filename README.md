# desafio-23 · A consulta que varria a tabela

> Trilha: database · Nível: intermediate · ~60 min · 200 pontos · PostgreSQL 16.4 (Docker Compose) · SQL

## Contexto

A loja virtual cresceu: são 600 mil pedidos, 100 mil clientes e 200 mil
produtos num PostgreSQL. Ninguém mudou o código da aplicação, mas desde a última
campanha o banco vive em 100% de CPU e o time de operações abriu uma lista de
chamados. O DBA respondeu com uma frase só: *"está tudo fazendo Seq Scan"*. O
backend jura que "tem índice em tudo" — e tem mesmo: um para cada coluna que
aparece num `WHERE`.

Você herdou os arquivos SQL da aplicação: os índices (`sql/indexes.sql`) e as
seis consultas que os endpoints executam (`sql/queries/*.sql`, com parâmetros
`$1`, `$2`, como um driver com *prepared statements*).

## Problema

Rode `./start.sh`: ele gera o banco, aplica os índices e mede cada consulta
(tempo, buffers lidos e tabelas lidas por inteiro). Os chamados:

- **Backoffice › Pedidos** (`orders_page.sql`): a primeira página já é lenta e
  cada página seguinte fica mais lenta que a anterior. Pior: quando chegam
  pedidos enquanto o operador navega, o mesmo pedido aparece em duas páginas
  seguidas, e quando um pedido é removido outro some da listagem.
- **Fechamento do caixa** (`daily_sales.sql`): o relatório de **um** dia lê a
  tabela de pedidos inteira.
- **Login** (`customer_by_email.sql`): lento em horário de pico, apesar do
  índice em `customers.email`.
- **Autocomplete de SKU** (`products_search.sql`): digitar `KIT_000` mostra
  produtos `KIT-0000111`, que não começam com o texto digitado; digitar `BEL%9`
  mostra metade da categoria BEL. E é lento, apesar da `UNIQUE` em `sku`.
- **Fila de separação** (`pending_queue.sql`): buscar os 20 pedidos pendentes
  mais antigos de um centro de distribuição varre milhares de páginas.
- **Campanha de reativação** (`inactive_customers.sql`): o marketing pediu os
  clientes que não compram desde uma data e a lista veio **vazia** — e, para
  algumas datas, a consulta simplesmente não termina.

Faça cada consulta devolver **exatamente** o resultado descrito no comentário do
seu arquivo, com um plano que leia só o necessário, usando um conjunto de
índices enxuto. O contrato de cada consulta (parâmetros, tipos, colunas e
ordem) está no comentário do arquivo: mantenha-o.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Ler `EXPLAIN (ANALYZE, BUFFERS)` e medir custo em buffers (páginas de 8 kB), não só em milissegundos.
- Escrever predicados *sargable* ou indexar exatamente a expressão usada (índice de expressão).
- Projetar índices compostos e parciais na ordem certa para filtro + `ORDER BY` + `LIMIT`.
- Usar `LIKE 'prefixo%'` com índice (collation `"C"` ou `text_pattern_ops`) e escapar os curingas do texto digitado.
- Trocar paginação por `OFFSET` por paginação por *keyset* (cursor `(created_at, id)`), estável sob escritas.
- Corrigir a semântica de `NOT IN` com `NULL` e da precedência `AND`/`OR`, preferindo `NOT EXISTS` (anti-join).
- Manter poucos índices, cada um justificado por uma consulta.

## Conhecimentos necessários

- SQL: `SELECT`, `JOIN`, `GROUP BY`, `ORDER BY`, `LIMIT`, subconsultas, `EXISTS`/`IN`, lógica de três valores com `NULL`.
- PostgreSQL: `CREATE INDEX`, tipos `timestamptz`/`date`, `psql`.
- Noção de B-tree (índice ordenado). O desafio ensina a ler os planos.

## Ambiente

- Docker >= 20.10 com **Docker Compose v2 >= 2.20** (`docker compose version`).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- `docker-compose.yml` sobe só o `postgres:16.4-alpine3.20` (projeto
  `gitquest-desafio-23`), com volume `gitquest-desafio-23-pgdata` e rede
  **interna**; a porta **não** é exposta no host. O diretório do desafio é
  montado somente leitura em `/challenge` e tudo roda com o `psql` do container.
- Dados determinísticos (`db/seed.sql`, sem `random()`), gerados uma vez no
  banco-modelo `shop_template` (~20 s). Cada `./start.sh` recria o banco `shop`
  (para você explorar) e cada `./test.sh` cria uma cópia limpa `shop_test`;
  as duas recebem `sql/indexes.sql` (executado como o papel `app`, dono das
  tabelas) e um `ANALYZE` com amostra completa (`default_statistics_target = 2000`),
  para estatísticas e planos idênticos em toda execução.
- Configuração do servidor: fuso `America/Sao_Paulo` (o "dia" do relatório é o
  dia civil de São Paulo), collation do banco `en_US.utf8`, sem consultas
  paralelas e sem JIT (planos e buffers determinísticos), `fsync=off` (banco de
  laboratório — nunca em produção).

## Como começar

```bash
./start.sh            # idempotente: PostgreSQL, dados (1ª vez), banco "shop" com seus índices e medições
./start.sh --down     # ao terminar: derruba o PostgreSQL e apaga o volume
```

Ferramentas de diagnóstico:

- `./explain.sh --summary` — tempo, buffers e tabelas varridas de cada consulta.
- `./explain.sh <consulta> <parâmetros...>` — o plano real com
  `EXPLAIN (ANALYZE, BUFFERS)` (`PREPARE` + `EXECUTE`, como o driver faria). Ex.:
  `./explain.sh orders_page NULL 50`, `./explain.sh orders_page pos:50000 50`,
  `./explain.sh daily_sales 2025-03-27`, `./explain.sh pending_queue 4 20`.
- `./explain.sh --rows <consulta> <parâmetros...>` — só o resultado.
- `psql` no banco: `docker compose -p gitquest-desafio-23 exec postgres psql -U gitquest -d shop`
  — experimente `\d orders`, `\di+`, `SELECT * FROM pg_stat_user_indexes;`,
  `SELECT * FROM pg_stats WHERE tablename = 'orders';`, e crie índices à mão
  para testar (depois passe-os para `sql/indexes.sql` e rode `./start.sh`).
- `build/test.log`: a saída completa do avaliador.

## Arquivos que você pode modificar

- `sql/indexes.sql` — os índices. Ele roda numa cópia limpa do banco (só
  tabelas, chaves primárias e a `UNIQUE` de `products.sku`): pode criar,
  renomear e remover índices à vontade, mas **só índices** — nada de
  `ALTER TABLE`, funções, `SET`/`ALTER ROLE`, nem mexer nos dados.
- `sql/queries/*.sql` — as seis consultas. Cada arquivo é **um único `SELECT`**
  com os parâmetros `$1`, `$2` do contrato descrito no comentário do arquivo
  (tipos, colunas, ordem). O formato do `cursor` de `orders_page` é livre: é
  opaco para o cliente, que só devolve o valor da última linha.

Não altere `db/`, `tests/`, `env/`, `explain.sh`, `start.sh`, `test.sh`,
`docker-compose.yml` nem `challenge.yaml`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador (`tests/run.sh`, dentro do container) copia o banco-modelo, aplica
`sql/indexes.sql` e `ANALYZE`, e então, para cada consulta:

1. **Correção** — executa com vários parâmetros, inclusive bordas (meia-noite,
   dias sem pedidos, maiúsculas, `_` e `%` digitados, página 1001, última
   página, escritas entre páginas), e compara com uma consulta de referência
   simples escrita no próprio teste.
2. **Plano** — `EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON)`: nada de `Seq Scan`
   nas tabelas grandes e no máximo N buffers; para a paginação, a página 1001
   deve custar o mesmo que a página 1. Quando falha, mostra o plano resumido.

Também confere o orçamento de índices e as regras do jogo (consultas que mexem
no planejador com `set_config`/`SET`, ou um `sql/indexes.sql` que muda algo além
de índices, reprovam). Leva ~15 s (mais ~20 s na primeira vez, para gerar os dados).

## Critérios de conclusão

- [ ] `sql/indexes.sql` aplicado sem erros e criando **só** índices
- [ ] índices enxutos: no máximo **6** índices criados por você e **64 MB** somados
- [ ] `orders_page`: páginas corretas (primeira, seguintes pelo cursor, página 1001, última)
- [ ] `orders_page`: estável com inserções e remoções entre as páginas (sem duplicar nem pular pedidos)
- [ ] `orders_page`: sem `Seq Scan`; página 1 com no máximo 100 buffers e página 1001 custando o mesmo que a página 1
- [ ] `daily_sales`: totais corretos (pedidos à meia-noite, primeiro/último dia, dias sem pedidos); plano sem `Seq Scan`, ≤ 150 buffers
- [ ] `customer_by_email`: encontra o cliente com qualquer caixa de letras; plano sem `Seq Scan`, ≤ 20 buffers
- [ ] `products_search`: só SKUs que começam com o texto digitado (inclusive `_` e `%`), em ordem; plano sem `Seq Scan`, ≤ 60 buffers
- [ ] `pending_queue`: pendentes mais antigos do centro de distribuição, na ordem; plano sem `Seq Scan`, ≤ 60 buffers
- [ ] `inactive_customers`: lista correta para várias datas; nenhuma subconsulta reexecutada a cada linha
- [ ] nenhuma consulta altera configurações do planejador (conferido depois de cada execução; a violação reprova o critério da consulta)

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- Markus Winand, *Use The Index, Luke!* (SQL Performance Explained, versão web): https://use-the-index-luke.com/ — capítulos "The Where Clause" (funções na coluna, índices de expressão, `LIKE`, datas), "Sorting and Grouping" (índice que entrega o `ORDER BY`, *pipelined top-N*) e "Partial Results" → "Paging Through Results" (*seek method* / keyset): https://use-the-index-luke.com/sql/partial-results/fetch-next-page
- Documentação do PostgreSQL 16 — Using EXPLAIN: https://www.postgresql.org/docs/16/using-explain.html
- Documentação do PostgreSQL 16 — Indexes (cap. 11: índices multicoluna, `ORDER BY`, expressões, parciais, classes de operadores e collations): https://www.postgresql.org/docs/16/indexes.html
- Documentação do PostgreSQL 16 — Row Constructor Comparison (`(a, b) < (x, y)`): https://www.postgresql.org/docs/16/functions-comparisons.html#ROW-WISE-COMPARISON
- Documentação do PostgreSQL 16 — Pattern Matching (`LIKE` e o caractere de escape): https://www.postgresql.org/docs/16/functions-matching.html#FUNCTIONS-LIKE
- Documentação do PostgreSQL 16 — Subquery Expressions (`NOT IN` com `NULL`, `EXISTS`): https://www.postgresql.org/docs/16/functions-subquery.html
- *Patterns for API Design*, Olaf Zimmermann et al. — cap. 7, padrão *Pagination* (variantes *offset-based* e *cursor-based*: o cursor não depende da posição absoluta e não muda quando entram elementos novos).
- *Data Structures the Fun Way*, Jeremy Kubica — cap. 12, "B-trees" (por que um índice B-tree lê poucas páginas para achar uma chave ou um intervalo).
- *Designing Data-Intensive Applications*, Martin Kleppmann — cap. 3, "Storage and Retrieval" (B-trees, índices secundários e o custo de escrita de cada índice). *(citado de memória)*

## Próximo desafio

**desafio-24 — O cache que mentia**: com as consultas rápidas, o time colocou
um Redis na frente do banco… e descobriu cache-aside, invalidação, *stampede*
e TTL com *jitter*.
