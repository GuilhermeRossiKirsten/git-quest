# Dicas — desafio-11

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

`std::move` não move nada: é só um *cast* para rvalue. Quem decide se haverá cópia ou
movimento é a sobrecarga escolhida depois. Então, para cada "movimento" do código,
pergunte: **existe** um construtor de movimento para esse tipo? Ele pode ser escolhido
para esse argumento? E quem está chamando — o `std::vector`, por exemplo — aceita usá-lo?

Comece pelos avisos do compilador em `build/compile.log` e pelos testes de `Buffer` e de
`Message`: os de `Batch` só melhoram depois que os tipos movem direito.

## Nível 2

Há seis lugares a investigar:

- `Message` em `src/message.h`: que função membro especial ela declara? O que isso faz
  com as operações de movimento que o compilador geraria? (*Effective Modern C++*, Item 17)
- `Buffer(Buffer&&)`: está correto, mas `std::vector` não o usa ao realocar. Por quê?
  Procure por `std::move_if_noexcept` (Item 14).
- `Buffer::operator=(Buffer&&)`: em que estado fica a origem? E se origem e destino forem
  o mesmo objeto?
- o construtor `Message(const std::string&, const Buffer&)` e `Batch::add`: o que acontece
  com um argumento temporário que chega por `const&`? (Item 41)
- `Message::compose`: o que `return std::move(msg);` impede o compilador de fazer? (Item 25)
- `Batch::dispatch`, `transform` e `drain`: o que `std::move` faz com um `const Message&`?
  E `pending_ = result;`, `out = pending_;`?

## Nível 3

Abordagem, item a item (sem código completo):

- `Message`: prefira a **regra do zero** — sem destrutor declarado, o compilador gera cópia
  e movimento corretos (e `noexcept` quando os membros movem sem lançar). Se quiser manter o
  destrutor virtual, declare as cinco operações com `= default` (e `noexcept` nas de movimento).
- `Buffer`: marque construção e atribuição por movimento com `noexcept`; na atribuição,
  trate `this == &other` e deixe a origem vazia (`data_` nulo, `size_` zero).
- Parâmetros *sink*: receba `std::string` e `Buffer` **por valor** e mova-os para os
  membros. Em `Batch::add`, use `emplace_back` com os argumentos movidos e aceite `Message`
  por valor.
- `compose`: devolva a variável local pelo nome (`return msg;`).
- `dispatch`/`transform`: itere com referência **não** `const` e mova cada elemento;
  `transform` pode trabalhar no próprio vetor. `drain`: troque (`swap`) ou mova o vetor
  em vez de copiá-lo.

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `message_copia_independente` falhou com "0 alocações"? Você está compartilhando o payload
  (`std::shared_ptr`?). Cópia explícita precisa ser profunda — economize cópias **movendo**, não aliasando.
- `message_compose_sem_movimento` acusa 1 movimento? Ainda há `std::move` no `return`.
- `message_move` reclama da origem não vazia? O movimento de algum membro foi escrito à mão
  e esqueceu de esvaziá-lo, ou uma cópia está acontecendo no lugar do movimento.
- `batch_dispatch_sem_copia` conta cópias mesmo com `std::move`? Olhe o `const` do iterador.
- `static_nothrow_move` falhou para `Message` mas passa para `Buffer`? O destrutor ainda está lá.
