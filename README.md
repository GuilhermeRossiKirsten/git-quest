# desafio-33 · A fila sem trava que travava

> Trilha: cpp · Padrão: **C++20** · Toolchain: **gcc:14.2.0** · Nível: advanced · ~90 min · 300 pontos

## Contexto

A mesa de arbitragem tem um feed de mercado escrito em C++: uma thread lê o multicast da
bolsa e decodifica as cotações, e outra, a da estratégia, decide se envia ordens. Entre as
duas há uma `SpscQueue` — uma fila **sem trava** (lock-free) para **um** produtor e **um**
consumidor, de capacidade fixa, sem mutex e sem alocação por mensagem. O autor original a
escreveu numa sexta-feira à tarde, os testes "passaram" no x86 da corretora e ela foi para
produção.

Desde então:

- a operação configurou a fila com **1000** posições (o número que coube na planilha de
  latência) e o produtor passa a maior parte do tempo encontrando a fila **cheia**;
- quem tentou uma fila de capacidade 1 para um canal de controle descobriu que ela
  **nunca aceita nada**;
- o processo da estratégia **cresce de memória** ao longo do pregão;
- no piloto em servidores **ARM**, a estratégia registrou cotações com o símbolo de uma e o
  preço de outra — "impossível", segundo o autor, "o código só usa `std::atomic`";
- a latência p99 piora muito quando as duas threads rodam em núcleos diferentes, e melhora
  misteriosamente quando o SO as coloca no mesmo núcleo.

Sua missão: deixar a fila **correta pelo modelo de memória do C++** (não "correta no x86"),
com ciclo de vida exato dos elementos, capacidade exata e um layout que não faça os dois
núcleos brigarem pela mesma linha de cache — mantendo a API que o feed usa.

## Problema

Rodando `./test.sh`, você vai observar:

- uma fila de capacidade `C` aceita **menos** que `C` itens — para algumas capacidades,
  nenhum — e `SpscQueue(0)` é aceita sem reclamar;
- depois de `try_pop()`, continuam **vivos** mais objetos do que há na fila, e destruir uma
  fila que ainda tem itens deixa objetos vivos e **vazamentos** acusados pelo
  LeakSanitizer;
- o índice instrumentado acusa escritas e leituras de índice com `memory_order_relaxed` e
  um lado relendo o índice do outro **a cada operação**;
- os dois índices aparecem **a 8 bytes um do outro**, na mesma linha de cache de 64 bytes;
- com duas threads de verdade, o **ThreadSanitizer** relata `data race` entre `try_emplace`
  e `try_pop` (e entre o construtor e o construtor de movimento de `Quote`), embora a
  ordem e o checksum estejam certos no x86;
- no estresse com objetos não triviais, a fila **recusa itens mesmo vazia** e sobram objetos
  vivos depois que ela é destruída.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Explicar por que `memory_order_relaxed` não publica dados entre threads e usar o par
  release/acquire para criar *happens-before* entre produtor e consumidor.
- Gerenciar o ciclo de vida de elementos em memória crua com `std::construct_at`
  (placement new) e `std::destroy_at`, sem vazamento, destruição dupla nem "T de reserva".
- Implementar um anel de capacidade arbitrária com máscara de potência de 2 e contadores
  livres, sem off-by-one entre "cheia" e "vazia".
- Eliminar false sharing separando em linhas de cache o que cada thread escreve, e
  verificar isso de forma determinística.
- Reduzir o *true sharing* com uma cópia local do índice oposto.
- Validar código lock-free com ThreadSanitizer, AddressSanitizer/LeakSanitizer e estresse.

## Conhecimentos necessários

- `std::atomic`, `load`/`store` e a ideia de `std::memory_order` (desafio-13 usa atomics e
  ThreadSanitizer).
- Fila circular de capacidade fixa: índices de leitura/escrita, cheia × vazia (desafio-14).
- Placement new, tempo de vida de objetos, `std::optional`, semântica de movimento.
- Noção de cache de CPU: linhas de 64 bytes e coerência entre núcleos.

## Ambiente

- Padrão exigido: **C++20** (`-std=c++20 -pedantic-errors -Wall -Wextra`), g++ **14.2**
  (imagem Docker `gcc:14.2.0`).
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- O avaliador compila **três variantes** e roda cada teste em processo próprio, com watchdog
  interno e `timeout` externo, num container sem rede e com limites de CPU (2), memória e PIDs:
  - `[asan]` AddressSanitizer + UBSan + LeakSanitizer;
  - `[tsan]` ThreadSanitizer (não pode coexistir com o ASan no mesmo binário);
  - `[perf]` `-O2` sem sanitizers, só para uma medição **informativa** de vazão contra uma
    fila com `std::mutex` + `std::deque` (nunca reprova; tempo de parede num container com
    CPUs compartilhadas não é determinístico).
- O ThreadSanitizer do gcc 14 não inicia em kernels com randomização de endereços agressiva
  (WSL2, Ubuntu 24.04: `FATAL: ThreadSanitizer: unexpected memory mapping`). Por isso os
  testes `[tsan]` rodam sob `setarch "$(uname -m)" -R`. O perfil seccomp padrão do Docker
  Engine bloqueia essa chamada; por isso o container usa `scripts/lib/seccomp-tsan.json` — o
  perfil padrão do Docker com uma única regra extra, `personality(ADDR_NO_RANDOMIZE)` (nunca
  `seccomp=unconfined`). Se mesmo assim o `setarch` falhar, o avaliador avisa explicitamente
  e os testes `[tsan]` falham com esse motivo.
- Sem Docker (somente Linux, g++ >= 14, `make` e `setarch` do util-linux):
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`.

### Por que o layout é verificado por uma API, e não por medição

False sharing é um problema de desempenho, mas medi-lo (tempo, `perf stat`, cachegrind)
dentro de um container com CPUs compartilhadas dá números diferentes a cada execução — um
critério assim seria injusto. A **causa**, porém, é estrutural e determinística: em qual
bloco de 64 bytes mora cada campo que uma thread escreve. Por isso:

- a fila expõe `layout_info()`, que devolve um `QueueLayout` (`src/queue_layout.h`, que você
  **não** edita) com `sizeof`, `alignof` e o *offset* de cada campo quente: o índice do
  produtor, o do consumidor e as cópias locais que cada lado guarda do índice do outro;
- o teste confere essa descrição contra os **endereços reais** dos índices (o índice
  instrumentado `ProbeIndex` sabe onde mora), em filas no heap, na pilha e num array;
- e só então aplica as regras: `alignof >= 64` e nenhuma linha de cache com campos escritos
  pelas duas threads.

Da mesma forma, os memory orders e a cópia local do índice oposto são verificados com um
índice instrumentado (`tests/support/probe_index.h`) que conta leituras e escritas e registra
a ordem pedida — por isso o contrato pede que os dois índices usem só `load(order)` e
`store(valor, order)`. O ThreadSanitizer, no estresse com duas threads, é a segunda linha de
defesa.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial (3 variantes)
./test.sh           # ~1,5 min (cerca de 40 s quando tudo passa)
```

Para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                          # log completo de cada teste que falhar
cat build/logs/tsan-estresse_sequencia.log      # relatório inteiro do ThreadSanitizer
cat build/logs/asan-vida_strings_heap.log       # relatório do LeakSanitizer
```

Leia `src/spsc_queue.h` começando pelo contrato no topo. Num relatório do TSan, compare os
**dois** acessos (o atual e o `Previous`): qual thread escreve aquela memória, qual lê, e o
que deveria ordenar uma operação antes da outra. Para pensar no layout, desenhe os campos da
classe em blocos de 64 bytes.

## Arquivos que você pode modificar

- `src/spsc_queue.h` — `SpscQueue<T, IndexT>`.

Mude à vontade membros privados, includes, a representação dos índices e a ordem dos campos.
Mantenha a **API pública** e as garantias descritas no contrato do topo do arquivo
(construtor, `try_push`, `try_emplace`, `try_pop`, `capacity`, `size_approx`, `layout_info`,
o parâmetro `IndexT` e as operações usadas nele): os testes e `src/market_feed.cpp` dependem
dela.

Não altere `src/queue_layout.h`, `src/market_feed.*`, `tests/`, `test.sh`, `challenge.yaml`,
`Makefile` nem `Dockerfile`.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada verificação aparece como `[asan]` ou `[tsan]` com ✔ ou ✘ e, quando falha, o motivo: a
verificação que falhou com os números medidos, a etapa em que travou, ou o tipo de relato do
sanitizer com as linhas de `src/` envolvidas. As linhas `·` trazem medições; a seção `[perf]`
é só informativa.

## Critérios de conclusão

`./test.sh` termina com código 0 — as 16 verificações passam nas duas variantes (32 ✔):

- [ ] compila com g++ 14 em `-std=c++20 -pedantic-errors`;
- [ ] FIFO exato e capacidade exata para 1, 2, 3, 4, 5, 7, 8, 64, 100, 1000 e 1024, com várias voltas no anel; capacidade 0 lança `std::invalid_argument`;
- [ ] tipos só-movíveis funcionam e `try_push` numa fila cheia não consome o argumento;
- [ ] cada elemento é construído e destruído exatamente uma vez; fila vazia não contém T vivo; exceção no construtor de T não corrompe a fila; nada vaza (LeakSanitizer);
- [ ] o índice de cada lado é publicado com release e o índice oposto é lido com acquire (ou `seq_cst`);
- [ ] cada lado relê o índice oposto no máximo uma vez a cada 8 operações no cenário de lotes;
- [ ] `alignof >= 64`, índices em linhas de cache diferentes nos endereços reais, e `layout_info()` verdadeiro, com produtor e consumidor sem dividir linha;
- [ ] estresse com duas threads (milhões de inteiros, objetos não triviais e o feed de mercado) com ordem e checksum corretos;
- [ ] nenhum relato do ThreadSanitizer, AddressSanitizer, LeakSanitizer ou UBSan.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Building Low Latency Applications with C++*, Sourav Ghosh — cap. 4, "Building the C++
  Building Blocks for Low Latency Applications", seção "Transferring data using lock-free
  queues" (`LFQueue` SPSC: armazenamento pré-alocado, índices atômicos de leitura e escrita,
  separação entre obter o slot e publicar o índice).
- *C++ High Performance*, 2ª ed., Björn Andrist e Viktor Sehr — cap. 11, "Concurrency":
  "The C++ memory model" (reordenação de instruções, barreiras de acquire/release, por que
  relaxed só protege contra escritas rasgadas), "Example: A lock-free queue" e "Performance
  guidelines → False sharing" (`alignas` com `std::hardware_destructive_interference_size`).
- *Asynchronous Programming with C++*, Javier Reguera-Salgado e Juan Antonio Rufes — cap. 5,
  "Atomic Operations": "The C++ memory model" (sequential consistency, acquire-release,
  relaxed) e "SPSC lock-free queue" (por que potência de 2, sincronização de head/tail);
  cap. 13, "Improving Asynchronous Software Performance": "False sharing" e "SPSC lock-free
  queue" (índices em linhas separadas e cópias locais para reduzir o *true sharing*).
- cppreference: [`std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order),
  [`std::construct_at`](https://en.cppreference.com/w/cpp/memory/construct_at),
  [`std::destroy_at`](https://en.cppreference.com/w/cpp/memory/destroy_at),
  [`std::bit_ceil`](https://en.cppreference.com/w/cpp/numeric/bit_ceil),
  [`std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size).
- [ThreadSanitizer](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual).

## Próximo desafio

Este é o fim da linha de baixa latência da trilha C++ por enquanto. Volte ao mapa
(`./scripts/gitquest list` na branch `main`) e escolha outra trilha — ou tente estender a fila:
uma versão *em lote* (`try_push_n`/`try_pop_n`) que publica vários itens com um único
`store(release)`.
