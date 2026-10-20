# Dicas — desafio-17

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe uma linha do tempo do desligamento, do SIGTERM até o processo
terminar. Em cada instante, responda: o balanceador ainda manda tráfego? O
listener ainda aceita conexões? As requisições em andamento podem terminar? O
que ainda está só na memória (buffer do journal)? Quem espera quem?

Agora leia `cmd/orders/main.go` e compare: depois que o sinal chega, **qual é a
próxima linha que roda na goroutine principal**? E o que acontece com as
outras goroutines quando `main` termina?

Depois olhe os middlewares em `src/httpx/` com uma pergunta para cada um: "em
que goroutine este código roda, e quem mais mexe no mesmo `ResponseWriter`?"

## Nível 2

Lugares para olhar:

- `srv.Serve(ln)` retorna **assim que** `Shutdown` começa, com
  `http.ErrServerClosed` — que é o caso normal, não um erro fatal. Leia
  `go doc net/http.Server.Shutdown` com atenção ao parágrafo sobre isso.
- `go vet` já aponta o problema de `signal.Notify`. `signal.NotifyContext`
  devolve um `context` que é cancelado no sinal — combina com `select`.
- `Shutdown(context.Background())` espera para sempre. E depois do prazo?
- `/readyz` responde sempre 200. Quem deveria mudar isso, e quando?
- `Journal.Run` dispara uma goroutine e retorna: quem a para? Ela chama
  `Flush` sem o mutex que `Append` usa. E `Close` descarta o buffer.
- `Timeout` roda o handler em outra goroutine: um pânico ali não passa pelo
  `defer recover()` de `Recover` (recover só funciona na própria goroutine).
  O canal `done` não tem buffer. E as duas goroutines escrevem em `w`.
- `Recover` recupera, mas o que o cliente recebe? E o `http.ErrAbortHandler`?
- `RequestID` define o cabeçalho depois de chamar o handler; `Logging` assume
  status 0 quando o handler só chama `Write`.
- `CreateOrder` usa `time.Sleep`: o que acontece com o pedido quando o prazo
  da requisição estoura no meio do trabalho?

## Nível 3

Uma abordagem possível, em passos pequenos (rode `./test.sh` entre eles):

1. Comece pelos middlewares, que têm testes isolados. Para `Timeout`, a
   biblioteca padrão já tem `http.TimeoutHandler`, que bufferiza a resposta,
   não vaza a goroutine e repassa pânicos para quem chamou. Em `Recover`,
   responda 500 e repasse `http.ErrAbortHandler` com `panic`.
2. No journal, faça `Run` bloquear num `select` entre `ctx.Done()` e o ticker
   (com `defer ticker.Stop()`), descarregando sob o mesmo mutex de `Append`, e
   uma última vez ao parar. Em `Close`: `Flush`, `Sync`, `Close`.
3. Em `CreateOrder`, troque o `Sleep` por um `select` entre um timer e
   `r.Context().Done()`; se o contexto acabou, não grave nada. Pedido sem
   itens deve virar 400 antes de qualquer divisão.
4. No `main`, use `signal.NotifyContext`, rode `srv.Serve` numa goroutine que
   manda o erro para um canal com buffer, e espere num `select` pelo sinal.
5. Ao receber o sinal: marque "não pronto" (um `atomic.Bool` lido por
   `/readyz`), espere `DRAIN_DELAY`, chame `Shutdown` com
   `context.WithTimeout(..., SHUTDOWN_TIMEOUT)`; se ele falhar, `Close` e
   código 1. Trate `ErrServerClosed` como esperado.
6. Só depois do `Shutdown`: cancele o contexto **dos workers** (que não é o do
   sinal!), espere-os com um `WaitGroup` e feche o journal. Saia com
   `os.Exit(run())`, para que os `defer` de `run` executem.

## Nível 4

Checklist final, item a item:

- O contexto do sinal **não** pode chegar às requisições (ex.: via
  `http.Server.BaseContext`): `Shutdown` existe justamente para deixá-las
  terminar. Cancelar tudo no SIGTERM perde os pedidos em andamento.
- Os workers precisam de um contexto próprio, cancelado **depois** do
  `Shutdown`; se pararem antes, os últimos pedidos ficam no buffer.
- Liveness (`/healthz`) continua 200 durante a drenagem: reiniciar o processo
  agora seria pior.
- Depois do primeiro sinal, chame o `stop()` do `NotifyContext`: um segundo
  Ctrl+C volta a encerrar na hora.
- Conferiu com o race detector? Os testes de integração compilam o binário com
  `-race` — uma corrida no serviço também reprova.
