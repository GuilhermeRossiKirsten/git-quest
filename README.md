# desafio-7 · O pool que transbordou

> Trilha: go · Nível: intermediate · ~60 min · 200 pontos

## Contexto

A Lojinha gera as miniaturas do catálogo num job noturno escrito em Go. O
coração do job é `pool.Run`, uma função genérica que aplica uma operação a uma
lista de itens "com no máximo N workers". Ela também é usada por outros jobs em
lote, então muita gente depende do contrato documentado no topo de
`src/pool/pool.go`.

Desde a "otimização" do ano passado, o job virou fonte de incidentes. Você foi
escalado para o plantão desta semana.

## Problema

O que a equipe de operações relatou (e que você consegue reproduzir com
`./demo.sh`):

- Num lote de 50 mil fotos o storage recusou conexões: o painel mostrou
  milhares de conversões simultâneas, embora o job esteja configurado com
  `workers=8`.
- Clientes reclamaram de produtos exibindo a miniatura **de outro produto**. Em
  alguns lotes, ainda, vieram menos miniaturas do que fotos — sem erro algum.
- Quando há fotos corrompidas, o job devolve o erro, mas as demais conversões
  continuam rodando em segundo plano, e a contagem de goroutines do processo
  (em `/debug/pprof/goroutine`) só cresce de uma madrugada para a outra.
- Às vezes o erro devolvido não diz qual foto falhou; quando o operador aborta
  o job (cancelando o `context`), ele não para.
- `go vet` tem um alerta pendente nesse pacote há meses.

Sua missão é fazer `pool.Run` cumprir o contrato documentado no próprio
arquivo, sem mudar a assinatura. Investigue primeiro: o código é curto, mas
os problemas interagem entre si.

Ferramentas de diagnóstico recomendadas:

- `go test -race` e `go run -race` — o detector de corridas aponta as linhas
  que acessam a mesma memória sem sincronização;
- `go vet ./...`;
- o stack das goroutines vazadas, que o teste de vazamento imprime (o mesmo que
  `runtime.Stack`/`pprof` mostrariam em produção);
- `./demo.sh -fail 3` para ver pico de concorrência, ordem e goroutines restantes.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Limitar a concorrência com um worker pool em vez de uma goroutine por item.
- Preservar a ordem dos resultados sem mutex.
- Encontrar e eliminar data races com o detector de corridas.
- Usar `sync.WaitGroup` corretamente (e conhecer `wg.Go`, do Go 1.25).
- Propagar o primeiro erro, cancelando o restante com `context`, preservando
  `errors.Is` com `%w`.
- Respeitar cancelamento e deadline do chamador.
- Garantir que nenhuma goroutine sobreviva ao retorno da função.

## Conhecimentos necessários

- Go básico: slices, closures, generics (`func Run[T, R any]`), tratamento de erros.
- Goroutines e canais (envio/recebimento, canais com e sem buffer, `close`, `range`, `select`).
- Noções de `sync.WaitGroup`, `sync.Once` e `context.Context`.

## Ambiente

- Toolchain: **Go 1.25.3**, imagem Docker fixada `golang:1.25.3-bookworm`.
  Só a biblioteca padrão é usada; o container roda sem rede.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- O cache de compilação fica no volume Docker `gitquest-go-cache`, então as
  execuções seguintes são bem mais rápidas que a primeira.
- **Sem Docker?** Com Go >= 1.25.3 instalado *e* cgo funcionando (o `-race`
  exige um compilador C, como gcc), rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. Os scripts verificam a versão e o cgo antes.
  No Windows sem gcc, prefira o Docker.

## Como começar

```bash
./start.sh            # constrói a imagem da toolchain e compila o código inicial
./demo.sh             # job saudável: compare o pico de conversões com workers=8
./demo.sh -fail 3     # lote com 3 fotos corrompidas: erro, goroutines restantes
./test.sh             # critérios de conclusão
```

Fora do container (se tiver Go local): `go test -race -count=1 ./tests/` e
`go test -run '^$' -bench . ./tests/` (benchmark para comparar números de
workers — material de estudo, não critério).

## Arquivos que você pode modificar

- `src/pool/` — a implementação de `pool.Run` (pode criar arquivos auxiliares no pacote).

Não altere a assinatura de `Run` nem `ErrInvalidWorkers`. Não altere `tests/`,
`test.sh`, `go.mod`, `challenge.yaml` nem o `Makefile`: o avaliador usa as
versões originais. `src/cmd/thumbs` é apenas a ferramenta de demonstração.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador roda, no container (limite de 2 CPUs e 2 GB, sem rede):
`go build ./...`, `go vet ./...` e
`go test -race -count=1 -timeout 60s ./tests/`, e mostra cada critério com
✔/✘ e o motivo. O log completo fica em `build/test.json`.

## Critérios de conclusão

- [ ] O código compila e `go vet ./...` não emite alertas.
- [ ] Casos de borda: `workers <= 0` devolve erro com `errors.Is(err, pool.ErrInvalidWorkers)` sem chamar `fn`; entradas vazias devolvem resultado vazio e erro `nil`.
- [ ] No máximo `workers` chamadas de `fn` em paralelo — e, havendo trabalho, todas as vagas são usadas.
- [ ] `results[i]` é o resultado de `inputs[i]`, sem resultados perdidos (200 e 2000 itens).
- [ ] No primeiro erro: o `ctx` das chamadas em andamento é cancelado, nenhuma chamada nova começa, e `Run` devolve `(nil, err)` com `errors.Is(err, original)` e o índice do item na mensagem (`item 3: ...`).
- [ ] Cancelamento ou deadline do chamador fazem `Run` voltar em até 200 ms com `errors.Is(err, context.Canceled)` / `context.DeadlineExceeded`; com o ctx já cancelado, `fn` nunca é chamada.
- [ ] Nenhuma goroutine criada por `Run` continua viva depois do retorno (sucesso, erro ou cancelamento).
- [ ] Nenhum data race detectado por `go test -race`.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *A Linguagem de Programação Go*, Alan A. A. Donovan e Brian W. Kernighan — cap. 8 "Gorrotinas e canais" (8.4 canais com e sem buffer, 8.5 looping em paralelo, 8.9 cancelamento), cap. 9 "Concorrência com variáveis compartilhadas" (9.1 condições de concorrência, 9.5 `sync.Once`, 9.6 o detector de concorrência) e 11.4 (funções benchmark).
- *Mastering Go*, 4ª ed., Mihalis Tsoukalos — cap. 8 "Go Concurrency" (worker pools, `sync.WaitGroup`, o pacote `context`, variáveis capturadas por closures em goroutines).
- Go Blog — [Go Concurrency Patterns: Pipelines and cancellation](https://go.dev/blog/pipelines).
- Documentação — [Data Race Detector](https://go.dev/doc/articles/race_detector).
- Documentação de [`sync.WaitGroup`](https://pkg.go.dev/sync#WaitGroup) (inclui `WaitGroup.Go`), [`context`](https://pkg.go.dev/context) e [`errors`](https://pkg.go.dev/errors).

## Próximo desafio

**desafio-8 — A requisição que não sabia parar**: um cliente HTTP que ignora
cancelamentos. Você vai levar o que aprendeu sobre `context` para deadlines,
retry com backoff e propagação de cancelamento através da rede.
