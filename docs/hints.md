# Dicas — desafio-33

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Há cinco famílias de sintoma, e elas são independentes — ataque uma por vez, começando pelos
testes de uma thread só (`fifo_*`, `capacidade_*`, `vida_*`), que são os mais baratos de ler.

- **Capacidade.** Para uma fila de capacidade 1000, desenhe à mão os índices que
  `(i + 1) & mask_` produz a partir de 0. E para capacidade 4: quando `next == head`, quantos
  itens há na fila?
- **Ciclo de vida.** Depois de `std::optional<T> value{std::move(slot)}`, o objeto do slot
  ainda existe? Quem o destrói? E quem destrói os itens que ainda estão na fila quando ela morre?
- **Memory order.** O que garante que a thread consumidora, ao ver o índice novo, também veja
  o elemento que foi construído no slot *antes* dele?
- **Layout.** Desenhe `SpscQueue` em blocos de 64 bytes. Em que bloco ficam `head_` e `tail_`?
- **Leituras do índice oposto.** Quantas vezes por `try_push` o produtor lê o índice que o
  consumidor escreve? Precisa?

`GQ_VERBOSE=1 ./test.sh` mostra o log completo de cada falha.

## Nível 2

Onde olhar e por quê:

- **Capacidade** (`SpscQueue(std::size_t)`, `try_emplace`, `size_approx`): a máscara só
  funciona como `%` quando o tamanho do anel é potência de 2 — e o tamanho do anel não
  precisa ser igual à capacidade pedida. "Cheia" e "vazia" com `next == head` / `head == tail`
  desperdiçam um slot; contadores que só crescem (o slot é `contador & mask_`) deixam
  `tail - head` ser o número exato de itens.
- **Ciclo de vida**: memória crua de `operator new` não contém objetos. `try_pop` precisa
  encerrar a vida do objeto do slot, e o destrutor da fila precisa encerrar a vida de
  *exatamente* os que ainda estão lá — nem um a mais (destruição dupla), nem um a menos.
- **Por que relaxed não basta**: `relaxed` só garante que o próprio índice é lido/escrito
  inteiro. Ele não ordena a escrita do *slot* (que não é atômica) em relação à escrita do
  índice. Sem uma relação *happens-before* entre "construir no slot" e "ler o slot", o
  programa tem data race — comportamento indefinido, mesmo que no x86 o hardware quase
  sempre entregue na ordem. O compilador também pode reordenar. Veja "Acquire-release
  ordering" e "Relaxed memory ordering" no cap. 5 de *Asynchronous Programming with C++*.
- **Layout**: `alignas` num membro muda o alinhamento da classe inteira. Veja "False sharing"
  no cap. 11 de *C++ High Performance* e no cap. 13 de *Asynchronous Programming with C++*.

## Nível 3

A abordagem, sem código pronto:

- Guarde `capacity_` (a pedida) e use um anel de `std::bit_ceil(capacity)` slots com
  `mask_ = anel - 1`. `head_` e `tail_` contam pops e pushes desde o início; cheia é
  `tail - head == capacity_`, vazia é `head == tail`. Recuse capacidade 0 no construtor.
- Produtor: lê o próprio `tail_` com relaxed, confere espaço, **constrói** o elemento com
  `std::construct_at` e só então faz `tail_.store(tail + 1, release)`. Consumidor: lê
  `tail_` com **acquire**, move o valor para fora, chama `std::destroy_at` no slot e faz
  `head_.store(head + 1, release)`; o produtor lê `head_` com **acquire** antes de reutilizar
  um slot. (Tudo `seq_cst` também é correto; só é mais caro em algumas arquiteturas.)
- Destrutor: destrói `[head, tail)` e libera a memória com o mesmo `align_val_t`.
- Cada lado guarda uma cópia simples (`std::size_t`) do último valor lido do índice oposto e
  só relê o índice de verdade quando a cópia diz "cheia" (produtor) ou "vazia" (consumidor).
- Layout: campos só-leitura juntos; o índice do consumidor e a cópia que o consumidor guarda
  numa linha `alignas(64)`; o índice do produtor e a cópia dele em outra linha `alignas(64)`.
  Preencha os quatro offsets em `layout_info()` com `byte_offset(this, &campo)`.

## Nível 4

Se ainda sobrar algum ✘:

- **`vida_excecao_no_construtor`**: o índice só pode andar depois que o construtor de T
  terminou sem lançar.
- **`tipo_so_movivel`**: verifique se a fila está cheia *antes* de mover o argumento.
- **`layout_info()` não confere com a memória real**: os offsets precisam ser medidos dos
  membros de verdade (o teste compara com o endereço do índice instrumentado).
- **TSan acusando `size_approx`** ou o destrutor: leituras de índice feitas por quem não é o
  dono também precisam de acquire.
- **O estresse trava**: alguma capacidade faz `try_push` devolver `false` para sempre — rode
  `capacidade_exata` primeiro.
