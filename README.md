# desafio-26 · A arena que não devolvia

> Trilha: cpp · Padrão: **C++20** · Toolchain: **gcc:14.2.0** · Nível: advanced · ~75 min · 250 pontos

## Contexto

O indexador de logs da plataforma recebe lotes de centenas de milhares de linhas por minuto
e, para cada uma, guardava mensagem, serviço e contadores em `std::vector<std::string>` e
`std::map`. O perfilador mostrava `malloc` e `free` no topo: milhões de alocações pequenas,
fragmentação e latência imprevisível.

Alguém resolveu "de uma vez por todas": escreveu uma **arena** caseira (um *bump allocator*
que pede blocos grandes e entrega pedaços), embrulhou-a num `std::pmr::memory_resource` e
trocou os contêineres por `std::pmr`. O benchmark ficou lindo. Em produção, o serviço
passou a cair de vez em quando — e só quando sharding e relatórios entram em cena.

Sua missão: manter a ideia (arena + `std::pmr`), mas fazer com que ela seja **correta**.

## Problema

O código compila e, em casos simples, até responde certo. O que os testes mostram:

- `allocate(n, 16)` e `allocate(n, 64)` devolvem endereços que **não são múltiplos** do
  alinhamento pedido; o UBSan reclama de *misaligned address* para os contadores
  `alignas(64)` do índice e para um `std::pmr::vector` de um tipo superalinhado;
- objetos criados com `Arena::make<T>()` **nunca são destruídos**: contadores de vida
  continuam positivos depois de `reset()` e o LeakSanitizer acusa vazamentos;
- duas `ArenaResource` de arenas diferentes se dizem "iguais";
- mover um contêiner `pmr` para outro de arena diferente e destruir a arena de origem termina
  em **heap-use-after-free** — o mesmo acontece ao absorver um *shard* com `absorb()`;
- depois de trocar tudo para `std::pmr`, o `ingest()` ainda faz **milhares de alocações
  no heap global** (contadas num `operator new` substituído);
- o relatório de `top_services()` fica corrompido (use-after-free) quando você faz outra
  consulta ou destrói o índice, e não usa o `memory_resource` que o chamador passou.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Escrever uma arena que respeita alinhamento, inclusive de tipos superalinhados.
- Separar *memória crua* de *tempo de vida de objetos* numa arena.
- Implementar `std::pmr::memory_resource` com `do_is_equal` correto e entender o que
  "allocators iguais" autorizam um contêiner a fazer.
- Fazer o resource **propagar** para elementos aninhados e evitar temporários no heap global.
- Escolher em qual resource cada resultado deve viver.
- Medir alocações e diagnosticar erros de memória com ASan, UBSan e LSan.

## Conhecimentos necessários

- Desafio-11 (semântica de movimento, `noexcept`, contagem de cópias com `operator new`).
- `alignof`/`alignas`, `std::max_align_t`, *placement new* e chamada explícita de destrutor.
- Noções de `std::pmr::polymorphic_allocator`, `memory_resource` e `monotonic_buffer_resource` (C++17).

## Ambiente

- Padrão exigido: **C++20** (`-std=c++20 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, libstdc++, ASan/UBSan/LSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`.

Tudo compila com `-fsanitize=address,undefined` e cada teste roda em um processo próprio,
dentro de um container sem rede e com limites de CPU, memória e tempo.

**Como as alocações são medidas:** o binário de testes substitui todas as formas do
`operator new` global e conta as chamadas feitas durante a operação medida. O *upstream* das
arenas nos testes é um resource próprio (`tests/support/resources.h`) que usa `malloc`
diretamente: ele **não** entra nessa conta, mas conta quantos blocos a arena pediu, e devolve
a memória com `free` — por isso o ASan acusa qualquer acesso a um bloco já devolvido.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial
./test.sh           # mostra quais critérios falham e por quê
```

Para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                         # log completo de cada teste que falhar
cat build/logs/index_absorb_shards.log         # relatório do ASan: onde leu e onde liberou
./build/test-runner --list                     # nomes dos testes
./build/test-runner arena_alinhamento_64       # roda um teste só (dentro do container)
```

Ferramentas úteis: o relatório do AddressSanitizer (a pilha "freed by thread" diz **quem**
liberou), as mensagens `runtime error: ... misaligned address` do UBSan, o contrato escrito
nos headers de `src/` e `std::pmr::polymorphic_allocator<T>::resource()` para descobrir onde
cada contêiner está alocando.

## Arquivos que você pode modificar

- `src/arena.h` e `src/arena.cpp` — a arena sequencial;
- `src/arena_resource.h` e `src/arena_resource.cpp` — o `memory_resource` sobre a arena;
- `src/log_index.h` e `src/log_index.cpp` — o índice (ingest, absorb, consultas);
- `src/log_line.h` e `src/log_line.cpp` — o parser de linhas (está correto; mexa se quiser).

Mantenha as assinaturas públicas usadas pelos testes e o contrato descrito nos comentários
dos headers. Não altere `tests/`, `test.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`:
o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada critério aparece com ✔ ou ✘ e, quando falha, o motivo: o endereço desalinhado, a
contagem de alocações contra o orçamento, a verificação que falhou ou o erro de memória com a
linha de `src/` onde ele aconteceu e onde a memória foi liberada.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o projeto compila com g++ 14 em `-std=c++20 -pedantic-errors`;
- [ ] `Arena::allocate` respeita alinhamentos de 2 a 4096 bytes (inclusive `alignas(64)` e pedidos maiores que um bloco), sem sobrepor pedaços;
- [ ] objetos criados com `make<T>()` são destruídos em ordem inversa em `reset()` e no destrutor, sem vazamentos;
- [ ] a arena pede memória ao upstream em blocos (orçamento de chamadas) e devolve tudo em `reset()`;
- [ ] `ArenaResource` só é igual a si mesma; mover contêineres `pmr` entre arenas não deixa ponteiros pendurados;
- [ ] o índice responde igual a uma implementação de referência ingênua (inclusive linhas malformadas);
- [ ] `ingest()` de 20 mil linhas e `absorb()` de shards: **0 alocações no heap global**; shards podem ser destruídos depois de absorvidos;
- [ ] `top_services(k, out)`: ranking correto, vetor e strings em `out`, 0 alocações globais, e o resultado sobrevive a novas consultas e à destruição do índice.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++ Memory Management*, Patrice Roy — cap. 1 "Objects, Pointers, and References" (seção sobre alinhamento, `alignof`/`alignas`); cap. 7 "Overloading Memory Allocation Operators", seção "Alignment-aware versions of the allocation operators" (tipos superalinhados e `std::align_val_t`); cap. 10 "Arena-Based Memory Management and Other Optimizations" (a arena gerencia memória crua, não objetos; "Chunked pools" e por que blocos sem cuidado com alinhamento geram objetos desalinhados); cap. 14 "Writing Generic Containers with Allocator Support", seções "Polymorphic memory resource allocators", "Nested allocators" e "Allocators and data collection" (`do_is_equal`, propagação para `pmr::string`, upstream e `null_memory_resource`).
- *C++ High Performance* (2ª ed.), Björn Andrist e Viktor Sehr — cap. 7 "Memory Management": seções sobre alinhamento (`std::align`, `alignas(64)`), "Arena" e "Using polymorphic memory allocators"/"Implementing a custom memory resource" (inclusive o alerta sobre devolver um `pmr::vector` cujo resource morreu).
- cppreference: [`std::pmr::memory_resource`](https://en.cppreference.com/w/cpp/memory/memory_resource), [`polymorphic_allocator`](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator), [`monotonic_buffer_resource`](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource), [uses-allocator construction](https://en.cppreference.com/w/cpp/memory/uses_allocator), [`std::align`](https://en.cppreference.com/w/cpp/memory/align).

## Próximo desafio

**desafio-33 — A fila sem trava que travava**: baixa latência com uma fila SPSC lock-free,
*memory orders* corretos e sem *false sharing* — o mesmo `alignas(64)` que você acabou de
fazer a arena respeitar, agora validado com TSan e testes de estresse.
