# desafio-8 · A requisição que não sabia parar

> Trilha: go · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`)

## Contexto

Toda madrugada o serviço de catálogo baixa a tabela de preços de um fornecedor
usando o pacote `fetch`. O handler que dispara a sincronização tem um prazo de
2 segundos (`context.WithTimeout`) — mas, na noite em que o fornecedor ficou
lento, o painel mostrou chamadas de **40 segundos**, o pool de conexões esgotou
e o pod foi reiniciado por falta de memória depois de baixar um "relatório" de
800 MB que alguém publicou por engano no lugar da tabela.

Nos logs, a única pista é sempre a mesma mensagem genérica, e o alarme de
"recurso inexistente" (404) disparou com quatro requisições por minuto em vez
de uma.

## Problema

Sintomas observáveis (rode `./start.sh` e veja a demonstração em `cmd/demo`):

- Com um fornecedor lento e prazo de 300 ms, `fetch.Get` leva segundos e
  **volta com sucesso** como se o prazo não existisse.
- Com o fornecedor fora do ar (503) e prazo de 500 ms, `Get` faz todas as
  tentativas e dorme entre elas muito além do prazo.
- Um 404 é pedido de novo várias vezes.
- O erro final é só texto: `errors.Is`/`errors.As` não reconhecem nem o
  cancelamento nem o status HTTP que causou a falha.
- Com muitas tentativas, as esperas entre elas crescem sem limite (e algo
  estranho acontece lá pela 35ª).
- Respostas de erro deixam conexões penduradas; corpos gigantes são lidos
  inteiros para a memória.

Seu trabalho é fazer `fetch.Get` cumprir o contrato abaixo — investigando e
corrigindo o código existente, não reescrevendo o serviço.

**Contrato de `fetch.Get(ctx, client, url, policy)`**

1. Respeita cancelamento e prazo do `ctx` em **todas** as fases: conexão e
   requisição, leitura do corpo e espera entre tentativas. Retorna em menos de
   ~100 ms após o cancelamento (os testes toleram 400 ms), e o erro satisfaz
   `errors.Is(err, context.Canceled)` ou `errors.Is(err, context.DeadlineExceeded)`.
   Com o contexto já cancelado, não faz nenhuma requisição.
2. Repete apenas falhas transitórias: erros de rede, status 5xx e 429. Demais
   4xx retornam na hora, sem nova tentativa.
3. Espera `BaseDelay`, `2·BaseDelay`, `4·BaseDelay`, ... entre tentativas,
   limitado a `MaxDelay` (0 = 30 s), sem overflow mesmo com centenas de
   tentativas. Num 429 com `Retry-After: <segundos>`, espera o maior entre esse
   valor e o backoff. `MaxAttempts < 1` conta como 1 tentativa. (Sem jitter,
   para que os testes sejam determinísticos — o artigo da AWS nas Referências
   explica por que, em produção, vale a pena adicioná-lo.)
4. Se o prazo restante do `ctx` for menor que a próxima espera, desiste
   imediatamente (sem chamar `Sleep`) com um erro que satisfaz
   `errors.Is(err, context.DeadlineExceeded)` e mantém a última falha na cadeia.
5. Fecha o corpo de **toda** resposta recebida.
6. Aceita no máximo `MaxBytes` bytes de corpo (0 = 10 MiB); acima disso retorna
   `ErrTooLarge` (sem nova tentativa) sem ler o resto.
7. O erro final preserva a causa: `errors.As(err, &*fetch.StatusError)` encontra
   o status HTTP, e a mensagem cita a URL.
8. `RetryPolicy.Sleep`, quando definido, substitui a espera entre tentativas
   (os testes o usam para registrar as durações sem esperar de verdade).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Propagar `context.Context` até a requisição HTTP para que cancelamentos e prazos interrompam a conexão e a leitura do corpo.
- Esperar com `select` sobre `ctx.Done()` e um timer, em vez de `time.Sleep`.
- Classificar falhas em transitórias e permanentes e respeitar `Retry-After`.
- Implementar backoff exponencial com teto, sem overflow, e desistir cedo quando o prazo não comporta a próxima espera.
- Fechar sempre o corpo da resposta e limitar seu tamanho.
- Embrulhar erros com `%w` para que `errors.Is`/`errors.As` funcionem.

## Conhecimentos necessários

- Go: goroutines, `select`, canais, `time.Timer` (desafio-7 cobre o essencial).
- Pacote `context`: `WithCancel`, `WithTimeout`, `Done()`, `Err()`, `Deadline()`.
- `net/http` como cliente: `http.Client`, `http.Request`, `http.Response.Body`.
- Erros em Go: `fmt.Errorf` com `%w`, `errors.Is`, `errors.As`.

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), sem rede: os testes usam `httptest.Server`
  na interface de loopback do próprio container. O cache de build fica no
  volume nomeado `gitquest-desafio-8-gocache` (remova com
  `docker volume rm gitquest-desafio-8-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25, `make` e um compilador C (o
  race detector usa cgo) instalados, rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. O avaliador oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila o código e roda `cmd/demo`, que mostra para
cada cenário o comportamento esperado, quanto tempo a chamada levou e quantas
requisições o servidor recebeu. Depois leia `src/fetch/fetch.go` com o contrato
acima em mãos.

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log` (cada teste diz o que esperava).
- O race detector (`-race`) já está ligado nos testes.
- O dump de goroutines que o teste de cancelamento imprime quando algo do
  pacote `fetch` continua rodando depois que `Get` retornou.
- `go doc net/http.NewRequestWithContext`, `go doc context`, `go doc io.LimitReader`.

## Arquivos que você pode modificar

- `src/` — o pacote `fetch` (`src/fetch/fetch.go`). Mantenha a API pública
  (`Get`, `RetryPolicy`, `StatusError`, `ErrTooLarge`, `DefaultMaxBytes`,
  `DefaultMaxDelay`); pode criar funções e arquivos auxiliares em `src/fetch/`.
  Use apenas a biblioteca padrão.

Não altere `tests/`, `cmd/`, `test.sh`, `challenge.yaml`, `go.mod`, `Dockerfile`
nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -race -count=1 -timeout 90s ./...` no container
e imprime um ✔/✘ por critério, com o motivo de cada falha.

## Critérios de conclusão

- [ ] busca bem-sucedida devolve o corpo; chamadas concorrentes sem data race
- [ ] repete erros transitórios (5xx, rede) com backoff `BaseDelay·2ⁿ`
- [ ] não repete erros 4xx (exceto 429)
- [ ] 429 respeita `Retry-After`
- [ ] backoff limitado por `MaxDelay`, sem overflow em 100 tentativas
- [ ] cancelamento durante a requisição: retorno rápido, `context.Canceled`, servidor percebe o cancelamento, nenhuma goroutine largada
- [ ] prazo expira durante a leitura do corpo: retorno rápido, `context.DeadlineExceeded`
- [ ] cancelamento durante a espera entre tentativas: retorno rápido, `context.Canceled`
- [ ] não inicia tentativa cuja espera não cabe no prazo; contexto já cancelado não faz requisição
- [ ] corpos de resposta sempre fechados
- [ ] corpo limitado por `MaxBytes` (`ErrTooLarge`)
- [ ] erro final preserva a causa (`errors.As` encontra `*StatusError`)
- [ ] `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *A Linguagem de Programação Go*, Alan A. A. Donovan e Brian W. Kernighan — cap. 8, seções 8.7 (Multiplexando com select) e 8.9 (Cancelamento).
- *Mastering Go*, 4ª ed., Mihalis Tsoukalos — cap. 8, "The context package"; cap. 9, "The http.Response type" e "The http.Transport type".
- Go Blog — "Go Concurrency Patterns: Context": https://go.dev/blog/context
- Documentação de `net/http` (`NewRequestWithContext`, `Response.Body`): https://pkg.go.dev/net/http
- Documentação de `context`: https://pkg.go.dev/context
- Go Blog — "Working with Errors in Go 1.13" (`%w`, `errors.Is/As`): https://go.dev/blog/go1.13-errors
- Marc Brooker — "Exponential Backoff And Jitter", AWS Architecture Blog: https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/
- RFC 9110, seção 10.2.3 (`Retry-After`): https://www.rfc-editor.org/rfc/rfc9110#field.retry-after

## Próximo desafio

**desafio-9 — A imagem de um gigabyte** (trilha docker): do cliente que sabe
parar para a imagem que sabe emagrecer — multi-stage build, usuário não-root,
health check e nenhum segredo dentro da imagem.
