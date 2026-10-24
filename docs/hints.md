# Dicas — desafio-26

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Uma arena entrega **memória crua**; quem decide o que mora ali (e quando deixa de morar) é
quem constrói os objetos. Para cada falha, pergunte:

- quem garante que o endereço devolvido serve para o tipo que vai ser construído nele?
- se a arena "libera tudo de uma vez", quem chama os destrutores — e o que acontece com a
  memória que um `std::string` dentro de um objeto da arena pediu ao heap?
- quando um contêiner `std::pmr` compara dois allocators e eles dizem "somos iguais", o que
  ele se sente autorizado a fazer com os ponteiros do outro?
- um `std::pmr::vector<X>` usa a arena para o **seu** buffer. E os elementos `X`? Eles sabem
  que existe uma arena?

Comece pelos testes `arena_*` (a base de tudo) e leia o comentário de contrato de cada header.

## Nível 2

Seis lugares a investigar:

- `Arena::allocate` (`src/arena.cpp`): o que é feito com o parâmetro `alignment`? Um
  grânulo de 8 bytes serve para `alignas(16)` e `alignas(64)`? E o primeiro pedido de um
  bloco novo, quando o alinhamento é maior que o do próprio bloco?
- `Arena::make` (`src/arena.h`) e `Arena::reset`: onde ficaria registrado "este objeto
  precisa ser destruído"? Em que ordem?
- `ArenaResource::do_is_equal`: "iguais" significa *memória alocada por um pode ser liberada
  pelo outro* — e, na prática, que um pode **adotar** os blocos do outro.
- `LogIndex` (`src/log_index.h`): o tipo dos elementos de `messages_` e da chave de
  `services_`. Qual allocator um `std::string` usa?
- `LogIndex::ingest` e `stats_for`: mesmo com o tipo certo, construir um temporário e depois
  inseri-lo cria o temporário **onde**?
- `LogIndex::top_services`: qual vetor é rascunho e qual é o resultado? Em qual resource cada
  um foi criado, e o que `scratch_.release()` faz com o resultado da chamada anterior?

## Nível 3

Abordagem, item a item (sem código pronto):

- Alinhamento: arredonde o **endereço** (não o tamanho) para cima até o múltiplo de
  `alignment` (aritmética com `std::uintptr_t` ou `std::align`). Se não couber, peça um bloco
  novo com folga suficiente para alinhar dentro dele (tamanho + alinhamento) e alinhe de novo.
- Destrutores: para `T` não trivialmente destrutível (`std::is_trivially_destructible_v`),
  aloque na própria arena um pequeno nó com "ponteiro para função que destrói" + objeto +
  próximo, empilhado numa lista. Em `reset()`, percorra a lista (o topo é o mais recente:
  ordem inversa) **antes** de devolver os blocos.
- `do_is_equal`: só a própria instância (`this == &other`). Com isso, o `pmr::vector` move
  elemento a elemento quando os resources diferem, e o `absorb()` passa a copiar para a arena
  certa sozinho.
- Propagação: use `std::pmr::string` nos elementos e nas chaves. Insira com
  `emplace_back(string_view)` / `emplace(string_view, ...)` para que a *uses-allocator
  construction* crie a string já com o allocator do contêiner — nada de
  `push_back(std::pmr::string(...))`.
- `top_services`: rascunho (ranking) no `scratch_`, resultado no `out`. Não "copie para fora"
  com o construtor de cópia: a cópia de um contêiner `pmr` vai para o resource **padrão**.

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `arena_alinhamento_64` falha só para 4096 ou para o pedido de 5000 bytes? A folga do bloco
  novo não considera o alinhamento.
- `arena_destrutores` acusa a ordem `[0, 1, 2, 3, 4]`? A lista está sendo percorrida do mais
  antigo para o mais recente.
- `index_ingest_sem_heap_global` conta só um punhado de alocações? Sobrou um temporário na
  criação da chave do mapa (um por serviço novo com nome que não cabe no SSO). Conta
  milhares? Mensagens ainda nascem fora da arena.
- `index_top_services` diz "1 alocação no heap global" mas o resultado sobrevive? Você copiou
  o resultado (`select_on_container_copy_construction` devolve o resource padrão).
- `index_absorb_shards` com use-after-free em `message()`? Algum `pmr::string` ou o próprio
  vetor ainda adota memória do shard: revise `do_is_equal`.
