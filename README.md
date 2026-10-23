# desafio-28 · O switch que crescia

> Trilha: cpp · Padrão: **C++20** · Toolchain: **gcc:14.2.0** · Nível: intermediate · ~60 min · 200 pontos

## Contexto

O motor de promoções do caixa nasceu com duas regras e um `switch`. Hoje são quatro
regras, um `enum Kind`, quatro funções com `switch`/`static_cast` e uma exportação
que outra equipe escreveu com `dynamic_cast` em cascata. A wiki interna explica como
acrescentar uma promoção: "mexa no enum, crie a classe, trate o caso em todos os
switches que encontrar e recompile tudo".

Agora o comercial fechou com a **Acme**, um parceiro que vai escrever as próprias regras
(cashback, combos, descontos por volume) **num módulo separado, sem acesso ao núcleo**.
O requisito é claro: *tipos novos de promoção entram sem que ninguém edite `src/`*, e o
que já existe continua dando exatamente o mesmo resultado — centavo por centavo.

## Problema

O código compila e a maioria dos cupons sai certa. O que você vai observar:

- a exportação JSON da loja termina em `{"type":"unknown"}` (o frete grátis "sumiu");
- abrir um segundo PDV copiando o caixa (`Checkout pdv2 = loja;`) derruba a demonstração com
  um erro do UBSan: *member call on address ... which does not point to an object of type 'BuyXPayY'*;
- `promo::Promotion p = promo::BuyXPayY("cafe", 3, 2);` **compila**, mas usar `p` dá o mesmo
  erro; `std::vector<promo::Promotion>` e `Checkout::promotions()` também;
- o plugin da Acme (`tests/plugin_acme.cpp`) não compila: `Checkout::add` só aceita classes
  derivadas de `Promotion`, e `promo::PromotionRule` nem existe;
- uma regra incompleta (sem `label()`, por exemplo) não tem como ser recusada com uma mensagem
  clara — não há contrato algum para uma regra.

Ferramentas: a saída de `./start.sh` (a demonstração `src/main.cpp`), os relatórios do
UBSan/ASan/LSan em `build/logs/<teste>.log`, a mensagem de erro do compilador ao compilar os
plugins e os testes de contrato.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Reconhecer por que `enum` + `switch` espalhado (e `dynamic_cast` em cascata) viola o
  princípio aberto/fechado e quebra em silêncio quando alguém esquece um caso.
- Identificar fatiamento (*slicing*) e a "cópia que perde o tipo" em hierarquias copiadas por valor.
- Substituir o despacho por tipo por **Type Erasure** (External Polymorphism + Bridge + Prototype):
  um tipo valor que aceita qualquer regra, sem herança.
- Expressar o contrato de uma regra com um **concept** C++20 e rejeitar tipos inválidos em
  tempo de compilação.
- Preservar a semântica de valor: cópias independentes, movimento barato e `noexcept`, sem vazamentos.
- Comparar com as alternativas (hierarquia virtual, `std::variant`) e saber o que cada uma
  deixa aberto: tipos ou operações.

## Conhecimentos necessários

- Desafio-11 (move semantics, `noexcept`, regra do zero/cinco).
- Herança, funções virtuais, `std::unique_ptr`.
- Templates e o básico de concepts/requires-expressions (desafio-4 ajuda).

## Ambiente

- Padrão exigido: **C++20** (`-std=c++20 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, ASan/UBSan/LSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make locais, com `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh` (no Windows/MinGW os sanitizers ficam desligados; o resultado
  oficial é o do container).

Cada teste roda em um processo próprio, com AddressSanitizer, UndefinedBehaviorSanitizer e
LeakSanitizer, dentro de um container sem rede e com limites de CPU, memória e tempo. Os testes
de alocação substituem o `operator new` global e contam as chamadas.

## Como começar

```bash
./start.sh          # constrói a imagem, compila e roda a demonstração (leia a saída!)
./test.sh           # mostra cada critério com ✔/✘ e o motivo
```

Para investigar: `cat build/logs/valor_copia_do_caixa.log` (relatório completo de um teste),
`cat build/logs/compile_plugin_acme.log` (erro completo ao compilar um plugin).

### A API pública (o contrato que os plugins usam)

Os plugins e os testes incluem **só** `promo/promotion.hpp` e `promo/checkout.hpp` e usam
apenas o que está abaixo. O design interno é seu.

**Uma regra de promoção** é qualquer tipo comum (sem herdar de nada) que satisfaça o concept
`promo::PromotionRule<R>`, que você deve definir em `promo/promotion.hpp`:

| Requisito | Significado |
|---|---|
| `r.discount(cart)` em um `const R&`, com `const promo::Cart&` | devolve **exatamente** `std::int64_t` (centavos, >= 0) |
| `r.label()` em um `const R&` | algo conversível para `std::string` (linha do cupom) |
| `r.to_json()` em um `const R&` | algo conversível para `std::string` (objeto JSON) |
| `R` copiável | promoções são valores |

Tipos que não cumprem o contrato (sem `label()`, `discount()` devolvendo `double` ou não-`const`,
tipos só-movíveis, `int`, `std::string`, ponteiros...) devem ser **rejeitados em tempo de
compilação** por `promo::PromotionRule`, pela construção de `promo::Promotion` e por
`Checkout::add` — e o erro deve citar `PromotionRule`.

**`promo::Promotion`** é um tipo valor:

```cpp
promo::Promotion p = acme::Cashback(10000, 700);   // conversão implícita de qualquer regra
promo::Promotion q{promo::PercentOff(10, "bebidas")};
promo::Promotion c = p;                            // cópia independente (nada de fatiamento)
p = promo::FreeShipping(15000);                    // reatribuir não afeta c
std::int64_t d = promo::discount(p, cart);         // funções livres no namespace promo
std::string  l = promo::label(p);
std::string  j = promo::to_json(p);
```

- copiável e atribuível; mover (construção e atribuição) é `noexcept` e não aloca;
- construir ou copiar faz no máximo 2 alocações (fora as da própria regra);
- regras de qualquer tamanho são aceitas.

**Promoções embutidas** (mesmos construtores de hoje): `promo::PercentOff(int percent, std::string category)`,
`promo::FixedOff(std::int64_t cents, std::int64_t min_subtotal)`, `promo::BuyXPayY(std::string sku, int x, int y)`,
`promo::FreeShipping(std::int64_t min_subtotal)`. O JSON do frete grátis é
`{"type":"free_shipping","min_subtotal":15000}`.

**`promo::Checkout`** (copiável, movimento `noexcept`): `add(regra ou Promotion)`, `size()`,
`promotions()` (um `std::vector<promo::Promotion>` com cópias independentes, na ordem de inserção),
`total(cart)` (não aloca), `receipt(cart)` e `export_json()` — com os formatos atuais.

`promo/cart.hpp` (`Item`, `Cart`, `format_brl`) não precisa mudar.

## Arquivos que você pode modificar

- `src/promo/` — o núcleo: `promotion.hpp/.cpp`, `checkout.hpp/.cpp`, `cart.hpp/.cpp`; você pode
  criar, renomear ou apagar arquivos aqui (todo `src/promo/*.cpp` é compilado);
- `src/main.cpp` — a demonstração.

Não altere `tests/`, `test.sh`, `start.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`: o
avaliador usa as versões originais. Em particular, **não é permitido** citar os tipos dos plugins
dentro de `src/` — e nem daria: eles só existem nos arquivos `tests/plugin_*.cpp`.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (g++ >= 14)
```

A saída é dividida em seções (compilação, referência, plugins, semântica de valor, memória,
contratos); cada critério aparece com ✔ ou ✘ e, quando falha, o valor esperado e o obtido,
a mensagem do sanitizer com a linha de `src/` ou o `static_assert` violado.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] núcleo, demonstração e testes compilam com g++ 14 em `-std=c++20 -pedantic-errors` e a demonstração roda sem erro;
- [ ] **referência**: total, cupom e JSON de todas as promoções embutidas idênticos aos de hoje (e o frete grátis exportado corretamente);
- [ ] **extensão**: `tests/plugin_acme.cpp` e `tests/plugin_tracked.cpp` (unidades de tradução separadas) compilam e funcionam usando só a API pública, misturados com as promoções embutidas;
- [ ] **valor**: `Promotion p = Derivada(...)`, `std::vector<Promotion>`, `promotions()` e cópias de `Checkout` preservam a promoção inteira, são independentes do original e sobrevivem a ele;
- [ ] **memória**: sem vazamentos nem erros do ASan/UBSan; mover não aloca; construir/copiar <= 2 alocações; 100 `push_back` sem `reserve` <= 150 alocações; `total()` não aloca; toda cópia de regra é destruída exatamente uma vez;
- [ ] **contrato**: `promo::PromotionRule` aceita regras válidas e rejeita as inválidas em tempo de compilação (concept-sondas em `tests/contracts/`), e o erro do compilador cita `PromotionRule`.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++ Software Design*, Klaus Iglberger — cap. 1, Guideline 2 ("Design for Change"); cap. 2,
  Guideline 5 ("Design for Extension", OCP) e Guideline 7 (base classes × concepts); cap. 4,
  Guideline 15 ("Design for the Addition of Types or Operations": a solução procedural com `enum` +
  `switch`) e Guideline 17 (`std::variant`: operações abertas, tipos fechados); cap. 5, Guidelines 19,
  22 e 23 (Strategy, value semantics, Strategy baseado em valores); cap. 7, Guidelines 30 e 31
  (Prototype, External Polymorphism); cap. 8, Guidelines 32–34 (Type Erasure, SBO e custos).
- *Hands-On Design Patterns with C++* (2ª ed.), Fedor G. Pikus — cap. 6 "Understanding Type Erasure"
  (type erasure usando herança, sem alocação, e "Guidelines for using type erasure"); cap. 13
  "Virtual Constructors and Factories" (seção "Polymorphic copy").
- *Refactoring with C++*, Dmitry Danilov — cap. 2 (Open-Closed Principle); cap. 4 "Identifying
  Ideal Candidates for Rewriting", seção "Smelly code and its basic characteristics" (Strategy no
  lugar de um `switch` por tipo).
- Sean Parent, "Inheritance Is the Base Class of Evil" (GoingNative 2013).
- cppreference: [constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints),
  [`std::variant`](https://en.cppreference.com/w/cpp/utility/variant), [object slicing](https://en.cppreference.com/w/cpp/language/object#Polymorphic_objects).

## Próximo desafio

**desafio-29 — Metros mais segundos**: depois de deixar o compilador recusar regras malformadas,
use o sistema de tipos para recusar *contas* malformadas.
