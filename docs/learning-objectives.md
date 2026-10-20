# Objetivos de aprendizagem — desafio-20

Ao concluir este desafio você será capaz de:

1. Explicar a entrega *at-least-once* e tornar um consumidor **idempotente**:
   registrar a mensagem como processada com uma chave protegida por restrição
   do banco (`PRIMARY KEY` + `INSERT ... ON CONFLICT DO NOTHING`) **na mesma
   transação** do efeito.
2. Reconhecer a **escrita dupla** (gravar no banco e publicar no broker em
   passos separados) e substituí-la pelo **transactional outbox**: o evento é
   gravado na transação do efeito e um relay o publica depois.
3. Implementar um relay *at-least-once*: marcar como publicado só depois de o
   broker aceitar e confiar na deduplicação por ID no downstream.
4. Evitar *check-then-act* e *lost update* sob READ COMMITTED: `UPDATE`
   atômico e condicional (`balance = balance - $1 ... AND balance >= $1`) ou
   `SELECT ... FOR UPDATE`, em vez de ler, calcular no Go e gravar.
5. Raciocinar sobre quedas em cada ponto de um fluxo distribuído e sobre o que
   a atomicidade de uma transação garante (e o que não garante).
6. Evoluir o schema com uma migração versionada sem tocar no schema base.

## Como os testes verificam cada objetivo

Todos os testes rodam contra um PostgreSQL 16 real (um banco novo por teste)
e conferem o resultado com `sim.Check`: um evento distinto por pagamento,
cobrança ⇔ `PaymentCompleted`, saldo = inicial − confirmados = inicial −
extrato, nada negativo e recusa só sem saldo.

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Consumidor idempotente | `TestReentrega_SemCobrancaDupla`: cada mensagem chega 1–3 vezes e ~1/4 das primeiras publicações falham; exige zero cobranças duplicadas. `TestConcorrencia_OitoConsumidores`: cópias da mesma mensagem chegam ao mesmo tempo a consumidores diferentes. |
| 2. Transactional outbox | `TestCrash_ConsumidorEntreEtapas`: o consumidor morre antes do 1º…11º passo (comando SQL ou `Publish`) ou antes do ack, e é reiniciado; nenhum pagamento pode ficar sem evento nem ser cobrado duas vezes. `TestReentrega_SemCobrancaDupla` também perde eventos se a publicação for feita fora da transação. |
| 3. Relay at-least-once | `TestRelay_Idempotente`: 250 eventos na outbox → 250 publicações, nada pendente, nova execução publica 0. `TestRelay_CrashNaoPerdeEventos`: o relay morre em 9 pontos diferentes e o broker falha de vez em quando; todos os eventos têm de chegar (repetições com o mesmo ID são aceitas). |
| 4. Sem check-then-act / lost update | `TestConcorrencia_OitoConsumidores`: 8 consumidores, 4 contas, 240 pagamentos que excedem o saldo, 1 ms de latência por comando; exige saldo = extrato, nenhum saldo negativo e recusas só sem saldo. |
| 5. Quedas e atomicidade | `TestCrash_ConsumidorEntreEtapas` e `TestRelay_CrashNaoPerdeEventos` (a queda fecha a conexão: o PostgreSQL desfaz a transação aberta). |
| 6. Migração | Cada teste aplica `migrations/*.sql` (inclusive a sua `002`) num banco novo; uma migração inválida derruba todos os testes com a mensagem do PostgreSQL. |
| Transversal | `TestBasico_CobraERecusa` (regressão sem falhas), `go test -race` e `go vet ./...`. |
