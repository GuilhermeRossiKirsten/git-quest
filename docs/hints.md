# Dicas — desafio-7

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Antes de mexer no código, observe: rode `./demo.sh` e `./demo.sh -fail 3`
e leia a primeira linha de cada `✘` do `./test.sh`.

- Quantas goroutines `Run` cria para um lote de 500 fotos? E quantas o
  parâmetro `workers` diz que deveriam trabalhar ao mesmo tempo?
- Duas goroutines fazendo `append` no mesmo slice: o que o `-race` acha disso?
  E, mesmo sem corrida, em que ordem os resultados entrariam no slice?
- Quando `Run` devolve o primeiro erro, quem ainda vai ler o canal `errc`?
  O que acontece com uma goroutine que envia num canal sem buffer que ninguém lê?
- `go vet` reclamou de uma linha. Por que `wg.Add` *dentro* da goroutine
  permite que `wg.Wait` retorne cedo demais?

## Nível 2

Onde olhar e o que pesquisar:

- Padrão **worker pool**: um número fixo de goroutines lendo índices de um
  canal de trabalhos (`for i := range jobs`). Quem envia fecha o canal quando
  acabam os itens — é isso que encerra o `range` de cada worker.
- Ordem sem mutex: se `results` já nasce com `len(inputs)` posições, cada
  worker escreve apenas em `results[i]`. Posições diferentes de um slice não
  disputam memória entre si.
- `sync.WaitGroup`: o `Add` precisa acontecer **antes** do `go`. No Go 1.25,
  `wg.Go(func() { ... })` faz as duas coisas na ordem certa.
- Cancelamento: `context.WithCancel(ctx)` cria um ctx filho que você mesmo pode
  cancelar no primeiro erro. `sync.Once` garante que só o primeiro erro conta.
- Para parar de despachar: o envio em `jobs` pode ficar num `select` junto com
  `<-ctx.Done()`. Veja go.dev/blog/pipelines, seção "Explicit cancellation".

## Nível 3

Uma abordagem completa, em passos (escreva o código você mesmo):

1. Valide `workers <= 0` e devolva `ErrInvalidWorkers` envolto com `%w`.
   Se o `ctx` já chegou cancelado, devolva `ctx.Err()` (envolto) sem chamar `fn`.
2. Crie `results := make([]R, len(inputs))` e trate `len(inputs) == 0` cedo.
3. Derive `ctx, cancel := context.WithCancel(ctx)` e use `defer cancel()`.
4. Inicie `min(workers, len(inputs))` workers com `wg.Go`. Cada worker, para
   cada índice recebido: se `ctx.Err() != nil`, pula (só esvazia o canal);
   senão chama `fn(ctx, inputs[i])`; em erro, dentro de um `sync.Once`, guarda
   `fmt.Errorf("item %d: %w", i, err)` e chama `cancel()`; em sucesso, grava
   em `results[i]`.
5. Na goroutine de `Run`, envie os índices com `select { case jobs <- i: case
   <-ctx.Done(): pare }`, depois `close(jobs)` e `wg.Wait()`.
6. Só então decida o retorno: erro guardado → `(nil, erro)`; ctx cancelado
   sem erro de `fn` → `(nil, ctx.Err())` envolto; senão `(results, nil)`.
   Como `wg.Wait()` já retornou, nenhuma goroutine sobra e ler `results` e o
   erro guardado é seguro (o `Wait` estabelece a relação *happens-before*).

## Nível 4

Armadilhas comuns de quem está quase lá:

- `fmt.Errorf("...: %v", err)` produz a mesma mensagem, mas quebra
  `errors.Is`. Use `%w`.
- Devolver o "último" erro em vez do primeiro: depois do `cancel()`, as outras
  chamadas devolvem `context.Canceled` — esses erros não podem sobrescrever
  o erro original.
- Retornar no primeiro erro **sem** `wg.Wait()`: os workers ainda em
  andamento viram goroutines órfãs (o teste de vazamento mostra o stack delas).
- Proteger `append` com um mutex resolve a corrida, mas não a ordem.
