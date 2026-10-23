# Dicas — desafio-29

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Quando `10.0_m + 2.0_s` compila, **qual** `operator+` foi escolhido? O
template `operator+(Quantity<D, R>, Quantity<D, R>)` não consegue deduzir um
único `D`... então o compilador procurou outro caminho. Que conversões
implícitas a classe `Quantity` oferece — de `double`, para `double` e de
outras `Quantity`? Cada uma delas é uma porta aberta.

E por que `500.0_m + 1.0_km` e `1.0_km + 500.0_m` dão resultados diferentes?
Olhe o que um objeto `Quantity` guarda além do número.

## Nível 2

Onde olhar em `src/units/units.hpp`:

- os construtores de `Quantity` (quais não são `explicit`?), o `operator Rep()` e o construtor-template que aceita `Quantity<D2, Rep>`;
- o membro `scale_` e quem o usa: ele dobra o tamanho, e `operator+` só olha a escala do operando da esquerda;
- o construtor de cópia escrito à mão — o que ele faz que a cópia gerada pelo compilador não faria? O que ele custa (`is_trivially_copyable`, `constexpr`)?
- `dim_product_impl` / `dim_quotient_impl` (confira os sinais expoente por expoente) e o tipo de retorno de `operator*` entre duas grandezas;
- os literais: `constexpr`? as duas sobrecargas (`long double` e `unsigned long long`) fazem a mesma conta?

Use `static_assert(std::is_same_v<decltype(expr), void>)` para o compilador
lhe mostrar o tipo de `expr`.

## Nível 3

Abordagem (o livro do Stroustrup, §28.7, segue o mesmo caminho):

1. Guarde **só** um `Rep`, sempre na unidade SI base; quem converte km → m, lbf → N é o literal, na construção. A cópia, a atribuição e a destruição geradas pelo compilador bastam.
2. Construtor a partir de `Rep` `explicit`; nenhum operador de conversão implícito; nenhum construtor entre dimensões diferentes.
3. `+`, `-`, comparações: só entre o mesmo tipo. Funções amigas definidas dentro da classe (não-template) ou templates com `D` deduzido dos dois lados — e um `operator<=>` `= default` dá as seis comparações.
4. `*` e `/` entre grandezas devolvem `Quantity<dim_product<…>>` / `Quantity<dim_quotient<…>>`; com número puro, a dimensão não muda (`1.0 / q` inverte).
5. Marque tudo `constexpr` (construtores, `value()`, operadores, literais).
6. Recompile `src/telemetry.cpp` e `src/mission/burns.hpp`: o compilador vai apontar cada lugar onde um `double` "sem unidade" entrava — use o literal que diz a unidade real do dado.

## Nível 4

Se uma recusa falhar com "o erro não aponta a linha": o avaliador exige que
o erro do compilador cite a linha marcada no arquivo de teste. Erros por
"nenhum operador viável" ou "conversão impossível" já fazem isso; um
`static_assert` dentro do corpo de um template também (o g++ mostra a linha
em "required from here"). Se o erro sai só de dentro de `units.hpp`, a
recusa provavelmente aconteceu por outro motivo — compile o arquivo com e sem
`-DGQ_VALID` e compare.
