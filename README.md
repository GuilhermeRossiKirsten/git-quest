# desafio-22 · Mensagens pela metade

> Trilha: network · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`), só a biblioteca padrão

## Contexto

A cooperativa tem 140 balanças rodoviárias espalhadas pelo interior. Cada
pesagem vira uma mensagem que a balança envia por TCP ao **concentrador**
(`src/echo`): um servidor que devolve cada mensagem, intacta, como
confirmação — só depois do eco o firmware apaga a leitura da memória local.
As mensagens são enquadradas pelo pacote `src/frame`: 4 bytes de tamanho e
depois o conteúdo.

No notebook do desenvolvedor tudo funciona: loopback, mensagens de poucos
bytes, um cliente por vez. Em produção, desde que as balanças novas passaram a
mandar a **foto da placa** junto com o peso, a operação abriu uma pilha de
chamados — e o plantão de sábado descobriu que um scanner de portas na rede
consegue derrubar o concentrador com **um único pacote de 4 bytes**.

## Problema

Sintomas relatados (rode `./start.sh`: a demonstração em `cmd/demo` reproduz
cada um e mostra *esperado* × *obtido*):

- Leituras pequenas são confirmadas; leituras com foto (~70 KB) voltam
  **truncadas ou embaralhadas**, e a balança fica reenviando para sempre.
- Balanças no fim de links 3G lentos recebem confirmações **com o tamanho
  errado**, mesmo para mensagens pequenas.
- Um pacote com os bytes `ff ff ff ff` faz o processo **morrer por falta de
  memória**.
- Uma balança com firmware travado, que envia e nunca lê as confirmações, faz
  o número de goroutines e a memória do concentrador **crescerem sem parar**.
- Com `IdleTimeout` configurado, balanças **ativas** são desconectadas no
  meio da conversa; e uma conexão que pinga um byte de vez em quando nunca cai.
- No deploy, `Close` retorna, mas as conexões abertas continuam penduradas.

Seu trabalho é fazer o concentrador cumprir o contrato abaixo — investigando e
corrigindo o código existente, não reescrevendo o serviço.

**Contrato do protocolo (`src/frame`)**

- Frame = cabeçalho de `HeaderSize` = **4 bytes, `uint32` big-endian** (ordem
  de rede) com o tamanho do payload, seguido do payload (0 a `MaxSize` =
  1 MiB). Um cliente escrito em qualquer linguagem precisa conseguir conversar
  com o servidor seguindo só esta frase.
- `WriteFrame(w, p)`: escreve o frame; payload acima de `MaxSize` → erro que
  satisfaz `errors.Is(err, ErrTooLarge)`, **sem escrever nada**.
- `ReadFrame(r)`: devolve o próximo payload, qualquer que seja a forma como os
  bytes chegam (um por vez, aos pedaços, junto com `io.EOF`). Fim do fluxo
  **exatamente entre dois frames** → `io.EOF`; fim **no meio** de um frame
  (cabeçalho ou corpo) → `io.ErrUnexpectedEOF`; cabeçalho acima de `MaxSize`
  → `ErrTooLarge` **sem alocar** o tamanho anunciado.

**Contrato do servidor (`src/echo`)**

- Cada frame recebido é devolvido ao mesmo cliente, **na mesma ordem**, sem
  frames misturados, inclusive quando o cliente envia muitos frames seguidos
  sem esperar as respostas (pipeline).
- `IdleTimeout` (padrão 2 min): uma conexão que não entrega um **frame
  completo** dentro desse prazo é derrubada (cliente mudo, *slowloris*); um
  cliente que continua mandando frames nunca é derrubado por ele.
- **Backpressure**: um cliente que não lê as respostas não pode fazer a
  memória do servidor crescer — o servidor para de ler dele (o TCP freia o
  cliente) ou o desconecta; os outros clientes continuam sendo atendidos.
- `WriteTimeout` (padrão 10 s): se entregar uma resposta demorar mais que
  isso, o cliente é desconectado.
- `Close`: para de aceitar conexões, **fecha as conexões abertas** e retorna
  quando todos os handlers terminaram; `Serve` devolve `ErrServerClosed`;
  nenhuma goroutine do pacote sobrevive.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Explicar por que TCP é um fluxo de bytes sem fronteiras de mensagem e enquadrar mensagens com prefixo de tamanho.
- Ler corretamente de um `io.Reader`/`net.Conn`: leituras parciais, `io.ReadFull`, `io.EOF` × `io.ErrUnexpectedEOF`.
- Validar entradas da rede antes de alocar memória (tamanho máximo de frame).
- Definir um formato no fio sem ambiguidade (largura e ordem dos bytes do cabeçalho).
- Usar deadlines de `net.Conn` para *idle timeout* e *write timeout*, entendendo que são instantes absolutos.
- Aplicar backpressure com filas limitadas e um único escritor por conexão.
- Encerrar um servidor TCP sem vazar goroutines.

## Conhecimentos necessários

- Go: goroutines, canais (com e sem buffer), `select`, `sync.Mutex`, `sync.WaitGroup`.
- Interfaces `io.Reader`/`io.Writer` e o pacote `encoding/binary`.
- Noções de TCP: conexão, fluxo de bytes, buffers de envio/recepção.
- Recomendado: desafio-17 (encerramento gracioso) — mas não é pré-requisito.

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), **sem rede externa**: servidor, clientes e
  um proxy que fatia os pacotes conversam pela interface de loopback do
  próprio container. O cache de build fica no volume
  `gitquest-desafio-22-gocache` (remova com
  `docker volume rm gitquest-desafio-22-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25, `make` e um compilador C (o
  race detector usa cgo), rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. O avaliador oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila o código e roda a demonstração. Depois
leia `src/frame/frame.go` e `src/echo/server.go` com o contrato acima em mãos.

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log`: cada teste diz o que
  esperava e o que recebeu.
- `go doc io.Reader` (leia com atenção o que `Read` pode devolver),
  `go doc io.ReadFull`, `go doc io.ErrUnexpectedEOF`,
  `go doc encoding/binary.BigEndian`, `go doc net.Conn` (seção sobre deadlines).
- `testing/iotest` (`OneByteReader`, `HalfReader`, `DataErrReader`) para
  reproduzir leituras parciais em testes seus.
- O race detector (`-race`, já ligado nos testes) e os dumps de goroutines
  que o teste de vazamento imprime.
- Para olhar os bytes no fio: `xxd` / `od -An -tx1` sobre a saída de um
  `WriteFrame` num `bytes.Buffer`.

## Arquivos que você pode modificar

- `src/frame/` — `ReadFrame` e `WriteFrame`.
- `src/echo/` — o servidor. Mantenha a API pública usada pelos testes
  (`Server` com os campos `IdleTimeout`, `WriteTimeout` e `Logger`, `Serve`,
  `Close`, `ErrServerClosed`, `DefaultIdleTimeout`, `DefaultWriteTimeout`;
  `frame.HeaderSize`, `frame.MaxSize`, `frame.ErrTooLarge`). Pode criar
  funções, tipos e arquivos auxiliares. Use apenas a biblioteca padrão.

Não altere `tests/`, `cmd/`, `test.sh`, `challenge.yaml`, `go.mod`,
`Dockerfile` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -race -count=1 -timeout 240s ./...` no
container e imprime um ✔/✘ por critério, com o motivo de cada falha. Os
testes do servidor usam TCP de verdade em `127.0.0.1`, com buffers de socket
reduzidos (~64 KiB) para que o kernel não esconda o que o servidor acumula, e
um proxy que entrega os bytes em pedaços de 1 a 4093 bytes, com pausas.

## Critérios de conclusão

- [ ] eco de mensagens pequenas (o caminho feliz — já funciona)
- [ ] `ReadFrame` monta frames com leituras parciais (1 byte por vez, metade, pedaços aleatórios, dados junto com `io.EOF`)
- [ ] fim limpo do fluxo é `io.EOF`; frame cortado no meio é `io.ErrUnexpectedEOF`
- [ ] cabeçalhos `0xFFFFFFFF`, `0x7FFFFFFF` e `MaxSize+1` viram `ErrTooLarge` alocando menos de 64 KiB; `WriteFrame` recusa payload grande sem escrever nada
- [ ] cabeçalho `uint32` big-endian no fio; mensagens de 64 KiB a 1 MiB chegam intactas
- [ ] servidor ecoa 160 frames (até 1 MiB) fatiados e atrasados por um proxy TCP
- [ ] 3000 pedidos em pipeline: ecos íntegros e na mesma ordem
- [ ] conexão muda e *slowloris* são derrubadas perto do `IdleTimeout`; cliente lento, mas ativo, continua conectado
- [ ] cliente que não lê é contido (menos de 4 MiB aceitos) e os outros clientes seguem atendidos
- [ ] consumidor que não lê é desconectado depois de `WriteTimeout`
- [ ] `Close` fecha as conexões, `Serve` devolve `ErrServerClosed` e nenhuma goroutine vaza
- [ ] nenhum data race; `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *A Linguagem de Programação Go*, Alan A. A. Donovan e Brian W. Kernighan — cap. 7, seção 7.1 (Interfaces como contratos: `io.Reader`/`io.Writer`); cap. 8, seções 8.3 (Servidor de eco concorrente), 8.4.4 (Canais com buffer), 8.7 (Multiplexando com select) e 8.10 (Servidor de bate-papo), com os exercícios 8.13 (desconectar clientes ociosos) e 8.15 (cliente que não lê trava todos os outros).
- *Mastering Go*, 4ª ed., Mihalis Tsoukalos — cap. 7, "Reading from /dev/random" (big endian × little endian com `encoding/binary`) e "Reading a specific amount of data from a file"; cap. 9, "Using SetDeadline()" e "Setting the timeout period on the server side"; cap. 10, "Developing a TCP server with net.Listen()" e "Developing concurrent TCP servers".
- *Sistemas Distribuídos: Princípios e Paradigmas*, 2ª ed., Andrew S. Tanenbaum e Maarten van Steen — cap. 4, seções 4.1 (Fundamentos: protocolos em camadas e a camada de transporte) e 4.3 (Comunicação orientada a mensagem, sobre sockets).
- RFC 9293 — *Transmission Control Protocol (TCP)* (atualiza a RFC 793): seção 2.2 (TCP entrega um fluxo de bytes, sem fronteiras de mensagem) e seção 3.8.6 (gerenciamento da janela: como um receptor lento freia o emissor). https://www.rfc-editor.org/rfc/rfc9293
- Documentação do pacote `io` (`Reader`, `ReadFull`, `ErrUnexpectedEOF`): https://pkg.go.dev/io
- Documentação do pacote `net` (`Conn`: `SetDeadline`, `SetReadDeadline`, `SetWriteDeadline`; `ErrClosed`): https://pkg.go.dev/net#Conn
- Documentação de `testing/iotest`: https://pkg.go.dev/testing/iotest

## Próximo desafio

**desafio-23 — A consulta que varria a tabela** (trilha database): os
frames agora chegam inteiros ao concentrador — e cada pesagem vira uma linha
num banco que começa a responder devagar. Hora de ler planos de execução e
descobrir por que uma consulta inocente percorre a tabela inteira.
