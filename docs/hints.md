# Dicas — desafio-28

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Quantos lugares do código você teria de editar para acrescentar uma quinta promoção? Conte os
`switch (promotion.kind())`, os `static_cast` e os `dynamic_cast`. Agora olhe os sintomas: o
`{"type":"unknown"}` da exportação e o erro do UBSan ao copiar o caixa são o mesmo problema —
alguém esqueceu um desses lugares.

Depois pergunte: o que exatamente é copiado em `promo::Promotion p = promo::BuyXPayY(...)`?
E em `out.push_back(*p)` dentro de `Checkout::promotions()`? Qual o tamanho de um
`Promotion` comparado com um `BuyXPayY`?

## Nível 2

Onde olhar:

- `src/promo/promotion.cpp`: `discount`, `label`, `to_json` e `clone_promotion` são quatro
  cópias da mesma decisão "qual é o tipo?" (Iglberger, Guideline 15). Repare qual caso cada
  uma esqueceu.
- `src/promo/checkout.hpp`: `std::vector<Promotion>` por valor e `make_unique<Promotion>(...)`
  guardam só a parte base — fatiamento.
- `tests/plugin_acme.cpp`: o parceiro escreve classes **comuns**, sem herdar de nada. Então a
  abstração não pode exigir herança dos tipos concretos: ela precisa ser *externa*
  (Guideline 31, External Polymorphism) e precisa ser um *valor* (Guideline 22).
- Trocar os `switch` por funções virtuais resolve o "esqueci um caso", mas não o fatiamento
  nem o plugin; trocar por `std::variant` resolve o fatiamento, mas fecha o conjunto de tipos
  (Guideline 17).

## Nível 3

A abordagem é Type Erasure (Iglberger, Guideline 32; Pikus, cap. 6):

1. Transforme as quatro promoções embutidas em classes comuns, cada uma com seus próprios
   `discount(const Cart&) const`, `label() const` e `to_json() const` — a lógica dos `switch`
   vai para dentro de cada tipo, e o `enum Kind` desaparece.
2. Escreva o concept `promo::PromotionRule` com uma requires-expression sobre um `const R&`
   (retorno de `discount` verificado com `std::same_as<std::int64_t>`) e `std::copy_constructible`.
3. Faça `promo::Promotion` guardar um ponteiro para uma interface privada ("Concept") com as
   três operações e um `clone()`; um template de classe ("Model<R>") implementa essa interface
   para qualquer `R` que satisfaça o concept. O construtor de `Promotion` é um template
   restrito pelo concept; a cópia usa `clone()` (Prototype); o movimento é o do ponteiro.
4. `Checkout` passa a guardar `std::vector<promo::Promotion>` e volta à regra do zero;
   `add` recebe uma `Promotion` por valor.

## Nível 4

Armadilhas comuns:

- Ordem do concept: se `std::copy_constructible<R>` vier antes da requires-expression, testar
  `PromotionRule<Promotion>` pergunta "Promotion é copiável?", o que consulta o próprio
  construtor restrito — recursão na satisfação do concept. Coloque primeiro a parte que já é
  falsa para `Promotion`.
- Um construtor template **sem** restrição "aceita" qualquer tipo na declaração; o erro só
  aparece dentro do corpo, longe do usuário, e os testes de contrato reprovam.
- Declarar destrutor ou cópia sem declarar o movimento faz o movimento virar cópia (desafio-11).
- Chamadas qualificadas `promo::label(p)` não encontram *hidden friends*: declare as funções
  livres também no namespace.
