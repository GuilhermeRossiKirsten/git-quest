# Dicas — desafio-15

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe o vetor do heap como árvore para 7 elementos, numerando as posições a partir de 0.
Quem são os filhos da posição 0? E os da posição 1? As funções `parent`, `left` e `right`
concordam entre si?

Depois pergunte: quando um nó desce, para qual dos dois filhos ele deve ir? Quando dois
nós trocam de lugar no vetor, quantas entradas do mapa chave → posição deixam de valer?

## Nível 2

Há problemas em seis pontos de `src/indexed_min_heap.h`:

- os índices dos filhos (`left`/`right`) para um vetor 0-based;
- `sift_down`, que só olha para um dos filhos;
- `swap_nodes`, que atualiza o mapa `pos_` só para um lado da troca;
- `update_priority`: aumentar a prioridade viola o heap em qual direção?
- `erase`: o nó que ocupa o lugar do removido pode precisar subir **ou** descer; e por que
  ele faz uma busca linear se existe `pos_`?
- `less`: com prioridades iguais, o que decide quem sai primeiro? Releia a regra de
  desempate no comentário do topo do arquivo.

O `Scheduler` está correto: os testes dele falham por causa do heap.

## Nível 3

Abordagem:

- filhos de `i` em `2i+1` e `2i+2`; pai em `(i-1)/2`;
- `sift_down` escolhe o **menor** entre o nó e os filhos existentes e para quando o nó já é o menor;
- toda troca atualiza `pos_` para os dois nós; com isso `erase` pode usar `pos_` em O(1);
- uma função "restaura a posição i" que sobe se o nó for menor que o pai e, caso contrário,
  desce — use-a em `update_priority` e depois de mover o último nó para o buraco do `erase`
  (e reaproveite o mesmo caminho no `pop`);
- guarde em cada nó um número de sequência crescente, atribuído no `push` e renovado no
  `update_priority`, e use-o como critério de desempate em `less`.

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `heap_empates_fifo` mas o resto passa: o `update_priority` renova a sequência mesmo
  quando a prioridade nova é igual à antiga?
- `heap_erase` / `heap_diferencial`: o caso em que o nó removido é o último do vetor
  (não há o que mover) está tratado?
- `desempenho` reprovado com tudo o mais verde: há alguma busca linear (`std::find`,
  laço sobre o vetor) ou reconstrução do heap inteiro em alguma operação?
- "Assertion '__n < this->size()' failed": alguma posição de `pos_` aponta para além do fim do vetor.
