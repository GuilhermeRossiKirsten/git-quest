# desafio-20 · O consumidor que processava duas vezes

> Trilha: database · Nível: advanced · ~90 min · 300 pontos · Go 1.25.3 + PostgreSQL 16.4 (Docker Compose)

## Contexto

O serviço `payments` consome a fila `payments.requested`: para cada pedido de
pagamento ele debita o saldo do cliente, grava a linha do extrato e avisa a
empresa publicando `PaymentCompleted` (ou `PaymentDeclined`, quando não há
saldo) no tópico `payments.events`. O time de pedidos libera a entrega quando
recebe a confirmação.

Na Black Friday, com oito réplicas do consumidor rodando e o broker
engasgando, o suporte abriu três chamados no mesmo dia: clientes **cobrados
duas vezes** pela mesma compra, pedidos **pagos que nunca foram liberados** e
uma conta cujo saldo não batia com o próprio extrato. A fila, segundo o time de
infraestrutura, "funciona como documentado": entrega cada mensagem **pelo menos
uma vez**.

## Problema

Rode `./start.sh`: ele sobe o PostgreSQL e executa `cmd/simulate`, um "dia de
produção" com 300 pedidos, quatro consumidores, reentregas, um broker que às
vezes recusa publicações e algumas quedas de processo. No fim, a simulação
confere o banco e os eventos. Sintomas observáveis:

- O mesmo `message_id` aparece duas vezes em `charges` (o cliente pagou duas vezes).
- Um pagamento recebe `PaymentDeclined` **e** `PaymentCompleted` com o mesmo ID de evento.
- Pagamentos confirmados no banco sem evento nenhum no broker (pedido nunca liberado).
- Saldo final que não bate nem com os eventos publicados nem com o extrato.
- Eventos parados na tabela `outbox` nunca chegam ao broker depois que o relay cai.

Investigue o consumidor e o relay (`internal/payments/`) e faça o serviço
cumprir o contrato abaixo **em qualquer combinação** de reentregas, quedas e
concorrência — corrigindo o código existente, não reescrevendo o serviço.

**Contrato**

1. Cada pedido de pagamento (`MessageID`) cobra o cliente **no máximo uma vez**,
   não importa quantas vezes nem em quantos consumidores ao mesmo tempo ele
   chegue. Cobrado ⇔ uma linha em `charges`.
2. Cada pagamento termina com **exatamente um** evento, depois que o downstream
   deduplica por `Event.ID`: `PaymentCompleted` (cobrado) ou `PaymentDeclined`
   (saldo insuficiente, nada cobrado). Republicar o **mesmo** evento (mesmo ID e
   conteúdo) é aceitável; dois eventos diferentes para o mesmo pagamento, ou
   nenhum, não.
3. Saldo final = saldo inicial − pagamentos confirmados = saldo inicial − extrato.
   Saldo nunca fica negativo, e recusa só acontece sem saldo.
4. Se o processo (consumidor ou relay) morrer em **qualquer** ponto — antes ou
   depois de qualquer comando SQL ou publicação, ou depois de terminar mas antes
   do ack — e for reiniciado, as regras acima continuam valendo.
5. `Relay.RunOnce` publica os eventos pendentes da `outbox`; depois que ele
   devolve `0, nil`, não há nada pendente e uma nova execução não republica nada.
   Eventos gravados na outbox seguem o formato documentado em
   `migrations/001_init.sql`.

Como os testes simulam uma queda: cada comando SQL e cada `Publish` é um
"passo"; o processo morre imediatamente antes do passo *k* — a conexão é
fechada (o PostgreSQL desfaz a transação aberta) e tudo o que ele tentar depois
falha. Por isso, use sempre o `ctx` recebido em `Handle`/`RunOnce`.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Reconhecer a entrega *at-least-once* e tornar um consumidor idempotente com uma chave de deduplicação protegida por restrição do banco, gravada na mesma transação do efeito.
- Identificar a escrita dupla (banco + broker) e substituí-la pelo *transactional outbox* com um relay *at-least-once*.
- Evitar *check-then-act* e *lost update* sob READ COMMITTED com `INSERT ... ON CONFLICT DO NOTHING`, `UPDATE` atômico e condicional ou `SELECT ... FOR UPDATE`.
- Raciocinar sobre quedas em cada ponto de um fluxo e sobre o que a atomicidade de uma transação garante.
- Evoluir o schema com uma migração versionada.

## Conhecimentos necessários

- Go: `context`, erros, `database/sql` ou pgx (o projeto usa `github.com/jackc/pgx/v5`: `pool.Begin`, `pgx.BeginFunc`, `tx.Exec`, `CommandTag.RowsAffected()`).
- SQL/PostgreSQL: transações, `PRIMARY KEY`/`UNIQUE`, níveis de isolamento (em especial READ COMMITTED, o padrão).
- Mensageria: fila, ack, reentrega (desafio-9 cobre o Docker que usamos aqui).

## Ambiente

- Docker >= 20.10 com **Docker Compose v2 >= 2.20** (`docker compose version`).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- `docker-compose.yml` sobe só o `postgres:16.4-alpine3.20`, com healthcheck,
  volume nomeado `gitquest-desafio-20-pgdata` e a rede **interna**
  `gitquest-desafio-20-net`. A porta do PostgreSQL **não** é exposta no host.
- O código Go roda em containers descartáveis da imagem de toolchain
  (`Dockerfile`, `golang:1.25.3-bookworm`), como usuário não-root, ligados só à
  rede interna (sem internet). O driver pgx v5.7.6 e suas dependências são
  baixados **no build da imagem** (é o único momento com rede); nos testes,
  `GOPROXY=off` e o `go.sum` do repositório conferem os módulos. Optamos por
  isso em vez de versionar `vendor/` (≈ 8 MB / 277 arquivos, quase tudo tabelas
  Unicode de `golang.org/x/text`).
- Cache de build no volume `gitquest-desafio-20-gocache`.
- Banco de laboratório: `fsync=off` e `synchronous_commit=off` para os testes
  serem rápidos. Nunca faça isso em produção.

## Como começar

```bash
./start.sh            # idempotente: imagem, PostgreSQL, compilação e simulação
./start.sh --down     # ao terminar: derruba o PostgreSQL e apaga o volume
```

Ferramentas de diagnóstico:

- A saída da simulação e o banco que ela deixa para inspeção:
  `docker compose -p gitquest-desafio-20 exec postgres psql -U gitquest -d payments_sim`
  — experimente `SELECT message_id, count(*) FROM charges GROUP BY 1 HAVING count(*) > 1;`
  e `SELECT * FROM outbox WHERE published_at IS NULL;`.
- Duas sessões `psql` lado a lado (`BEGIN; ...`) para reproduzir à mão duas
  transações concorrentes e ver quem espera por quem (`SELECT * FROM pg_locks`).
- `./test.sh` e o log completo em `build/test.log`: cada teste lista as regras
  violadas (cobranças duplicadas, eventos perdidos, saldos que não batem).
- `docker compose -p gitquest-desafio-20 logs postgres`.

## Arquivos que você pode modificar

- `internal/payments/` — o consumidor (`consumer.go`) e o relay (`relay.go`).
  Mantenha a API pública (`NewConsumer`, `Consumer.Handle`, `NewRelay`,
  `Relay.RunOnce`); pode criar arquivos auxiliares no pacote.
- `migrations/002_payments_consumer.sql` — a migração do time de pagamentos.
  Ela é aplicada depois da `001`, uma vez por banco; cada teste cria um banco
  novo, então você pode reescrevê-la à vontade (para a simulação, rode
  `./start.sh` de novo — ela também recria o banco `payments_sim`).

Não altere `migrations/001_init.sql` (schema compartilhado), `internal/events`,
`internal/sim`, `internal/migrate`, `cmd/`, `tests/`, `env/`, `test.sh`,
`docker-compose.yml`, `Dockerfile`, `go.mod`/`go.sum`, `challenge.yaml` nem o
`Makefile`: o avaliador usa as versões originais. Não há dependências novas
disponíveis (os testes rodam sem internet).

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Sobe o PostgreSQL se preciso (`docker compose -p gitquest-desafio-20 up -d --wait`),
roda `go vet ./...` e `go test -race -count=1 ./tests/...` num container na
rede do compose — cada teste cria e apaga o próprio banco — e imprime ✔/✘ por
critério, com as regras violadas. Leva cerca de 40 s.

## Critérios de conclusão

- [ ] sem falhas: cobra quem tem saldo, recusa quem não tem
- [ ] reentregas (cada mensagem 1–3x) e broker instável: nenhuma cobrança dupla, exatamente um evento por pagamento
- [ ] queda do consumidor antes de cada um dos primeiros passos e antes do ack, com reinício: nada perdido nem duplicado
- [ ] 8 consumidores concorrentes na mesma fila: sem cobrança dupla, sem *lost update*, sem saldo negativo
- [ ] relay publica cada evento uma vez sem falhas e não republica o que já enviou
- [ ] quedas do relay e falhas do broker: nenhum evento da outbox perdido
- [ ] `go test -race` sem data races e `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Strategic Monoliths and Microservices*, Vaughn Vernon e Tomasz Jaskuła — cap. 9, "Message- and Event-Driven Architectures" (persistir eventos primeiro e enfileirar depois, entrega *at-least-once*).
- *Designing Data-Intensive Applications*, Martin Kleppmann — cap. 7 (Transactions: *lost updates*, *write skew*) e cap. 11 (Stream Processing: idempotência e *exactly-once* efetivo). *(citado de memória)*
- *Sistemas Distribuídos: Princípios e Paradigmas*, 2ª ed., Andrew S. Tanenbaum e Maarten van Steen — cap. 1 (transações e propriedades ACID), cap. 4, seção 4.3 (Comunicação orientada a mensagem) e cap. 8 (Tolerância a falha: semânticas *pelo menos uma vez* / *no máximo uma vez* e operações idempotentes).
- Chris Richardson, microservices.io — "Pattern: Transactional outbox": https://microservices.io/patterns/data/transactional-outbox.html
- Chris Richardson, microservices.io — "Pattern: Idempotent Consumer": https://microservices.io/patterns/communication-style/idempotent-consumer.html
- Documentação do PostgreSQL 16 — Transaction Isolation: https://www.postgresql.org/docs/16/transaction-iso.html
- Documentação do PostgreSQL 16 — `INSERT ... ON CONFLICT`: https://www.postgresql.org/docs/16/sql-insert.html#SQL-ON-CONFLICT
- Documentação do PostgreSQL 16 — `SELECT ... FOR UPDATE` (The Locking Clause): https://www.postgresql.org/docs/16/sql-select.html#SQL-FOR-UPDATE-SHARE
- pgx v5: https://pkg.go.dev/github.com/jackc/pgx/v5

## Próximo desafio

Este é, por enquanto, o último desafio da trilha **database**. Para ir além:
troque o relay por *polling* concorrente com `FOR UPDATE SKIP LOCKED` ou leia
sobre *change data capture* (Debezium) como alternativa ao relay.
