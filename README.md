# desafio-35 · Relógios que discordavam

> Trilha: distributed · Nível: advanced · ~75 min · 250 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`), só a biblioteca padrão

## Contexto

O **Fórum da Vila** — perguntas, respostas, pedidos de mutirão e o contador de
curtidas mais disputado do bairro — roda num KV replicado caseiro (`src/kv`):
três a cinco réplicas, cada uma aceita escritas localmente e propaga as
alterações para as outras de forma assíncrona. Ninguém coordena nada; "o NTP
cuida dos relógios", diz o comentário no código.

Desde que a terceira réplica foi para um servidor emprestado na escola, a
moderação vive abrindo chamados. Alguém começou a migrar o sistema para
relógios lógicos (`src/clock`) — o código está lá, os carimbos viajam nas
mensagens — mas a migração parou no meio.

Para investigar sem depender da sorte, o time escreveu uma **rede simulada**
(`sim/`): nada de sockets nem de relógio de parede de verdade. Uma semente
decide os atrasos de cada mensagem (e, portanto, a reordenação), quais
mensagens chegam duplicadas e quanto o relógio físico de cada réplica está
desviado. A mesma semente reproduz exatamente a mesma história.

## Problema

Sintomas relatados (rode `./start.sh`: a demonstração em `cmd/demo` reproduz
cada um e mostra *esperado* × *obtido*):

- Um usuário lê um pedido "aberto" e o cancela. Minutos depois, duas das três
  réplicas mostram o pedido **aberto de novo** — e só uma mostra "cancelado".
- Há leitores que veem a **resposta "Está no telhado!" sem a pergunta**
  "Alguém viu meu gato?".
- O contador de curtidas **passa da quantidade de cliques** — e cada réplica
  mostra um número diferente.
- Duas pessoas trocam o tema ao mesmo tempo; depois que tudo se acalma, as
  réplicas **não concordam** sobre qual tema ficou.
- Os testes unitários dos relógios lógicos que alguém começou a escrever não
  batem com o que o livro diz.

Seu trabalho é fazer o KV cumprir o contrato abaixo — investigando e
corrigindo o código existente, não reescrevendo o serviço.

**Contrato da rede (`sim/`, do avaliador — não altere)**

- Mensagens não se perdem, mas podem chegar **fora de ordem** e **duplicadas**.
- `Endpoint.WallClock()` é o relógio físico da réplica, **com desvio** (pode
  estar minutos adiantado). `time.Now()` não tem relação com o tempo simulado.

**Contrato dos relógios (`src/clock`)**

- `Lamport`: `Tick()` registra um evento local e devolve o carimbo;
  `Observe(remoto)` registra um recebimento: o relógio passa a
  `max(local, remoto) + 1`.
- `Vector`: `Compare(w)` devolve `Before` se `v → w` (aconteceu antes),
  `After` se `w → v`, `Equal` se iguais e `Concurrent` se nenhum precede o
  outro. `Merge` faz o máximo posição a posição; `Clone` copia.

**Contrato do KV (`src/kv`)**

- `Put`, `Add`, `Get` e `Counter` respondem com o estado local da réplica e
  são seguros para uso concorrente; `Receive` é chamado pela rede.
- **Causalidade:** uma réplica só torna visível uma escrita depois de todas
  as escritas que a réplica de origem já tinha visto ao fazê-la. Mensagem que
  chega "cedo demais" fica retida até suas dependências chegarem.
- **Idempotência:** uma escrita entregue mais de uma vez tem efeito uma vez.
- **Conflitos:** para a mesma chave, vence a escrita de maior
  **(carimbo de Lamport, ID da réplica de origem)** — comparando primeiro o
  carimbo e, em empate, o ID. Escrita causalmente posterior sempre vence;
  entre concorrentes, o resultado é o mesmo em todas as réplicas,
  qualquer que seja a ordem de chegada ou o relógio físico.
- **Convergência:** quando a rede fica quieta, todas as réplicas mostram o
  mesmo estado, e nenhuma mensagem fica retida para sempre.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Explicar por que relógios físicos com desvio não servem para ordenar escritas causalmente relacionadas.
- Implementar o relógio de Lamport, inclusive a regra de recebimento `max(local, remoto) + 1`.
- Comparar relógios vetoriais distinguindo "aconteceu antes" de concorrência.
- Implementar entrega causal com relógios vetoriais e um buffer de retenção.
- Tornar a aplicação de mensagens idempotente numa rede que duplica entregas.
- Resolver escritas concorrentes com uma ordem total determinística (Lamport, ID) para garantir convergência.
- Usar testes de propriedade com muitas sementes e reproduzir uma falha a partir da semente.

## Conhecimentos necessários

- Go: structs, slices, maps, `sync.Mutex`, `encoding/json`.
- A relação "aconteceu antes" de Lamport e a ideia de relógio lógico (o
  Tanenbaum, cap. 6, é uma boa leitura de aquecimento).
- Noções de replicação e consistência (consistência causal e eventual).
- Recomendado: desafio-22 (mensagens pela rede: nada chega como foi enviado).

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), **sem rede**: a rede do desafio é toda
  simulada em memória. O cache de build fica no volume
  `gitquest-desafio-35-gocache` (remova com
  `docker volume rm gitquest-desafio-35-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25, `make` e um compilador C (o
  race detector usa cgo), rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. O avaliador oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila o código e roda a demonstração. Depois
leia, nesta ordem, `src/clock/lamport.go`, `src/clock/vector.go`,
`src/kv/message.go` e `src/kv/replica.go`, com o contrato acima em mãos. A
rede em `sim/network.go` é curta e vale a leitura: é ela que decide o que
chega, quando e quantas vezes.

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log`: cada falha mostra o que
  esperava, o que obteve e os últimos eventos (escritas, leituras, entregas).
- **Reprodução por semente:** os testes de propriedade dizem qual semente
  falhou. `GQ_SEED=137 ./test.sh` roda só aquela semente e grava o diário
  completo dela em `build/test.log` — a mesma história, toda vez.
- Desenhe a história num papel: uma linha do tempo por réplica, setas para as
  mensagens, e os carimbos (físico, Lamport, vetor) em cada evento.
- O race detector (`-race`, já ligado nos testes).

## Arquivos que você pode modificar

- `src/clock/` — os relógios de Lamport e vetorial.
- `src/kv/` — a réplica e o formato das mensagens. Mantenha a API pública
  usada pelos testes (`NewReplica(sim.Endpoint)`, `Put`, `Add`, `Get`,
  `Counter`, `Receive`; `clock.Lamport` com `Now/Tick/Observe`,
  `clock.Vector` com `NewVector/Clone/Increment/Merge/Compare` e as
  constantes de `clock.Order`). Pode criar funções, tipos e arquivos
  auxiliares. Use apenas a biblioteca padrão.

Não altere `sim/`, `tests/`, `cmd/`, `test.sh`, `challenge.yaml`, `go.mod`,
`Dockerfile` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
GQ_SEED=137 ./test.sh        # reproduz uma semente (não conta como conclusão)
```

Roda `go vet ./...` e `go test -race -count=1 -timeout 240s ./...` no
container e imprime um ✔/✘ por critério, com o motivo de cada falha e a
semente que a provocou. Os testes de propriedade usam 500 sementes, com 3 a 5
réplicas, atrasos de até 200 ms, até 50% de mensagens duplicadas e relógios
desviados em até ±3 s.

## Critérios de conclusão

- [ ] um escritor, relógios certos: tudo replica (o caminho feliz — já funciona)
- [ ] relógio de Lamport: `Tick` conta eventos; recebimento salta para `max(local, remoto)+1`
- [ ] relógio vetorial: `Compare` distingue antes, depois, igual e concorrente (tabela + 200 histórias aleatórias)
- [ ] réplica com relógio adiantado (30 ms a 1 min) não vence uma escrita causalmente posterior
- [ ] resposta que chega antes da pergunta fica retida até a pergunta chegar — e então aparece
- [ ] escritas concorrentes: o mesmo vencedor em todas as réplicas, pela ordem (Lamport, ID)
- [ ] mensagem entregue duas vezes é aplicada uma vez
- [ ] 500 sementes: todas as réplicas convergem para o mesmo estado, e a mesma semente dá o mesmo estado
- [ ] 500 sementes: nenhum leitor vê efeito sem causa (verificado depois de cada entrega); edição posterior nunca se perde; nada fica retido
- [ ] 500 sementes com duplicação: nenhum contador soma duas vezes, nem por um instante
- [ ] clientes concorrentes e a rede ao mesmo tempo: resultado certo e nenhum data race
- [ ] `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Sistemas Distribuídos: Princípios e Paradigmas*, 2ª ed., Andrew S. Tanenbaum e Maarten van Steen (Pearson)
  — os números de seção e página abaixo vêm do sumário; os nomes de subseção (6.2.1, 6.2.2, 7.3.1) não
  puderam ser conferidos no PDF disponível ao autor do desafio (só sumário e cap. 1), então confira-os
  num exemplar completo:
  - cap. 1, seção 1.2 (Metas) — "Problemas de escalabilidade", p. 6: algoritmos descentralizados não presumem um relógio global ("é impossível conseguir a exata sincronização de todos os relógios"); e as premissas falsas de quem projeta o primeiro sistema distribuído ("a rede é confiável", "a latência é zero"…), pp. 9–10;
  - cap. 6, seção 6.1 (Sincronização de relógios, p. 140): por que relógios físicos derivam e só podem ser sincronizados aproximadamente;
  - cap. 6, seção 6.2 (Relógios lógicos, p. 147): 6.2.1 "Relógios lógicos de Lamport" (a relação "acontece antes", a regra de recebimento e o desempate pelo número do processo; exemplo do multicast totalmente ordenado) e 6.2.2 "Relógios vetoriais" (concorrência e "imposição de comunicação causalmente ordenada": a condição de entrega e a retenção de mensagens);
  - cap. 7, seção 7.2 (Modelos de consistência centrados em dados, p. 167): consistência causal, em "Ordenação consistente de operações";
  - cap. 7, seção 7.3 (Modelos de consistência centrados no cliente, p. 174): 7.3.1 "Consistência eventual".
- Leslie Lamport, "Time, Clocks, and the Ordering of Events in a Distributed System", *Communications of the ACM* 21(7), 1978. https://lamport.azurewebsites.net/pubs/time-clocks.pdf
- K. Birman, A. Schiper e P. Stephenson, "Lightweight Causal and Atomic Group Multicast", *ACM Transactions on Computer Systems* 9(3), 1991 — a origem da condição de entrega causal com vetores.
- Documentação de `math/rand/v2` (geradores com semente, `PCG`): https://pkg.go.dev/math/rand/v2

## Próximo desafio

Este é, por enquanto, o último desafio da trilha **distributed**. Se quiser
ir além: troque o desempate (Lamport, ID) por *siblings* — guardar todas as
versões concorrentes e deixar a aplicação escolher — e veja quais testes
precisariam mudar.
