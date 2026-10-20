# desafio-17 · Desligamento gracioso

> Trilha: go · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`)

## Contexto

O serviço `orders` recebe pedidos da loja: cada `POST /orders` reserva estoque,
cobra no gateway de pagamento (≈300 ms) e grava o pedido num *journal* em
arquivo — a fonte da verdade que o faturamento lê de madrugada.

Desde que o serviço foi para o Kubernetes, todo deploy vira incidente. Durante
o rolling update, clientes recebem `connection reset`, o suporte encontra
pedidos **confirmados ao cliente que não estão no journal** (e, às vezes, uma
última linha cortada pela metade), e o balanceador continua mandando tráfego
para Pods que já estão morrendo. Para piorar, um pedido sem itens que um
parceiro mandou por engano derrubou o Pod inteiro.

O time de plataforma resumiu numa frase: *"o serviço não sabe morrer"*.

## Problema

Sintomas observáveis (rode `./start.sh`: a demonstração em `cmd/demo` sobe o
serviço, dispara 10 pedidos lentos e envia SIGTERM no meio):

- Todos os pedidos em andamento terminam com erro de conexão, nenhum com `201`.
- O processo sai em milissegundos com código **1** e a mensagem `http: Server closed`.
- `/readyz` nunca responde 503 — o balanceador não tem como saber que o Pod está saindo.
- O journal fica sem os pedidos que estavam no buffer.
- Um `POST /orders` com `"items": []` derruba o processo inteiro.
- `go vet` reclama de `cmd/orders/main.go`, e o race detector encontra corridas
  nos middlewares e no journal.

Seu trabalho é fazer o serviço cumprir o contrato abaixo — investigando e
corrigindo o código existente, não reescrevendo o serviço.

**Contrato do desligamento** (ao receber SIGTERM ou SIGINT):

1. `/readyz` passa a responder **503 imediatamente**; `/healthz` continua 200.
2. Durante `DRAIN_DELAY` o serviço continua aceitando e atendendo requisições
   (tempo para os endpoints do Kubernetes/balanceador serem atualizados).
3. Depois da drenagem, **novas conexões são recusadas** e as requisições em
   andamento **concluem normalmente** (`http.Server.Shutdown`).
4. Se elas não terminarem em `SHUTDOWN_TIMEOUT`, as conexões são fechadas à
   força e o processo sai com código **≠ 0** logo depois do prazo.
5. Só então os workers de fundo param (via `context`) e o journal é fechado:
   **todo pedido confirmado com 201 está no journal**, nenhum registro é parcial
   e nenhum pedido abandonado (prazo estourado, cliente desistiu) é gravado.
6. Desligamento limpo sai com código **0**, sem goroutines vazadas nem data races.

**Contrato dos componentes**

- `httpx.RequestID`: reaproveita um `X-Request-ID` válido (1–64 caracteres
  `[A-Za-z0-9._-]`) ou gera um novo; o id fica no contexto (`RequestIDFrom`) e
  volta no cabeçalho da resposta.
- `httpx.Logging`: uma linha JSON por requisição com `msg=request`, `method`,
  `path`, `status` (o status **real**, inclusive o 200 implícito), `duration_ms`
  e `request_id`.
- `httpx.Recover`: um pânico vira resposta **500** e uma linha de log
  `msg=panic`; `http.ErrAbortHandler` é repassado ao `net/http`.
- `httpx.Timeout(d, h)`: após `d`, responde **503** com `TimeoutBody`; o handler
  recebe um contexto com esse prazo; nada que ele escreva depois do prazo chega
  ao cliente; nenhuma goroutine fica presa; um pânico no handler chega ao
  `Recover` que está por fora.
- `orders.Journal`: `Append` é seguro para uso concorrente; `Run(ctx, every)`
  **bloqueia** até `ctx` terminar, descarregando o buffer a cada `every` e uma
  última vez ao parar; `Close` grava tudo o que foi aceito, e depois dele
  `Append` devolve `ErrClosed`.
- `orders.Service.CreateOrder`: pedido válido → **201** com `order_id` e
  `request_id`, e um registro no journal; pedido sem itens nunca derruba o
  processo (400 ou 500).

**Configuração do binário** (variáveis de ambiente): `ADDR` (`:8080`; os testes
usam `127.0.0.1:0`), `JOURNAL_PATH`, `WORK_DELAY` (300ms), `REQUEST_TIMEOUT` (2s),
`FLUSH_INTERVAL` (200ms), `DRAIN_DELAY` (2s), `SHUTDOWN_TIMEOUT` (5s). Os logs
saem em JSON na saída padrão, e ao subir o serviço registra
`{"msg":"listening","addr":"<endereço real>"}` — os testes leem a porta dali.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Tratar SIGTERM/SIGINT com `signal.NotifyContext` e orquestrar o desligamento: readiness 503, drenagem, `Shutdown` com prazo e `Close` forçado.
- Entender por que `Serve` retorna `http.ErrServerClosed` na hora e o que o processo precisa esperar.
- Parar workers de fundo por `context.Context`, na ordem certa, sem vazar goroutines.
- Manter um journal consistente sob concorrência e no desligamento.
- Escrever middlewares HTTP corretos: request id, `log/slog`, recover e timeout sem data race.
- Distinguir liveness de readiness e escolher o código de saída do processo.

## Conhecimentos necessários

- Go: goroutines, canais, `select`, `sync.Mutex`, `sync.WaitGroup`, `sync/atomic`.
- `context` (desafio-8): cancelamento, prazos, `Done()`.
- `net/http` como servidor: `http.Handler`, `ResponseWriter`, middlewares que embrulham handlers.
- Noções de processos Unix: sinais (SIGTERM, SIGINT, SIGKILL) e código de saída.

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), sem rede: os testes sobem o binário na
  interface de loopback do próprio container e enviam **sinais Linux de
  verdade**. O cache de build fica no volume `gitquest-desafio-17-gocache`
  (remova com `docker volume rm gitquest-desafio-17-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** em Linux/macOS/WSL, com Go >= 1.25, `make` e um
  compilador C (o race detector usa cgo), rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. No Windows nativo os sinais POSIX não funcionam
  da mesma forma — use o modo Docker, que é o avaliador oficial.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila o código e roda a demonstração, que
compara cada sintoma com o comportamento esperado. Depois leia
`cmd/orders/main.go` com o contrato acima em mãos — é ali que o desligamento
acontece (ou deveria acontecer).

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log`: cada teste diz o que
  esperava; os testes do binário imprimem os logs JSON do serviço quando falham.
- `go vet ./...` (já roda no `./test.sh`) e o race detector (`-race`), ligado
  nos testes **e** no binário que os testes de integração compilam.
- Os dumps de goroutines que os testes de vazamento imprimem.
- `go doc net/http.Server.Shutdown`, `go doc os/signal.NotifyContext`,
  `go doc net/http.TimeoutHandler`, `go doc bufio.Writer`.

## Arquivos que você pode modificar

- `cmd/orders/` — o `main` do serviço (sinais, servidor, ordem do desligamento).
- `src/` — os middlewares (`src/httpx/`) e o domínio de pedidos com o journal
  (`src/orders/`). Mantenha a API pública usada pelos testes (`RequestID`,
  `RequestIDFrom`, `HeaderRequestID`, `Logging`, `Recover`, `Timeout`,
  `TimeoutBody`, `OpenJournal`, `Journal.Append/Run/Close`, `ErrClosed`,
  `Record`, `Item`, `Service`, `Created`); pode criar funções e arquivos
  auxiliares. Use apenas a biblioteca padrão.

Não altere `tests/`, `cmd/demo/`, `test.sh`, `challenge.yaml`, `go.mod`,
`Dockerfile` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -race -count=1 -timeout 120s ./...` no container
e imprime um ✔/✘ por critério, com o motivo de cada falha. Os testes de
integração compilam `cmd/orders` com `-race`, sobem o binário numa porta livre,
disparam 30 pedidos lentos e enviam SIGTERM no meio.

## Critérios de conclusão

- [ ] request id gerado ou preservado, no contexto e no cabeçalho da resposta
- [ ] log estruturado registra o status real de cada requisição
- [ ] pânico no handler vira 500; `http.ErrAbortHandler` é repassado
- [ ] timeout por requisição responde 503 no prazo, sem goroutine vazada, sem escrita tardia e repassando pânicos ao `Recover`
- [ ] pedido que expirou não é gravado no journal
- [ ] journal: `Close` grava tudo; escritas e descargas concorrentes íntegras; worker para pelo contexto, descarrega e não vaza
- [ ] SIGTERM: `/readyz` 503 e `/healthz` 200 durante a drenagem, conexões novas recusadas depois dela, os 30 pedidos em andamento recebem 201, journal com exatamente 30 registros íntegros, saída 0 dentro do tempo esperado
- [ ] SIGTERM com requisição travada: espera `SHUTDOWN_TIMEOUT`, fecha à força e sai com código ≠ 0 logo depois, sem gravar o pedido abandonado
- [ ] pedido que provoca pânico não derruba o serviço
- [ ] nenhum data race; `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *A Linguagem de Programação Go*, Alan A. A. Donovan e Brian W. Kernighan — cap. 8, seções 8.4 (Canais, inclusive canais com buffer), 8.7 (Multiplexando com select) e 8.9 (Cancelamento).
- *Mastering Go*, 4ª ed., Mihalis Tsoukalos — cap. 8, "Handling UNIX signals", "Buffered channels", "The Go race detector" e "The context package"; cap. 9, "Creating a web server" e "Setting the timeout period on the server side".
- Documentação de `http.Server.Shutdown` e `http.TimeoutHandler`: https://pkg.go.dev/net/http#Server.Shutdown
- Documentação de `signal.NotifyContext`: https://pkg.go.dev/os/signal#NotifyContext
- Documentação de `log/slog`: https://pkg.go.dev/log/slog
- *Kubernetes Up & Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan Evenson — cap. 5, "Deleting a Pod" (período de encerramento) e "Health Checks"; cap. 7, "Readiness Checks" (readiness como mecanismo de desligamento gracioso).
- Kubernetes — "Pod Lifecycle: Termination of Pods": https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/#pod-termination

## Próximo desafio

**desafio-18 — Probes que mentem** (trilha kubernetes): agora que o serviço
sabe sair de cena, é hora de ver o outro lado — liveness e readiness probes
mal configuradas que reiniciam Pods saudáveis e mandam tráfego para Pods que
ainda não estão prontos.
