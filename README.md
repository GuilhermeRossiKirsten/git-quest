# desafio-30 · Dados que não cabiam no cache

> Trilha: performance · Padrão: **C++20** · Toolchain: **gcc:14.2.0 + Valgrind 3.19** · Nível: advanced · ~75 min · 250 pontos

## Contexto

A rede de supermercados **Mercado Boa Vista** tem um painel interno de vendas
(`gq::SalesStore`): ele recebe pedidos, devoluções e o catálogo de produtos e
responde, em memória, às quatro perguntas que a diretoria faz o dia inteiro —
quanto faturamos, quantas unidades saíram por dia numa região, quanto devolvemos
e quanto vale uma cesta de compras.

Com 20 lojas, ninguém reclamava. Agora a rede tem 1 500 lojas, um ano de
histórico e 120 000 pedidos na memória, e o painel "engasga". O código está
**certo** — todas as respostas batem com a contabilidade —, mas alguém rodou um
profiler e descobriu que a CPU passa a maior parte do tempo esperando a memória.
Sua missão é fazer os dados caberem na cache **sem mudar a API nem as respostas**.

## Problema

Rode `./start.sh`. Além de compilar, ele mede cada consulta com o **simulador de
cache do Valgrind** e mostra algo assim:

```text
      região                     Ir    misses D1    misses LL
      ingest             133 433 677    2 344 981    2 033 528
      revenue              4 152 699      632 681      632 679
      units_by_day         4 253 514      406 950       52 801
      refunded_cents         999 591      159 794      136 356
      basket_value        21 459 322    1 302 292       30 968
```

Repare nos sintomas:

- `revenue` lê só **dois** campos de cada pedido, mas cada chamada erra a cache
  mais de uma vez **por pedido** (3 chamadas × 120 000 pedidos ≈ 630 000 misses) —
  e erra também o último nível (LL), como se os pedidos nem coubessem em 2 MiB;
- `units_by_day` soma números inteiros guardados num único vetor contíguo e,
  ainda assim, erra o L1 em quase toda leitura;
- `refunded_cents` percorre ~30 000 devoluções e erra a cache em praticamente
  todas, embora o par (dia, valor) de cada uma tenha só 10 bytes;
- `basket_value` faz 200 000 buscas num catálogo de apenas 4 000 produtos e soma
  mais de seis misses de L1 por busca.

`./test.sh` confirma: os testes funcionais passam, mas os critérios de cache não.

**Ferramentas para investigar** (todas dentro da imagem do desafio):

```bash
./test.sh                                   # roda tudo e salva build/perf/callgrind.out.<n>
docker run --rm -v "$PWD":/work -w /work gitquest/desafio-30:<tag> \
  callgrind_annotate --auto=yes build/perf/callgrind.out.2     # misses por linha de código (revenue)
```

(`./start.sh` mostra a tag da imagem.) No `callgrind_annotate`, as colunas
`D1mr`/`DLmr` apontam a linha exata do laço que erra a cache. Pense em **quantas
linhas de 64 bytes** cada consulta precisa trazer da memória e quantas ela
realmente traz. `sizeof` e `offsetof` respondem onde cada campo mora.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Explicar por que o que importa são as **linhas de cache** tocadas, e medir misses de L1/LL de forma reprodutível.
- Separar dados quentes de frios (array of structs gordo → structure of arrays ou struct quente compacta).
- Eliminar indireção por ponteiro e listas ligadas com nós espalhados no heap.
- Percorrer uma matriz na ordem em que ela está na memória.
- Trocar um contêiner baseado em nós por um vetor ordenado quando os dados quase não mudam.
- Otimizar sem mudar a API nem os resultados.

## Conhecimentos necessários

- C++ moderno: `std::vector`, `std::span`, `std::optional`, algoritmos (`std::sort`, `std::lower_bound`).
- Noções de hierarquia de memória: cache L1/L2/L3, linha de cache, localidade espacial e temporal.
- Ler a saída de uma ferramenta de profiling (aqui, `callgrind_annotate`).
- Ter feito o desafio-14 (estruturas de dados contíguas, sem heap) ajuda.

## Ambiente

- Padrão exigido: **C++20** (`-std=c++20 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker construída a partir de `gcc:14.2.0` com **Valgrind 3.19.0**
  (`valgrind=1:3.19.0-1`, do Debian 12, versão fixada no `Dockerfile`).
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash). Os limites foram
  calibrados em linux/amd64.
- Sem Docker: Linux com g++ >= 14, make e valgrind, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh` (os números podem variar um pouco com outro compilador/Valgrind).

**Como a medição funciona.** Nada de cronômetro: o bench (`tests/bench.cpp`, compilado
com `-O2 -g`) roda sob `valgrind --tool=callgrind --cache-sim=yes`, que simula uma cache
**fixa** — L1 de dados de 32 KiB, 8 vias, linhas de 64 B; LL de 2 MiB, 16 vias — e não a da
sua máquina. Cada fase fica entre *client requests* (`CALLGRIND_TOGGLE_COLLECT` /
`CALLGRIND_DUMP_STATS_AT`), então só ela é contada, e antes de cada fase a cache simulada é
"esfriada". O simulador é o mesmo do `cachegrind`; usamos o `callgrind` porque o cachegrind
do Valgrind 3.19 não sabe medir só um trecho do programa. Com a mesma imagem e o mesmo código,
os números se repetem exatamente.

Os testes funcionais são compilados à parte com AddressSanitizer + UBSan.

## Como começar

```bash
./start.sh          # constrói a imagem, compila e mostra a medição do código atual
./test.sh           # testes funcionais + critérios de cache, cada um com ✔/✘
```

## Arquivos que você pode modificar

- `src/sales_store.h` — **apenas a parte privada** de `gq::SalesStore` (e os `#include`).
- `src/sales_store.cpp` — a implementação.

A **API pública** descrita no comentário do topo de `src/sales_store.h` é fixa: os tipos
`Status`, `Order`, `Refund`, `Product`, `BasketLine`, `BasketTotal` e as assinaturas públicas
de `SalesStore`. `find_order()` e `find_product()` precisam devolver **todos** os campos, então
os dados frios não podem simplesmente sumir.

Não altere `tests/`, `test.sh`, `start.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`:
o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GQ_VERBOSE=1 ./test.sh        # log completo dos testes funcionais que falharem
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14 e valgrind)
```

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] tudo compila em C++20 com `-pedantic-errors`, com a API pública intacta;
- [ ] respostas idênticas às do gabarito (`tests/support/dataset.h`) em redes de vários
      tamanhos e sementes, casos de borda incluídos, e checksum de todas as respostas igual;
- [ ] nenhum erro de AddressSanitizer/UBSan/LeakSanitizer;
- [ ] no bench (que também confere as respostas), para `revenue`, `units_by_day`,
      `refunded_cents` e `basket_value`: misses de D1 (e de LL, onde o layout faz diferença)
      **≤ 40% do código inicial e ≤ 2× uma solução de referência**, e instruções (Ir)
      **≤ 1,5× o código inicial** — os limites exatos estão em `tests/budgets.txt`;
- [ ] a ingestão não pode ficar mais de 2× mais cara (Ir e misses de D1) que a atual.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *The Art of Writing Efficient Programs*, Fedor G. Pikus — cap. 4 "Memory Architecture and
  Performance": "The speed of sequential memory access" e "Memory performance optimizations in
  hardware" (pp. 127–130), "Memory-efficient data structures" (lista × vetor, pp. 132–135),
  "Profiling memory performance" (contadores de misses, pp. 135–137) e "Optimizing algorithms
  for memory performance" (pp. 137–143).
- *C++ High Performance*, 2ª ed., Björn Andrist e Viktor Sehr — cap. 4 "Data Structures":
  "The properties of computer memory" (linhas de cache, localidade, *cache thrashing* ao
  percorrer uma matriz coluna a coluna, pp. 99–103), "List and forward_list" e "Ordered sets
  and maps" (contêineres baseados em nós) e "Parallel arrays" (separar dados quentes e frios,
  pp. 123–130).
- *Computer Architecture*, Charles Fox — cap. 10 "Memory": "The Memory Hierarchy" (p. 215) e
  "Caches" (pp. 226–232: linhas, tags, hit/miss, mapeamento direto, associativo e associativo
  por conjunto — os parâmetros que passamos ao simulador).
- Manual do Valgrind: [Cachegrind](https://valgrind.org/docs/manual/cg-manual.html) e
  [Callgrind](https://valgrind.org/docs/manual/cl-manual.html) (`--cache-sim`, client requests).

## Próximo desafio

**desafio-33** — siga a trilha com `./scripts/gitquest list`.
