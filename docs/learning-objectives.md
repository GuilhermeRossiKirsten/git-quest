# Objetivos de aprendizagem — desafio-16

Ao concluir este desafio você será capaz de:

1. **Distinguir BFS de DFS** e usar BFS (fila) para obter o menor número de arestas.
2. **Implementar Dijkstra corretamente** com `std::priority_queue`: um vértice só é definitivo quando
   sai da fila, e entradas obsoletas são descartadas sem reprocessar as arestas.
3. **Dimensionar tipos e casos de borda**: custos em `std::int64_t`, `src == dst` e destino
   inalcançável modelados com `std::optional`.
4. **Reconstruir o caminho** a partir dos predecessores, na ordem `src → dst`.
5. **Implementar Union-Find** com compressão de caminho e união por tamanho/posto, sem recursão
   proporcional à altura, contando só uniões efetivas.
6. **Validar algoritmos de grafos** com testes diferenciais contra uma referência e testes de
   desempenho adversariais com limites relativos.

## Como os testes verificam cada objetivo

Os testes de correção rodam com `-O1 -fsanitize=address,undefined`; os de desempenho com `-O2`.
Cada teste roda em um processo próprio, com tempo limite.

| Objetivo | Verificação |
|---|---|
| 1. BFS × DFS | `bfs_linha`, `bfs_ignora_pesos`, `bfs_ordem_das_ruas` (o mesmo mapa cadastrado em duas ordens), `bfs_grade` (distância de Manhattan numa grade 30×30) e `bfs_hops` comparado com uma BFS de referência em todos os mapas do teste diferencial e na grade 1000×1000. |
| 2. Dijkstra correto e eficiente | `dijkstra_atalho` (o vértice é descoberto primeiro por um caminho caro), `dijkstra_multiaresta_laco_zero`, `diferencial_floyd` (300 mapas aleatórios contra Floyd–Warshall, todos os pares) e `diferencial_dinamico`. `desempenho_atalhos_tardios` coloca um vértice de grau 200 mil 100 mil vezes na fila: sem descartar entradas obsoletas o tempo explode; o limite é 10× a referência + 300 ms. `desempenho_grade` mede 3 consultas numa grade 1000×1000. |
| 3. Tipos e bordas | `dijkstra_tempos_grandes` (somas acima de 2^31 e de até 2·10^12 µs; o UBSan acusa estouro de `int`), `diferencial_tempos_grandes` (ruas de 10^9 a 10^12 µs), `dijkstra_origem_igual_destino`, `dijkstra_inalcancavel`, `entradas_invalidas` (exceções sem alterar o mapa). |
| 4. Reconstrução do caminho | Toda rota devolvida é validada: começa em `src`, termina em `dst`, cada passo usa uma rua existente e a soma dos tempos é o custo anunciado; `dijkstra_rota_completa` exige a sequência exata. |
| 5. Union-Find | `uf_basico` (retorno de `unite`, `count()` com uniões redundantes), `uf_bairros_com_ciclos`, `uf_dinamico`, `components()`/`connected()` no teste diferencial; `uf_fila_longa` (1 milhão de uniões em fila sob ASan: recursão profunda vira `stack-overflow`); `desempenho_union_find` (fila, estrela invertida e 1 milhão de operações aleatórias, limite 10× a referência + 300 ms). |
| 6. Testes como ferramenta | As mensagens mostram custo obtido × esperado, a rota devolvida, o mapa aleatório que falhou (para reproduzir à mão) e os tempos medidos contra o limite (linhas `PASSO:` nos logs). |
