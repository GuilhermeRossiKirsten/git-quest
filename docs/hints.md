# Dicas — desafio-20

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe a linha do tempo de `Consumer.Handle` passo a passo (cada comando SQL
e cada `Publish`). Para cada intervalo entre dois passos, pergunte:

- "Se o processo morrer **aqui** e a mensagem for entregue de novo, o que a
  segunda execução encontra no banco? O cliente é cobrado de novo? O evento sai?"
- "Se **outra cópia** da mesma mensagem estiver neste mesmo ponto, em outro
  consumidor, ao mesmo tempo, o que cada uma vê?"
- "Se outro pagamento **da mesma conta** estiver aqui ao mesmo tempo, qual
  saldo cada um leu e qual cada um grava?"

Faça o mesmo com `Relay.RunOnce`: o que acontece se ele morrer depois de
marcar e antes de publicar?

## Nível 2

Onde olhar:

- Quantas transações `Handle` abre? A checagem "já processei?", o débito e o
  registro em `processed_messages` estão na mesma? O que garante que dois
  consumidores não passem os dois pela checagem? (Olhe o schema de
  `processed_messages` em `001_init.sql` e o que a `002` acrescenta.)
- O saldo é lido num comando e gravado em outro com um valor calculado no Go.
  Em READ COMMITTED, o que impede outra transação de mudar o saldo entre os dois?
- Banco e broker são dois sistemas: não existe transação que abranja os dois.
  Há uma tabela no schema que existe justamente para isso — e um relay que já a lê.
- No relay, a ordem "marcar como publicado → publicar" é a que você quer?

Teste à mão com duas sessões `psql` (`BEGIN;` em ambas) para ver quem bloqueia quem.

## Nível 3

Abordagem (sem código pronto):

1. Uma única transação por mensagem que faz **tudo**: reivindica a mensagem,
   debita, grava o extrato e grava o evento na `outbox`. Ou tudo fica, ou nada.
2. Reivindique a mensagem com uma restrição do banco, não com um `SELECT`:
   uma `PRIMARY KEY (message_id)` em `processed_messages` (numa migração
   `002`) e `INSERT ... ON CONFLICT (message_id) DO NOTHING`; se
   `RowsAffected() == 0`, outra entrega já fez (ou está fazendo) o trabalho —
   o `INSERT` concorrente **espera** a outra transação terminar.
3. Débito atômico e condicional num só comando:
   `UPDATE accounts SET balance_cents = balance_cents - $1 WHERE id = $2 AND balance_cents >= $1`
   — zero linhas afetadas significa saldo insuficiente. (Alternativa:
   `SELECT ... FOR UPDATE` antes de decidir.)
4. O consumidor não publica mais nada: o evento vai para a `outbox`, no formato
   documentado, com um ID determinístico.
5. O relay publica **antes** de marcar `published_at`; se cair entre os dois, o
   evento sai de novo com o mesmo ID — e o downstream deduplica. Pare no
   primeiro erro do broker e deixe o resto para a próxima execução.

## Nível 4

Armadilhas comuns quando quase tudo passa:

- Duas transações em sequência (débito numa, outbox/`processed_messages` em
  outra) ainda quebram com uma queda entre elas.
- `UPDATE ... SET balance_cents = balance_cents - $1` sem a condição no `WHERE`,
  decidido por um `SELECT` anterior, evita o *lost update* mas deixa o saldo
  ficar negativo com consumidores concorrentes.
- `SELECT` "já processei?" dentro da transação, sem restrição única, não
  resolve: em READ COMMITTED as duas transações não enxergam o `INSERT` uma da outra.
- Usar `context.Background()` em vez do `ctx` recebido esconde as quedas
  simuladas dos testes — e, em produção, ignora cancelamentos.
