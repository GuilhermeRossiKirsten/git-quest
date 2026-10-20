# Objetivos de aprendizagem — desafio-8

Ao concluir este desafio você será capaz de:

1. Propagar `context.Context` até a requisição HTTP (`http.NewRequestWithContext`)
   para que cancelamentos e prazos interrompam a conexão e a leitura do corpo —
   cancelando de verdade, e não abandonando o trabalho em segundo plano.
2. Esperar entre tentativas com `select` sobre `ctx.Done()` e um `time.Timer`,
   em vez de `time.Sleep`.
3. Classificar falhas em transitórias (rede, 5xx, 429) e permanentes (demais 4xx,
   corpo grande demais), respeitando o cabeçalho `Retry-After`.
4. Implementar backoff exponencial com teto sem overflow de `time.Duration`, e
   desistir cedo quando o prazo restante do contexto não comporta a próxima espera.
5. Fechar sempre o corpo de `http.Response` e limitar seu tamanho com
   `io.LimitReader`, evitando vazamento de conexões e de memória.
6. Embrulhar erros com `%w` (inclusive dois `%w` na mesma mensagem) para que
   `errors.Is` e `errors.As` encontrem a causa original.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/fetch_test.go`) |
|---|---|
| 1. Contexto na requisição | `TestCancelamento_DuranteRequisicao`: handler lento; cancela após 100 ms; exige retorno em < 400 ms, `errors.Is(err, context.Canceled)`, que o **servidor** observe `r.Context().Done()` e que nenhuma goroutine com frames do pacote `fetch` sobreviva. `TestCancelamento_DuranteLeituraDoCorpo`: servidor envia 1 KiB e trava; prazo de 200 ms; exige retorno rápido e `context.DeadlineExceeded`. |
| 2. Espera cancelável | `TestCancelamento_DuranteEspera`: 503 com `BaseDelay` de 3 s e a espera padrão (sem `p.Sleep`); cancela após 150 ms; exige retorno em < 400 ms, `context.Canceled` e uma única requisição. |
| 3. Classificação e Retry-After | `TestRetry_ErrosTransitorios` (503, 500, 502 e conexão derrubada → sucesso na 3ª tentativa com esperas `[10ms 20ms]`; `MaxAttempts` respeitado; `RetryPolicy{}` faz 1 tentativa). `TestRetry_NaoRepete4xx` (400/401/403/404/410 → 1 requisição, sem espera, `*StatusError` com o código). `TestRetry_429RespeitaRetryAfter` (`Retry-After: 2` → espera de 2 s; ausente/inválido → backoff). |
| 4. Backoff com teto e prazo | `TestBackoff_TetoSemOverflow`: sequência exata `[10 20 40 50 50]ms`; 100 tentativas com base 1 s e teto 30 s → 99 esperas em `(0, 30s]`, não decrescentes, terminando no teto. `TestPrazo_NaoIniciaTentativaInutil`: prazo de 1 s e espera de 5 s → retorno em < 500 ms sem chamar `Sleep`, `DeadlineExceeded` **e** `*StatusError` 503 na cadeia; contexto já cancelado → 0 requisições. |
| 5. Corpo fechado e limitado | `TestRecursos_CorposSempreFechados`: um `RoundTripper` instrumentado conta corpos abertos em respostas 200, 404, 503, 429→200 e 500→200; todos devem estar fechados quando `Get` retorna. `TestLimite_CorpoGrande`: 32 MiB com e sem `Content-Length` e limite de 1 MiB → `ErrTooLarge` sem nova tentativa; exatamente `MaxBytes` é aceito; limite padrão de 10 MiB. |
| 6. Causa preservada | `TestErros_PreservaCausa`: após esgotar tentativas com 502, `errors.As` encontra `*StatusError{502}` e a mensagem cita a URL. Os testes de cancelamento e prazo também exigem `errors.Is` com o erro do contexto. |
| Transversal | `TestBasico_Sucesso`, `TestConcorrencia_ChamadasSimultaneas` (32 chamadas concorrentes), `go test -race` em toda a suíte e `go vet ./...` sem alertas. |
