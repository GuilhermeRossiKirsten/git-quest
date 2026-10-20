# Dicas — desafio-16

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Há problemas em três lugares independentes — vale atacar um de cada vez, guiado pelos testes:

- **BFS**: qual estrutura de dados faz os vértices saírem na ordem de distância? O que muda se
  o último que entrou for o primeiro a sair?
- **Dijkstra**: em que momento o custo de um cruzamento passa a ser *definitivo*? Quando ele é
  descoberto pela primeira vez, ou quando sai da fila de prioridade? Qual o tipo do custo acumulado
  e qual o maior valor que ele pode atingir?
- **Union-Find**: qual a altura máxima da árvore depois de `unite(0,1)`, `unite(1,2)`, …,
  `unite(n-2, n-1)`? Quanto custa um `find` numa árvore assim? E quantas chamadas recursivas?

Desenhe os mapas pequenos de `tests/test_03_dijkstra.cpp` num papel e simule o código.

## Nível 2

Onde olhar:

- `RoutePlanner::bfs_hops` — o contêiner usado como "fronteira".
- `RoutePlanner::shortest_path` — o vetor `settled` (quando é marcado e para que serve), o tipo de
  `best`, de `Entry` e de `Edge::travel_us` (em `route_planner.h`), o valor de `came_from` para
  vértices nunca alcançados, e o laço que monta `route.stops`.
- `DisjointSet::root_of`, `unite` e o contador `sets_`: o que acontece quando `a` e `b` já estão
  juntos? Para qual lado a árvore cresce?

Depois de corrigir o Dijkstra, rode o teste `desempenho_atalhos_tardios`: um vértice pode estar
**várias vezes** na fila. O que acontece quando uma cópia antiga (com custo pior) sai da fila?

## Nível 3

Abordagem:

- BFS usa `std::queue` (FIFO); marcar o vértice ao enfileirar continua correto *com fila*.
- Dijkstra com fila "preguiçosa": não marque nada ao inserir; ao retirar `(custo, u)`, se `custo`
  for maior que o melhor custo conhecido de `u`, descarte a entrada e siga. Relaxe as arestas só
  com entradas válidas. Use `std::int64_t` para tempos de rua e somas, e o máximo de `int64` como
  "infinito".
- Inalcançável: se o custo do destino continuar "infinito", devolva `std::nullopt`. Use um sentinela
  próprio para "sem predecessor"; percorra de `dst` até `src`, inclua `src` e inverta.
- Union-Find: `unite` não faz nada (nem decrementa o contador) se as raízes forem iguais; pendure a
  árvore menor na maior (tamanho ou posto) e comprima o caminho em `find`. Prefira um `find`
  iterativo (duas passadas, ou "path halving"); recursão só é segura se a altura for O(log n).

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `dijkstra_origem_igual_destino`: a rota de `src` até `src` é `{src}`, custo 0, mesmo com um retorno.
- `dijkstra_multiaresta_laco_zero`: a rua mais rápida entre dois cruzamentos pode ter sido cadastrada depois da mais lenta.
- `diferencial_*` com "tempo esgotado": um tempo de rua truncado para `int` vira negativo e o Dijkstra nunca para — confira o tipo de `Edge`.
- `uf_fila_longa` com `stack-overflow`: a recursão de `find` acompanha a altura da árvore; com compressão mas sem união por tamanho, a primeira consulta ainda desce 1 milhão de níveis.
- `desempenho_union_find` com "falha de segmentação": mesmo motivo, agora em `-O2`.
- `desempenho_grade` lento na construção: `add_road` também faz um `unite` — o Union-Find precisa ser rápido.
