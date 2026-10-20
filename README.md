# desafio-16 · O caminho mais curto

> Trilha: dsa · Padrão: **C++17** · Toolchain: **gcc:14.2.0** · Nível: advanced · ~75 min · 250 pontos

## Contexto

A transportadora acabou de expandir as entregas para a região metropolitana e o
planejador de rotas — um pequeno módulo C++ que ninguém revisa há anos — começou a
envergonhar a empresa. Motoristas recebem rotas mais longas do que o atalho que todo
mundo conhece, entregas intermunicipais aparecem com tempo **negativo**, o painel diz que
a cidade tem "18 quintilhões de bairros" e a consulta "estes dois depósitos estão
ligados?" simplesmente não volta quando o mapa inteiro é carregado.

Você foi chamado para consertar o planejador **sem trocar a interface**: o aplicativo dos
motoristas e o painel de operações já chamam `RoutePlanner` e `DisjointSet`.

## Problema

Rodando os testes você vai observar:

- `shortest_path` devolve uma rota **mais cara** que outra que existe (um atalho com mais
  ruas é ignorado), e ruas repetidas entre os mesmos cruzamentos confundem o cálculo;
- a lista de cruzamentos da rota vem **de trás para frente**, sem a origem — e vazia quando
  a origem é o próprio destino;
- um destino em outro bairro devolve uma "rota" de custo `2147483647` em vez de `std::nullopt`;
- rotas com trechos acima de ~35 minutos (2^31 µs) dão custos absurdos; o UBSan acusa
  `signed integer overflow`;
- `bfs_hops` diz que um cruzamento a 2 ruas de distância está a 3 (ou a 438!), dependendo
  da ordem em que as ruas foram cadastradas;
- `components()` não bate com o mapa quando há ciclos, ruas repetidas ou retornos;
- uma longa fila de uniões derruba o processo com **stack-overflow** (AddressSanitizer), e
  as consultas de conectividade num mapa de 1 milhão de cruzamentos não terminam;
- mesmo depois de corrigir o resultado, uma consulta de rota num mapa com um cruzamento muito
  movimentado pode ficar centenas de vezes mais lenta que o necessário.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Distinguir BFS (fila) de DFS (pilha) e usar BFS para contar o menor número de arestas.
- Implementar Dijkstra com `std::priority_queue` corretamente: quando um vértice se torna definitivo e o que fazer com entradas obsoletas na fila.
- Escolher tipos que comportam a soma dos custos e modelar "não há rota" com `std::optional`.
- Reconstruir caminhos a partir dos predecessores.
- Implementar Union-Find com compressão de caminho e união por tamanho/posto, sem recursão profunda.
- Ler testes diferenciais (contra Floyd–Warshall) e testes de desempenho adversariais.

## Conhecimentos necessários

- C++17: `std::vector`, `std::optional`, structured bindings, `std::priority_queue`, `std::queue`, `std::stack`.
- Grafos: lista de adjacência, BFS, a ideia do algoritmo de Dijkstra.
- Noções de complexidade (O(n), O(n log n), O(n²)) e de faixa dos tipos inteiros.

## Ambiente

- Padrão exigido: **C++17** (`-std=c++17 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, ASan/UBSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh` (os limites de tempo são relativos a uma referência medida na
  mesma máquina, então valem em qualquer computador).

O avaliador compila o código duas vezes: com `-O1 -fsanitize=address,undefined` para os testes de
correção e com `-O2` sem sanitizers para os testes de desempenho. Cada teste roda em um processo
próprio, com tempo limite, dentro de um container sem rede e com CPU e memória limitadas.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial
./test.sh           # mostra quais critérios falham e por quê
```

Para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                      # log completo de cada teste que falhar
cat build/logs/dijkstra_atalho.log          # log de um teste específico
cat build/logs/desempenho_grade.log         # passos e tempos medidos (linhas PASSO:)
```

Ferramentas úteis: os relatórios do **UBSan** (linha exata do estouro), do **ASan** (`stack-overflow`
com a função recursiva no topo da pilha), as mensagens dos testes (custo obtido × esperado, rota
devolvida) e o mapa impresso pelo teste diferencial — reproduza-o à mão num papel. Os tempos dos
testes de desempenho aparecem nas linhas `PASSO:` dos logs.

## Arquivos que você pode modificar

- `src/route_planner.h` e `src/route_planner.cpp` — o `RoutePlanner` (BFS, Dijkstra, conectividade);
- `src/disjoint_set.h` e `src/disjoint_set.cpp` — o `DisjointSet` (Union-Find).

Você pode mudar membros privados, includes, tipos internos e algoritmos. Mantenha a **API pública**
descrita nos comentários dos headers (nomes, parâmetros, retornos, exceções): os testes dependem dela.

Não altere `tests/`, `test.sh`, `start.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`: o avaliador
usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada critério aparece com ✔ ou ✘ e, quando falha, o motivo: a verificação que falhou, o erro do
sanitizer com a linha de `src/`, o tempo medido contra o limite ou o último passo alcançado antes de
um tempo esgotado.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o projeto compila com g++ 14 em `-std=c++17 -pedantic-errors`, com e sem sanitizers;
- [ ] `bfs_hops` dá o menor número de ruas, independentemente da ordem de cadastro e dos tempos;
- [ ] `shortest_path` devolve o custo mínimo exato (inclusive acima de 2^31 µs) e uma rota válida de `src` a `dst`, `{src}` quando `src == dst` e `std::nullopt` quando não há rota;
- [ ] ruas repetidas, retornos e ruas de tempo zero são tratados corretamente;
- [ ] `DisjointSet`, `connected` e `components` acompanham as ruas abertas, sem contar uniões redundantes;
- [ ] os 300 + 60 mapas aleatórios do teste diferencial batem com Floyd–Warshall, BFS e Union-Find de referência;
- [ ] nenhum erro de ASan/UBSan, inclusive numa fila de 1 milhão de uniões;
- [ ] desempenho (-O2): grade 1000×1000 (construção, 3 rotas, BFS), o mapa com o cruzamento movimentado e 3 milhões de operações adversárias de Union-Find ficam dentro de ~10× o tempo da referência.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Data Structures the Fun Way*, Jeremy Kubica — cap. 4 "Stacks and Queues" (DFS × BFS: trocar a estrutura muda o comportamento da busca), cap. 7 "Priority Queues and Heaps" e cap. 15 "Graphs" (Dijkstra).
- *Data Structures and Algorithms with the C++ STL*, John Farrier — cap. 9 "Advanced Container Adaptor Usage" (`std::stack`, `std::queue`, `std::priority_queue` e comparadores).
- *Practical C++ Game Programming with Data Structures and Algorithms*, Zhenyu George Li e Charles Shih-I Yeh — cap. 9 "Building AI Opponents", seção "Understanding A* pathfinding" (fila de prioridade em busca de caminhos).
- cppreference: [`std::priority_queue`](https://en.cppreference.com/w/cpp/container/priority_queue), [`std::optional`](https://en.cppreference.com/w/cpp/utility/optional), [tipos inteiros de largura fixa](https://en.cppreference.com/w/cpp/types/integer).
- Union-Find: [Disjoint-set data structure](https://en.wikipedia.org/wiki/Disjoint-set_data_structure) (compressão de caminho, união por posto/tamanho e a função inversa de Ackermann α(n)).
- [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) e [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer).

## Próximo desafio

Este é o último desafio da trilha **dsa** por enquanto. Conquista desbloqueada: **Cartógrafo das rotas**.
Volte ao catálogo (`./scripts/gitquest list`) para escolher a próxima trilha.
