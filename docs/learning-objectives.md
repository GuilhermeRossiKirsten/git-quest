# Objetivos de aprendizagem — desafio-7

Ao concluir este desafio você será capaz de:

1. **Limitar a concorrência** com um worker pool (N goroutines lendo de um
   canal), em vez de criar uma goroutine por item.
2. **Preservar a ordem** dos resultados sem mutex, gravando cada resultado na
   posição do item correspondente.
3. **Encontrar e eliminar data races** com `go test -race` e entender por que
   `append` concorrente num slice compartilhado é inseguro.
4. **Usar `sync.WaitGroup` corretamente** (`Add` antes de `go`, ou `wg.Go`) e
   reconhecer o alerta do `go vet` sobre isso.
5. **Propagar o primeiro erro** cancelando o trabalho restante com
   `context.WithCancel`, `sync.Once` e erros envoltos com `%w` (`errors.Is`).
6. **Respeitar o cancelamento e o deadline do chamador**, parando de despachar
   trabalho novo.
7. **Evitar vazamento de goroutines**: toda goroutine criada termina antes de a
   função retornar; saber diagnosticar uma goroutine presa num envio de canal.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Limitar a concorrência | `TestConcurrencyLimit` mede com `sync/atomic` o pico de chamadas simultâneas de `fn`: deve ser exatamente `workers` sob carga (4 vagas para 40 itens de 20 ms), 1 com `workers=1` e no máximo `len(inputs)` quando há mais workers que itens. |
| 2. Preservar a ordem | `TestPreservesOrder` faz os primeiros itens demorarem mais e confere `results[i]` para 200 itens; `TestStress` confere 2000 itens. |
| 3. Sem data races | Toda a suíte roda com `-race`; qualquer relatório `WARNING: DATA RACE` reprova o critério "sem data races" e o teste em que ocorreu. |
| 4. `WaitGroup` correto | `go vet ./...` (analisador `waitgroup` do Go 1.25) e, em comportamento, `TestStress`/`TestPreservesOrder` detectam resultados perdidos quando `Wait` retorna antes da hora. |
| 5. Primeiro erro | `TestFirstError`: `errors.Is(err, errBoom)`, mensagem com `item 3`, `results == nil`, chamadas em andamento veem o ctx cancelado, retorno em < 500 ms e nenhuma chamada nova depois do erro; com todos os itens falhando, só um erro é devolvido. |
| 6. Cancelamento do chamador | `TestCallerCancellation`: cancelamento no meio do lote (com `fn` que observa o ctx e com `fn` que o ignora) retorna `context.Canceled` em até 200 ms e para de despachar; deadline retorna `context.DeadlineExceeded`; ctx já cancelado não chama `fn`. |
| 7. Sem vazamento | `TestNoGoroutineLeak` tira um instantâneo das goroutines (`runtime.Stack`) antes de `Run` e exige que todas as novas terminem em até 1 s após o retorno — em sucesso, erro com chamadas em andamento, vários erros e cancelamento. Em falha, mostra o stack da goroutine presa. |
| Casos de borda | `TestEdgeCases`: `workers` 0 e -1 → `ErrInvalidWorkers` sem chamar `fn`; entradas vazias ou `nil` → resultado vazio, erro `nil`. |
