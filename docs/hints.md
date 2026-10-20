# Dicas — desafio-14

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe um array de 5 lugares no papel com duas setas, `head_` e `tail_`. Faça 5 `push()`
seguidos: onde as setas param? E se você fizer 5 `pop()` a partir daí — onde elas param?
Se as duas situações terminam com as setas no **mesmo** lugar, como `empty()` e `full()`
conseguem saber qual das duas aconteceu?

Depois, abra o log de um teste diferencial (`build/logs/diferencial_capacidade_7.log`) e
reproduza no papel as últimas operações listadas.

## Nível 2

Há três frentes para investigar em `src/ring_buffer.h`:

- **Contabilidade**: com só `head_` e `tail_`, "cheia" e "vazia" se confundem. O código
  atual resolve isso abrindo mão de um lugar — e por isso a capacidade real é N - 1 (e
  N = 1 não funciona). Que informação extra acabaria com a ambiguidade?
- **Posições**: `operator[](i)` deveria contar a partir do mais antigo, não do início do
  array. E em `back()`, o que acontece com `tail_ - 1` quando `tail_` é 0? Lembre-se de
  que `std::size_t` não tem sinal: faça a conta para N = 5 e para N = 8. O que
  `push_overwrite()` precisa fazer com o mais antigo quando a fila está cheia?
- **Armazenamento**: `std::vector<T>(N)` usa o heap, constrói N objetos `T()` logo de
  saída (por isso exige construtor padrão) e os mantém vivos para sempre. Atribuir um
  novo valor a um lugar não é o mesmo que construir um objeto ali, e "andar com o índice"
  não destrói nada.

## Nível 3

Uma abordagem que resolve tudo sem truques:

- guarde `head_` (posição do mais antigo) e `count_` (quantos elementos há). `empty()`,
  `full()` e `size()` viram comparações diretas com `count_`;
- calcule a posição física do i-ésimo elemento num único lugar, algo como
  `(head_ + i) % N`, e use-o em `push`, `back` e `operator[]`. Somar antes de tirar o
  módulo evita a subtração sem sinal;
- troque o `std::vector` por um array de tamanho fixo **dentro** do objeto em que cada lugar
  pode estar vazio ou ocupado: `std::array<std::optional<T>, N>`. Um `std::optional<T>`
  vazio não constrói nenhum `T`; `emplace(...)` constrói o elemento no lugar e `reset()`
  o destrói;
- `pop()` move o valor para fora, destrói o lugar e avança `head_`; `push_overwrite()` com
  a fila cheia remove o mais antigo (destruindo-o) e depois insere; `clear()` destrói
  todos os elementos.

Com `std::optional`, o destrutor e a cópia gerados pelo compilador já fazem a coisa certa.

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `basico_capacidade_1` / `diferencial_capacidade_1`: com N = 1, `(x + 1) % 1` é sempre 0.
  Nenhuma regra de cheia/vazia baseada só em índices funciona aqui.
- `basico_back_apos_volta`: `(tail_ - 1) % N` só acerta quando N é potência de 2.
- `ciclo_*` com "objetos vivos" a mais: algum caminho (pop, clear, push_overwrite) avança
  o índice sem destruir o objeto do lugar.
- `ciclo_copia` com objetos vivos a menos ou "uso de objeto que não estava vivo": se você
  escolheu um array de bytes com placement new (`alignas(T) std::byte buf[N * sizeof(T)]`
  + `new (ptr) T(...)` + `ptr->~T()`), a cópia gerada pelo compilador copia bytes sem
  construir objetos. Escreva a cópia elemento a elemento ou declare-a `= delete`, e não
  esqueça o destrutor da fila. `std::launder` ajuda a acessar o objeto construído no buffer.
- `alocacao_*`: qualquer `std::vector`, `std::deque`, `std::unique_ptr` ou `new` dentro da
  fila conta como alocação.
