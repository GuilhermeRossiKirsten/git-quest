# desafio-15 · Heap de prioridades

> Trilha: dsa · Padrão: **C++17** · Toolchain: **gcc:14.2.0** · Nível: intermediate · ~50 min · 150 pontos

## Contexto

O time de plataforma tem um escalonador de tarefas (`gq::Scheduler`) que decide qual job
roda primeiro: o de prazo (deadline) mais próximo. Por baixo dele há uma fila de prioridade
"caseira", `gq::IndexedMinHeap`, escrita porque `std::priority_queue` não deixa reagendar
nem cancelar uma tarefa que já está na fila.

Desde que o escalonador entrou em produção, os chamados não param: relatórios rodando antes
de backups que venciam mais cedo, tarefas reagendadas que "somem", cancelamentos que
bagunçam a fila inteira — e, nos dias de pico, o escalonador fica visivelmente lento.
Você recebeu o módulo para consertar **sem trocar a interface**: outras equipes já usam
`src/indexed_min_heap.h` e `src/scheduler.h`.

## Problema

Rodando `./test.sh`, você vai observar:

- pops que **não** saem em ordem crescente de prioridade, mesmo sem nenhuma atualização;
- `priority(chave)` devolvendo a prioridade **de outra chave** depois de algumas inserções;
- reduzir a prioridade de uma tarefa às vezes funciona; **aumentar** a prioridade do topo
  deixa a tarefa no topo, com a prioridade nova;
- depois de `erase` no meio da fila, os pops seguintes saem fora de ordem;
- tarefas com o **mesmo prazo** saem em ordem imprevisível, embora o contrato diga que a
  primeira agendada sai primeiro;
- o teste de desempenho (1 milhão de operações) não termina no tempo-limite.

O contrato completo, inclusive a regra de desempate, está no comentário do topo de
`src/indexed_min_heap.h`. Leia-o antes de mexer no código.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Manter a propriedade de heap num vetor 0-based (índices de pai/filhos, sift-up, sift-down).
- Manter um mapa chave → posição coerente a cada troca, para atualizar e remover em O(log n).
- Implementar decrease-key, increase-key e remoção arbitrária restaurando o heap na direção certa.
- Definir e garantir uma regra de desempate estável (FIFO).
- Usar testes diferenciais e medições de desempenho para validar uma estrutura de dados.

## Conhecimentos necessários

- Templates de classe, `std::vector` e `std::unordered_map` em C++.
- Noção de árvore binária completa e de complexidade O(log n) vs. O(n).
- Recomendado: ter concluído o desafio-14.

## Ambiente

- Padrão exigido: **C++17** (`-std=c++17 -pedantic-errors`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, ASan/UBSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`.

Os testes funcionais são compilados com `-fsanitize=address,undefined` e
`-D_GLIBCXX_ASSERTIONS` (acesso fora dos limites de `std::vector` aborta com mensagem) e
rodam um por processo. O teste de desempenho é compilado à parte com `-O2` e compara o
seu heap com um modelo baseado em `std::set` medido na mesma máquina. Tudo roda num
container sem rede e com limites de CPU, memória e tempo.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial
./test.sh           # mostra quais critérios falham e por quê
```

Ferramentas de diagnóstico:

- `GQ_VERBOSE=1 ./test.sh` imprime o log inteiro dos testes que falharem;
  `build/logs/<teste>.log` guarda cada log.
- As mensagens dizem **qual operação** divergiu (ex.: "operação nº 42: pop() devolveu…").
  Reproduza o caso com poucos elementos e desenhe o vetor como árvore no papel.
- Comece pelos testes mais simples (`heap_basico`, `heap_ordem_pops`): os demais dependem deles.

## Arquivos que você pode modificar

- `src/indexed_min_heap.h` — o heap indexado (onde está o problema);
- `src/scheduler.h` e `src/scheduler.cpp` — o escalonador que usa o heap.

Você pode mudar membros privados, funções auxiliares e includes. Mantenha a **API pública**
descrita nos comentários dos headers (nomes, parâmetros, retornos, exceções e a regra de
desempate): os testes dependem dela. Não use `std::set`/`std::map`/`std::priority_queue`
para contornar o heap — o objetivo é consertá-lo, e o teste de desempenho espera um heap
binário bem mais rápido que um `std::set`.

Não altere `tests/`, `test.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada critério aparece com ✔ ou ✘ e, quando falha, o motivo: a operação que divergiu do
modelo, o erro de memória/limite detectado ou o tempo medido.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o projeto compila com g++ 14 em `-std=c++17 -pedantic-errors`;
- [ ] pops saem em ordem de prioridade; `top`, `contains` e `priority` refletem o estado real;
- [ ] `update_priority` funciona para prioridades menores **e** maiores;
- [ ] `erase` remove de qualquer posição e o heap continua válido;
- [ ] empates saem em ordem FIFO (push, ou o último `update_priority`);
- [ ] 200 mil operações aleatórias (semente fixa) dão exatamente os mesmos resultados de um modelo `std::set`;
- [ ] o `Scheduler` agenda, reagenda, cancela e executa tarefas na ordem certa (20 mil eventos);
- [ ] nenhum erro de ASan/UBSan nem acesso fora dos limites de `std::vector`;
- [ ] 1 milhão de operações mistas em `-O2` levam no máximo 3x o tempo do modelo `std::set`.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Data Structures the Fun Way*, Jeremy Kubica — cap. 7 "Priority Queues and Heaps" (seções "Storing Auxiliary Information" e "Updating Priorities", min heaps).
- *Data Structures and Algorithms with the C++ STL*, John Farrier — cap. 9 "Advanced Container Adaptor Usage" (`std::priority_queue`) e cap. 14 "Permutations, Partitions, and Heaps" (`std::make_heap`, `std::push_heap`, `std::pop_heap`, `std::is_heap`).
- *Estrutura de Dados* (material FIAP) — cap. 2 "Pilhas e filas" (fila FIFO) e cap. 5 "Espalhamento" (mapas não ordenados, base do mapa de posições).
- cppreference: [`std::priority_queue`](https://en.cppreference.com/w/cpp/container/priority_queue), [`std::make_heap`](https://en.cppreference.com/w/cpp/algorithm/make_heap), [`std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map).

## Próximo desafio

**desafio-16 — O caminho mais curto**: grafos com BFS, Dijkstra (que usa exatamente uma fila
de prioridade com decrease-key) e Union-Find, com testes de desempenho.
