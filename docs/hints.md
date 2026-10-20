# Dicas — desafio-2

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Para cada closure que a biblioteca devolve, pergunte: **o que ela guarda, e por
quanto tempo isso vive?** Uma closure costuma viver muito mais do que a função que
a criou. Comece pelos relatórios do AddressSanitizer: a linha `SUMMARY` diz o tipo
de erro e a pilha mostra *onde a memória foi liberada* — compare com o lugar em
que a lambda foi criada.

E para os sintomas que não são crashes: o que acontece com o *tipo* de um valor
quando ele passa por um `std::function<int(int)>`? Quantas vezes um argumento
recebido por valor é copiado antes de chegar à função de verdade?

## Nível 2

Onde olhar em `src/gq/functional.hpp`:

- `tagger` e `at_least`: a lista de captura `[&]` (Effective Modern C++, Item 31).
- `bind_sink`: o comentário "não dá para mover um unique_ptr para dentro de uma
  lambda" era verdade em C++11. Em C++14 existe *init capture* (Item 32).
- `make_counter`: a variável `static` é compartilhada por todas as closures.
- `compose`: compare a ordem em que `f_` e `g_` são chamadas com a documentação;
  depois veja como o argumento `T x` é repassado.
- `By::operator()` recebe `T a, T b` — por valor.
- `apply_n`: o tipo de retorno e o parâmetro estão fixos em `int`.
- `is_small`: o comentário promete duas condições; o código verifica uma.

## Nível 3

Abordagem:

- Troque functors e `std::function` por funções com retorno `auto` que devolvem
  lambdas. Lambdas genéricas (`auto` / `const auto&` nos parâmetros) resolvem a
  genericidade sem um functor por tipo.
- Capture *por valor*, nomeando cada captura. Para tipos move-only, use
  `[nome = std::move(objeto)]`. Uma closure que é dona de um `unique_ptr` é
  move-only: **ela não cabe num `std::function`**, então quem a recebe (inclusive
  `src/main.cpp`) precisa guardá-la em `auto`. Aproveite para trocar
  `std::unique_ptr<T>(new T(...))` por `std::make_unique<T>(...)` (Item 21).
- Estado próprio por closure: um membro criado com init capture numa lambda
  `mutable`.
- Encaminhamento sem cópias: parâmetro `auto&&` + `std::forward<decltype(x)>(x)` (Item 33).
- `compose` variádico em C++14: sem *fold expressions*. Use recursão — um caso
  base com uma função e um caso geral que captura a primeira função e a
  composição das demais.
- `is_small_v`: combine `sizeof` com `std::is_trivially_copyable<T>::value` numa
  variable template `constexpr` (variáveis `inline` são C++17 e não compilam aqui).

## Nível 4

Se um critério reprova com "não compila", leia a primeira linha `error:` e a
linha `required from here` mais próxima de `src/`. O caso mais comum depois de
corrigir `bind_sink` é `std::function target must be copy-constructible`: algum
lugar ainda tenta guardar a closure move-only num `std::function`.

Se `compose` ainda copia: confira que nenhum nível da recursão recebe o argumento
por valor nem o repassa como lvalue (`inner(x)` em vez de
`inner(std::forward<decltype(x)>(x))`).
