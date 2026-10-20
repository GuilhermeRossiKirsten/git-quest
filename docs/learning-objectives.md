# Objetivos de aprendizagem — desafio-15

Ao concluir este desafio você será capaz de:

1. **Manter a propriedade de heap** num vetor 0-based: calcular pai e filhos, subir
   (sift-up) e descer (sift-down) escolhendo o menor filho.
2. **Manter um índice auxiliar** (mapa chave → posição) coerente a cada troca, o que permite
   `contains`/`priority` em O(1) e `update_priority`/`erase` em O(log n).
3. **Implementar decrease-key, increase-key e remoção arbitrária**, restaurando o heap na
   direção certa (para cima ou para baixo).
4. **Definir e garantir uma regra de desempate estável** (FIFO por ordem de definição da
   prioridade) numa estrutura que, por natureza, não é estável.
5. **Validar estruturas de dados** com testes diferenciais contra um modelo simples e com
   medições de desempenho relativas.

## Como os testes verificam cada objetivo

Os testes funcionais rodam com ASan + UBSan + `_GLIBCXX_ASSERTIONS`, um processo por teste,
e verificam só comportamento observável (sequência de pops, `top`, `priority`, `contains`).

| Objetivo | Verificação |
|---|---|
| 1. Propriedade de heap | `heap_basico` e `heap_ordem_pops` (2000 prioridades embaralhadas precisam sair em ordem; topo conferido após cada push). |
| 2. Mapa de posições | `heap_posicoes` confere `priority(k)` de todas as chaves após trocas, pops e updates; `heap_basico` e `heap_erase` também. Posições inválidas abortam via `_GLIBCXX_ASSERTIONS`. |
| 3. decrease/increase-key e erase | `heap_diminuir_prioridade`, `heap_aumentar_prioridade` (inclusive o topo que precisa afundar), `heap_erase` (nó interno cujo substituto precisa subir; 600 remoções aleatórias); `scheduler_reagendar` e `scheduler_cancelar` de ponta a ponta. |
| 4. Desempate FIFO | `heap_empates_fifo` (inserções iguais, update para o mesmo valor, 3000 elementos com 4 prioridades); `scheduler_basico` (prazos iguais); `heap_diferencial` usa só 64 prioridades, então quase todo pop é um empate. |
| 5. Diferencial e desempenho | `heap_diferencial`: 200 mil operações (semente fixa) comparadas com um modelo `std::set`; `scheduler_ponta_a_ponta`: 20 mil eventos contra um modelo `std::map`; `desempenho`: 1 milhão de operações em `-O2` com os mesmos resultados do modelo e no máximo 3x o tempo dele (a solução de referência fica perto de 0,35x; uma busca linear ou reconstrução O(n) estoura o limite). |
