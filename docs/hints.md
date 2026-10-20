# Dicas — desafio-8

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

`Get` recebe um `ctx` — mas quem, dentro do pacote, realmente olha para ele?
Siga o `ctx` desde a assinatura de `Get` e anote cada lugar em que o código
bloqueia (esperando a rede, lendo bytes, dormindo). Para cada um, pergunte:
"se o contexto for cancelado agora, o que acorda esta linha?".

Depois, para cada resposta HTTP que chega: quem fecha o corpo? E quais
respostas merecem uma nova tentativa?

## Nível 2

Lugares para olhar em `src/fetch/fetch.go`:

- `c.Get(url)` monta uma requisição sem contexto. Procure em `net/http` a
  forma de criar uma requisição que carrega um `context.Context`; com ela, o
  transporte aborta a conexão **e** a leitura do corpo quando o contexto termina.
- A espera padrão usa `time.Sleep`, que não pode ser interrompido. `select`
  com `ctx.Done()` e um `time.Timer` pode (D&K, seção 8.9).
- `defer resp.Body.Close()` só é alcançado no caminho de sucesso.
- `p.BaseDelay * time.Duration(1<<(attempt-1))`: o que acontece com esse valor
  quando `attempt` passa de ~34? E quando passa de 63?
- `%v` em `fmt.Errorf` transforma o erro em texto; `errors.As` não consegue
  mais encontrar o `*StatusError`.
- `MaxBytes` e `ErrTooLarge` estão declarados, mas alguém os usa?

## Nível 3

Uma abordagem possível, em passos pequenos (rode `./test.sh` entre eles):

1. Crie a requisição uma vez com `http.NewRequestWithContext(ctx, ...)` e use
   `c.Do(req)`. Não embrulhe a chamada numa goroutine com `select`: ela
   retornaria rápido, mas a requisição continuaria viva em segundo plano
   (o teste percebe pelo servidor e pelo dump de goroutines).
2. Feche o corpo com `defer` logo depois de `Do` retornar sem erro. Nas
   respostas de erro, descarte (com limite) o que restar do corpo.
3. Leia o corpo com `io.LimitReader(body, max+1)`: se vier mais que `max`,
   é `ErrTooLarge`. Um `Content-Length` declarado acima do limite já permite
   desistir sem ler nada.
4. Escreva uma função `retryable(err)`: `*StatusError` 5xx/429 → sim; demais
   status → não; `ErrTooLarge` → não; erros de transporte → sim. Antes de
   classificar, verifique `ctx.Err()`: se o contexto acabou, retorne-o.
5. Calcule o backoff dobrando a partir de `BaseDelay` e pare de dobrar ao
   atingir o teto (compare com `MaxDelay/2` antes de multiplicar). Com
   `Retry-After`, use o maior dos dois.
6. Antes de esperar, compare `time.Until(deadline)` com a espera; se não cabe,
   retorne um erro com dois `%w`: `context.DeadlineExceeded` e a última falha.
7. Use `%w` em todos os `fmt.Errorf` que embrulham erros.

## Nível 4

Checklist final do contrato, item a item:

- `MaxAttempts < 1` conta como 1; contexto já cancelado → nenhuma requisição.
- O erro de uma requisição cancelada já satisfaz `errors.Is(err, ctx.Err())`?
  Se não tiver certeza, embrulhe os dois (`%w ... %w`).
- A espera padrão é a **única** que deve ver o `ctx`; quando `p.Sleep` é
  fornecido, chame-o — e respeite o erro que ele retornar.
- Conferiu se `go vet ./...` não reclama de nada (ex.: `cancel` não chamado)?
