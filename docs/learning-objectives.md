# Objetivos de aprendizagem — desafio-17

Ao concluir este desafio você será capaz de:

1. Tratar SIGTERM/SIGINT com `signal.NotifyContext` e orquestrar o
   desligamento: readiness 503, drenagem, `http.Server.Shutdown` com prazo e
   `Close` forçado depois.
2. Explicar por que `Serve` retorna `http.ErrServerClosed` assim que `Shutdown`
   começa, e fazer o processo esperar `Shutdown` terminar antes de sair.
3. Parar workers de fundo por `context.Context`, na ordem certa (depois que as
   requisições terminaram), sem vazar goroutines.
4. Manter um journal em arquivo consistente: escrita e descarga concorrentes
   protegidas, descarga final no `Close`, nada gravado para requisições
   abandonadas.
5. Escrever middlewares HTTP corretos com a stdlib: request id, log
   estruturado com `log/slog`, recover que responde 500 e timeout por
   requisição sem data race, sem vazamento e repassando pânicos.
6. Distinguir liveness de readiness e escolher o código de saída do processo
   conforme o desligamento foi limpo (0) ou estourou o prazo (≠ 0).

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Sinais e sequência de desligamento | `TestBinario_DesligamentoGracioso` (`binary_test.go`): compila `cmd/orders` com `-race`, sobe em `127.0.0.1:0`, dispara 30 `POST /orders` de 1,2 s, envia **SIGTERM** real; exige `/readyz` 503 50 ms depois, conexão TCP nova recusada após `DRAIN_DELAY`, os 30 pedidos com 201 e saída entre ~1 s e `DRAIN_DELAY + WORK_DELAY + 2 s`. |
| 2. `ErrServerClosed` e espera | O mesmo teste exige código de saída 0 e que o processo não saia antes dos pedidos em andamento terminarem; `TestBinario_PrazoEstourado` exige que ele espere pelo menos `SHUTDOWN_TIMEOUT` (1 s) com uma requisição travada. |
| 3. Workers e vazamentos | `TestJournal_WorkerParaComContexto`: `Run` bloqueia até o cancelamento, retorna em < 1 s depois dele, faz a descarga final e nenhuma goroutine nova de `src/orders` sobrevive. `TestTimeout_SemVazamentoNemCorrida`: nenhuma goroutine nova de `src/httpx` depois que os handlers terminam. |
| 4. Journal consistente | `TestJournal_CloseGravaTudo` (3 `Append` + `Close` → 3 linhas íntegras; `ErrClosed` depois), `TestJournal_ConcorrenciaIntegra` (8×60 `Append` concorrentes com descarga a cada 1 ms → 480 linhas JSON íntegras, sob `-race`), `TestPedidos_ExpiradoNaoGrava` (pedido que recebeu 503 não aparece no journal; o pedido aceito aparece com o mesmo `order_id`/`request_id`), e no binário: journal com **exatamente** 30 registros íntegros, todos os confirmados presentes; nenhum registro do pedido abandonado. |
| 5. Middlewares | `TestRequestID_PropagaEGera` (cabeçalho e contexto, preserva id válido, troca inválido), `TestLogging_RegistraStatus` (JSON com status 200 implícito e 404, `duration_ms`, `request_id`), `TestRecover_PanicoVira500` (500, log `msg=panic`, `ErrAbortHandler` repassado), `TestTimeout_RespondeNoPrazo` (503 + `TimeoutBody` em ~100 ms, contexto com prazo, resposta rápida intacta), `TestTimeout_PanicoPropagado` (num processo filho: pânico sob `Timeout` vira 500 em vez de derrubar o processo), `TestBinario_PanicoNaoDerruba` (pedido sem itens não derruba o serviço). |
| 6. Liveness, readiness e código de saída | `TestBinario_DesligamentoGracioso` (`/readyz` 200 antes e 503 durante a drenagem; `/healthz` 200 durante a drenagem; saída 0), `TestBinario_PrazoEstourado` (saída ≠ 0 entre 1 s e 2,6 s após o SIGTERM; a requisição travada não recebe 201). |
| Transversal | `go vet ./...` sem alertas (inclui o uso de `signal.Notify` com canal sem buffer) e `-race` na suíte e no binário. |
