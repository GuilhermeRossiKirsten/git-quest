# Dicas — desafio-4

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Os quatro concepts de `src/gq/concepts.hpp` aceitam qualquer tipo. Ainda que
você os defina corretamente, algum template da `gq` passa a recusar algo se
ninguém o usa? E o que muda entre um uso inválido que falha **dentro do corpo**
do template e um que é recusado **na assinatura**? Os testes de
`tests/contracts/` só enxergam a assinatura.

Nem todo problema é de tipo: um range vazio, ou `lo > hi`, só aparece em
execução. Qual parte de cada contrato o compilador consegue garantir, e qual
precisa de uma verificação em runtime?

## Nível 2

- `<concepts>` já tem `std::integral`, `std::floating_point`, `std::same_as`, `std::equality_comparable`, `std::copyable`, `std::convertible_to`.
- `<ranges>` tem `std::ranges::input_range`, `std::ranges::output_range` e `std::ranges::range_value_t`.
- `std::vector<bool>` e `std::string` também são ranges: o tipo dos elementos é que importa.
- Um class template pode ter uma constraint (`template <Hashable Key> class ...`), e uma função-membro também (`... snapshot() const requires ...`).
- Para checar se `std::hash<T>` funciona, use uma `requires`-expression que crie e chame o objeto hash.
- Quando a biblioteca rejeitar usos inválidos, `src/report.cpp` deixa de compilar: leia o erro, que agora deve citar o contrato, e corrija o relatório.

## Nível 3

Abordagem, arquivo por arquivo:

1. `concepts.hpp`: escreva `Numeric` como "inteiro ou ponto flutuante" menos `bool` e menos uma lista de tipos de caractere (um concept auxiliar ajuda). `NumericRange` é *input range* + `Numeric` do `range_value_t`. `Hashable` combina `equality_comparable` com uma `requires`-expression sobre `std::hash<T>{}(v)`. `Serializable` exclui ponteiros e aceita `bool`, `Numeric` e o que converte para `std::string_view`.
2. `stats.hpp`: troque `class` pelos concepts nos parâmetros de template. `mean` acumula em `double`, conta os elementos e lança se não houver nenhum. Em `clamp_all`, exija também que o range aceite escrita (`output_range`), valide `lo <= hi` **antes** de alterar qualquer elemento e converta os limites para o tipo do elemento.
3. `histogram.hpp`: restrinja `Key` com `Hashable` e com uma requires-clause que exclui ponteiros.
4. `serialize.hpp`: restrinja com `Serializable` e use `if constexpr` para escolher entre bool, número (`std::to_chars` dá a forma mais curta que preserva o valor) e texto (aspas + escape).
5. `registry.hpp`: `snapshot()` deve ser `const`, copiar `entries_` e existir apenas `requires std::copyable<T>`.
6. `report.cpp`: use `Histogram<std::string>` e calcule a disponibilidade contando os `true`.

## Nível 4

Se um caso de `tests/contracts/` ainda reclama, compile só ele (comando no
README) e leia as notas `required for the satisfaction of ...`: elas mostram
qual parte da constraint foi avaliada e com quais tipos. Lembre que
`clamp_all(v, 0, 1)` com `std::vector<double>` precisa continuar compilando,
então os limites não podem ser obrigados a ter exatamente o tipo do elemento.
