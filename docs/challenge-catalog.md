# Catálogo de desafios

A lista canônica (com pontos, tempo e status) é [`config/challenge-catalog.yaml`](../config/challenge-catalog.yaml);
consulte-a no terminal com `./scripts/gitquest list` e `./scripts/gitquest info desafio-N`.
Este documento organiza os desafios em trilhas. A CI verifica que todo id do catálogo aparece aqui.

Os desafios **não** formam uma sequência única: cada um declara seus pré-requisitos.
Um desafio de Kubernetes pode exigir Docker sem exigir C++.

```text
C++           desafio-1 ──► desafio-2 ──► desafio-4 ──┐
                  │                                    ├──► desafio-5
                  └──────► desafio-3 ──────────────────┘
Estruturas    desafio-6
Go            desafio-7 ──► desafio-8
Docker/K8s    desafio-9 ──► desafio-10
```

## Disponíveis

### Trilha C++

| Desafio | Padrão | Título | Você vai aprender |
|---|---|---|---|
| `desafio-1` | C++11 | O vazamento silencioso | ciclo de vida de objetos, RAII, `std::unique_ptr`, regra dos três/cinco, LeakSanitizer/AddressSanitizer |
| `desafio-2` | C++14 | Lambdas sem fronteiras | generic lambdas, init capture com tipos move-only, dedução de tipo de retorno, `std::make_unique`, variable templates |
| `desafio-3` | C++17 | O valor que não estava lá | `std::optional` no lugar de sentinelas, `std::string_view` sem dangling, `std::variant`, `std::from_chars` |
| `desafio-4` | C++20 | Contratos de template | concepts, `requires`, restrições que transformam bugs silenciosos em erros de compilação |
| `desafio-5` | C++23 | Erros que não se escondem | `std::expected` e operações monádicas, adaptadores de ranges, `std::print`/`std::format` |

### Trilha Estruturas de Dados e Algoritmos

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-6` | A tabela que esquecia | endereçamento aberto, tombstones, rehash, fator de carga, qualidade da função de hash, teste diferencial e de desempenho |

### Trilha Go

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-7` | O pool que transbordou | worker pool com generics, concorrência limitada, ordem de resultados, `go test -race`, vazamento de goroutines |
| `desafio-8` | A requisição que não sabia parar | `context.Context`, deadlines, retry com backoff exponencial, `Retry-After`, fechamento de corpos HTTP, `errors.Is` |

### Trilha Docker e Kubernetes

| Desafio | Título | Você vai aprender |
|---|---|---|
| `desafio-9` | A imagem de um gigabyte | multi-stage build, imagens distroless, usuário não-root, `HEALTHCHECK`, `.dockerignore`, segredos fora da imagem, cache de camadas |
| `desafio-10` | O Deployment que nunca ficou pronto | `kubectl describe/logs/events`, ImagePullBackOff, CrashLoopBackOff, OOMKilled, probes, selectors de Service, requests/limits, securityContext |

## Roteiro (planejados)

| Desafio | Trilha | Título |
|---|---|---|
| `desafio-11` | C++ | Move semantics sem cópias escondidas |
| `desafio-12` | C++ | Corrotinas que geram |
| `desafio-13` | C++ | jthread e stop tokens |
| `desafio-14` | Estruturas de dados | Fila circular sem alocação |
| `desafio-15` | Estruturas de dados | Heap de prioridades |
| `desafio-16` | Estruturas de dados | O caminho mais curto |
| `desafio-17` | Go | Desligamento gracioso |
| `desafio-18` | Kubernetes | Probes que mentem |
| `desafio-19` | Kubernetes | Rollout e rollback |
| `desafio-20` | Bancos de dados e arquitetura | O consumidor que processava duas vezes |

## Áreas ainda sem desafios

Linux/sistemas operacionais (processos, sinais, pipes, file descriptors, sanitizers, CMake),
redes (framing de protocolos binários, timeouts, backpressure, DNS), bancos de dados
(índices, isolamento de transações, deadlocks, Redis, Kafka/RabbitMQ, migrações),
Kubernetes avançado (RBAC, NetworkPolicy, HPA, Helm, Kustomize) e C++26 (reflection e
contracts quando houver suporte real em compiladores). Veja
[contributing-challenges.md](contributing-challenges.md) para propor um.
