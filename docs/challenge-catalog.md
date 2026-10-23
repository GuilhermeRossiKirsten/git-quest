# Catálogo de desafios

A lista canônica (com pontos, tempo e status) é [`config/challenge-catalog.yaml`](../config/challenge-catalog.yaml);
consulte-a no terminal com `./scripts/gitquest list` e `./scripts/gitquest info desafio-N`.
Este documento organiza os desafios em trilhas. A CI verifica que todo id do catálogo aparece aqui.

Os desafios **não** formam uma sequência única: cada um declara seus pré-requisitos.
Um desafio de Kubernetes pode exigir Docker sem exigir C++.

```text
C++           desafio-1 ──┬──► desafio-2 ──► desafio-4 ──┬──► desafio-5
                          │                              ├──► desafio-12 (corrotinas)
                          │                              └──► desafio-13 (jthread)
                          ├──► desafio-3 ────────────────────► desafio-5
                          └──► desafio-11 (move semantics)
Estruturas    desafio-6        desafio-14 ──► desafio-15 ──► desafio-16
Go            desafio-7 ──► desafio-8 ──► desafio-17
Docker/K8s    desafio-9 ──► desafio-10 ──┬──► desafio-18 (probes)
                                         └──► desafio-19 (rollout) ──► desafio-25 (RBAC/NetworkPolicy)
Banco/Arq.    desafio-9 ──► desafio-20 (outbox e idempotência)
              desafio-23 (índices) ──► desafio-24 (cache Redis)
Linux         desafio-21 (processos e sinais)
Redes         desafio-22 (framing sobre TCP)
```

## Disponíveis

### Trilha C++

| Desafio | Padrão | Título | Você vai aprender |
|---|---|---|---|
| `desafio-1` | C++11 | O vazamento silencioso | ciclo de vida de objetos, RAII, `std::unique_ptr`, regra dos três/cinco, LeakSanitizer/AddressSanitizer |
| `desafio-11` | C++11 | Move semantics sem cópias escondidas | rvalue references, supressão do move implícito, `noexcept` e realocação de `std::vector`, NRVO, parâmetros "sink" |
| `desafio-2` | C++14 | Lambdas sem fronteiras | generic lambdas, init capture com tipos move-only, dedução de tipo de retorno, `std::make_unique`, variable templates |
| `desafio-3` | C++17 | O valor que não estava lá | `std::optional` no lugar de sentinelas, `std::string_view` sem dangling, `std::variant`, `std::from_chars` |
| `desafio-4` | C++20 | Contratos de template | concepts, `requires`, restrições que transformam bugs silenciosos em erros de compilação |
| `desafio-13` | C++20 | jthread e stop tokens | `std::jthread`, `std::stop_token`, `condition_variable_any`, encerramento cooperativo, ThreadSanitizer |
| `desafio-5` | C++23 | Erros que não se escondem | `std::expected` e operações monádicas, adaptadores de ranges, `std::print`/`std::format` |
| `desafio-12` | C++23 | Corrotinas que geram | `std::generator`, avaliação preguiçosa, RAII em corrotinas, `std::ranges::elements_of` |

### Trilha Estruturas de Dados e Algoritmos

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-14` | Fila circular sem alocação | ring buffer de capacidade fixa, cheia × vazia, wrap-around, armazenamento sem heap |
| `desafio-15` | Heap de prioridades | binary heap indexado, decrease/increase-key, remoção arbitrária, desempate estável |
| `desafio-6` | A tabela que esquecia | endereçamento aberto, tombstones, rehash, fator de carga, qualidade da função de hash |
| `desafio-16` | O caminho mais curto | BFS, Dijkstra com descarte de entradas obsoletas, Union-Find com compressão e união por tamanho, overflow |

### Trilha Go

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-7` | O pool que transbordou | worker pool com generics, concorrência limitada, ordem de resultados, `go test -race`, vazamento de goroutines |
| `desafio-8` | A requisição que não sabia parar | `context.Context`, deadlines, retry com backoff exponencial, `Retry-After`, fechamento de corpos HTTP, `errors.Is` |
| `desafio-17` | Desligamento gracioso | `http.Server.Shutdown`, sinais, readiness durante o drain, middlewares (recover, timeout, request-id), journal íntegro |

### Trilha Docker e Kubernetes

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-9` | A imagem de um gigabyte | multi-stage build, imagens distroless, usuário não-root, `HEALTHCHECK`, `.dockerignore`, segredos fora da imagem, cache de camadas |
| `desafio-10` | O Deployment que nunca ficou pronto | `kubectl describe/logs/events`, ImagePullBackOff, CrashLoopBackOff, OOMKilled, probes, selectors de Service, requests/limits, securityContext |
| `desafio-18` | Probes que mentem | liveness × readiness × startup probes, warmup lento, dependência fora do ar sem reinícios em cascata |
| `desafio-19` | Rollout e rollback | RollingUpdate sem indisponibilidade, `minReadySeconds`, `progressDeadlineSeconds`, histórico de revisões, PodDisruptionBudget |
| `desafio-25` | Quem pode o quê | RBAC de menor privilégio (`kubectl auth can-i`, `resourceNames`), ServiceAccounts dedicadas, `automountServiceAccountToken`, NetworkPolicy deny-by-default com DNS liberado |

### Trilha Bancos de Dados e Arquitetura

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-20` | O consumidor que processava duas vezes | consumidor idempotente, transactional outbox, escrita dupla, `INSERT ... ON CONFLICT`, UPDATE atômico, PostgreSQL real via Compose |
| `desafio-23` | A consulta que varria a tabela | índices compostos, parciais e de expressão, consultas sargable, `EXPLAIN (ANALYZE, BUFFERS)`, paginação por keyset, `NOT IN` com NULL |
| `desafio-24` | O cache que mentia | cache-aside em Redis, invalidação e a corrida de repovoamento (leases), stampede, TTL com jitter, cache negativo, degradação com Redis fora |

### Trilha Linux

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-21` | O processo que não morria | `fork`/`exec`, `waitpid` e zumbis, subreaper, pipes e EOF, `O_CLOEXEC`, sinais com self-pipe, grupos de processos (C17) |

### Trilha Redes

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-22` | Mensagens pela metade | framing length-prefix sobre TCP, leituras parciais (`io.ReadFull`), limite de frame, deadlines por operação, backpressure, encerramento sem vazar goroutines |

## Roteiro (em construção)

| Desafio | Trilha | Título |
|---|---|---|
| `desafio-26` | cpp | A arena que não devolvia |
| `desafio-27` | cpp | Só compila na minha máquina |
| `desafio-28` | cpp | O switch que crescia |
| `desafio-29` | cpp | Metros mais segundos |
| `desafio-30` | performance | Dados que não cabiam no cache |
| `desafio-31` | compilers | A precedência esquecida |
| `desafio-32` | security | O pacote que derrubava o coletor |
| `desafio-33` | cpp | A fila sem trava que travava |
| `desafio-34` | architecture | A API que quebrava os clientes |
| `desafio-35` | distributed | Relógios que discordavam |
| `desafio-36` | architecture | O pedido que aceitava tudo |

## Áreas ainda sem desafios

Candidatas:

Mensageria (Kafka/RabbitMQ), DDD e Clean Architecture, isolamento de transações e
deadlocks, Helm/Kustomize/HPA, profiling e benchmarks em Go, CMake e linking, e C++26
(reflection e contracts quando houver suporte real em compiladores). Veja
[contributing-challenges.md](contributing-challenges.md) para propor um.
