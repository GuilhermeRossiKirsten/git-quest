# GitQuest: The Software Engineering Challenge

Um laboratório prático de engenharia de software em que **o Git organiza a progressão,
o código apresenta os problemas, os ambientes permitem experimentar e os testes comprovam
o aprendizado.**

Cada desafio é uma branch `desafio-N` independente, com código inicial com problemas
reais, um ambiente reproduzível (Docker, Kind), testes automatizados e critérios objetivos
de conclusão. A branch `main` (esta) contém o catálogo, a documentação e as ferramentas.

```text
main ─────────── catálogo, documentação, CLI, validador, template, CI
desafio-1 ────── C++11 · O vazamento silencioso
desafio-2 ────── C++14 · Lambdas sem fronteiras
...
desafio-36 ───── Arquitetura (Go) · O pedido que aceitava tudo
```

## Começo rápido

```bash
git clone <URL_DO_REPOSITORIO> gitquest
cd gitquest

./scripts/check-environment.sh     # o que está instalado e quais trilhas estão liberadas
./scripts/gitquest list            # catálogo
./scripts/gitquest info desafio-1  # objetivos, pré-requisitos, ambiente

./scripts/gitquest start desafio-1 # = git switch desafio-1 (nunca descarta alterações)
cat README.md                      # o enunciado do desafio
./start.sh                         # prepara o ambiente (idempotente)
./scripts/gitquest test            # roda os testes e registra seu progresso
./scripts/gitquest hints 1         # dicas progressivas (1, 2, 3...)
git add -A && git commit -m "resolve desafio-1"
./scripts/gitquest status          # branch, alterações e pontuação
```

Requisitos mínimos: Git >= 2.23, Bash >= 4 e Python >= 3.8 (só biblioteca padrão).
Os toolchains (GCC 14.2, Go 1.25, PostgreSQL 16, Redis 7.4, Kind) rodam em containers com versões fixadas, então o
**Docker** é o único requisito pesado. Windows: use **WSL2** (recomendado) ou Git Bash —
veja [docs/getting-started.md](docs/getting-started.md).

## Catálogo

| Desafio | Trilha | Nível | Tema |
|---|---|---|---|
| `desafio-1` | C++11 | iniciante | RAII, `std::unique_ptr`, regra dos três/cinco, LeakSanitizer |
| `desafio-2` | C++14 | intermediário | generic lambdas, init capture, dedução de retorno |
| `desafio-3` | C++17 | intermediário | `std::optional`, `std::string_view`, `std::variant` |
| `desafio-4` | C++20 | intermediário | concepts e constraints |
| `desafio-5` | C++23 | avançado | `std::expected`, ranges, `std::print` |
| `desafio-6` | Estruturas de dados | intermediário | hash table com endereçamento aberto |
| `desafio-7` | Go | intermediário | worker pool, race detector, vazamento de goroutines |
| `desafio-8` | Go | intermediário | `context`, deadlines, retry com backoff |
| `desafio-9` | Docker | intermediário | multi-stage, não-root, health check, segredos |
| `desafio-10` | Kubernetes | avançado | diagnóstico de Deployment no Kind |
| `desafio-11` | C++11 | intermediário | move semantics, `noexcept`, NRVO |
| `desafio-12` | C++23 | avançado | `std::generator`, corrotinas preguiçosas |
| `desafio-13` | C++20 | avançado | `std::jthread`, stop tokens, ThreadSanitizer |
| `desafio-14` | Estruturas de dados | iniciante | ring buffer sem alocação |
| `desafio-15` | Estruturas de dados | intermediário | heap indexado com decrease-key |
| `desafio-16` | Estruturas de dados | avançado | BFS, Dijkstra, Union-Find |
| `desafio-17` | Go | intermediário | graceful shutdown e middlewares HTTP |
| `desafio-18` | Kubernetes | intermediário | liveness, readiness e startup probes |
| `desafio-19` | Kubernetes | avançado | rollout sem indisponibilidade e rollback |
| `desafio-20` | Banco de dados | avançado | outbox, consumidor idempotente, PostgreSQL |
| `desafio-21` | Linux (C17) | intermediário | fork/exec, sinais, zumbis, pipes, FDs herdados |
| `desafio-22` | Redes (Go) | intermediário | framing sobre TCP, leituras parciais, backpressure |
| `desafio-23` | Banco de dados (SQL) | intermediário | índices, `EXPLAIN`, paginação por keyset |
| `desafio-24` | Banco de dados (Go) | avançado | cache-aside em Redis, stampede, TTL com jitter |
| `desafio-25` | Kubernetes | avançado | RBAC de menor privilégio, NetworkPolicy |
| `desafio-26` | C++20 | avançado | `std::pmr`, arenas, alinhamento |
| `desafio-27` | CMake | intermediário | targets, requisitos de uso, install/export |
| `desafio-28` | C++20 | intermediário | type erasure no lugar de `switch` por tipo |
| `desafio-29` | C++20 | avançado | unidades físicas em tempo de compilação |
| `desafio-30` | Desempenho (C++20) | avançado | localidade de cache, AoS × SoA, cachegrind |
| `desafio-31` | Compiladores (Go) | intermediário | lexer, parser descendente, precedência |
| `desafio-32` | Segurança (C17) | avançado | parsing defensivo, overflow em tamanhos, fuzzing |
| `desafio-33` | C++20 | avançado | fila SPSC lock-free, memory orders, false sharing |
| `desafio-34` | Arquitetura (Go) | intermediário | paginação, ETag/`If-Match`, idempotência |
| `desafio-35` | Distribuídos (Go) | avançado | relógios de Lamport e vetoriais, entrega causal |
| `desafio-36` | Arquitetura (Go) | intermediário | DDD: agregados, value objects, eventos |

Catálogo completo, trilhas e roteiro: [docs/challenge-catalog.md](docs/challenge-catalog.md).
Os desafios não exigem ordem linear; os pré-requisitos são explícitos.

## Comandos

| Comando | O que faz |
|---|---|
| `gitquest list [--track T]` | lista os desafios (✔ nos concluídos) |
| `gitquest info desafio-N` | metadados, objetivos, pré-requisitos e ambiente |
| `gitquest start desafio-N` | troca para a branch; se houver alterações locais, para e explica como guardá-las |
| `gitquest test` | executa `./test.sh` do desafio atual; ao passar, registra pontos e conquistas |
| `gitquest hints [nível]` | mostra as dicas até o nível pedido |
| `gitquest status` | branch, estado do Git, arquivos alterados fora do escopo, pontuação |
| `gitquest progress [--markdown]` | relatório por trilha: concluídos, pontos e próximo desafio desbloqueado |
| `gitquest validate` | valida o contrato do desafio (para autores) |

Saída: `0` sucesso, `1` falha, `2` uso incorreto. O progresso fica em
`.git/gitquest/progress.tsv` — local, fora do controle de versão e válido em todas as branches.

## Documentação

- [Primeiros passos](docs/getting-started.md) — Linux, macOS, Windows/WSL2
- [Arquitetura](docs/architecture.md) — decisões, branches órfãs, ferramentas vendorizadas, riscos
- [Formato de um desafio](docs/challenge-format.md) — contrato e schema do `challenge.yaml`
- [Catálogo](docs/challenge-catalog.md) — trilhas e roteiro
- [Criar desafios](docs/contributing-challenges.md) — passo a passo para colaboradores
- [Segurança](docs/security.md) — isolamento, limites, CI com código não confiável
- [Solução de problemas](docs/troubleshooting.md)
- [Bibliografia](docs/bibliography.md) — livros de referência por trilha

## Licença

[MIT](LICENSE).
