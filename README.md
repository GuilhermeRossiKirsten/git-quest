# desafio-11 · Move semantics sem cópias escondidas

> Trilha: cpp · Padrão: **C++11** · Toolchain: **gcc:14.2.0** · Nível: intermediate · ~45 min · 150 pontos

## Contexto

O barramento de mensagens interno da empresa foi "modernizado para C++11" há alguns
anos: alguém espalhou `std::move` pelo código, escreveu construtores de movimento e
declarou vitória. Mesmo assim, o serviço que despacha lotes de mensagens de ~100 KB
consome o dobro de CPU que deveria e o perfilador mostra `memcpy` e `operator new`
no topo, bem longe de qualquer lugar onde alguém tenha *pedido* uma cópia.

Sua missão: fazer o pipeline `add → transform → dispatch` mover os payloads em vez de
copiá-los, **sem abrir mão da semântica de valor** — quem copia explicitamente uma
`Message` ainda espera receber uma mensagem independente.

## Problema

O código compila e as mensagens chegam corretas. O que você vai observar com os testes:

- despachar 1000 mensagens faz **milhares de cópias profundas** de payload
  (cerca de 1 GiB copiado à toa), quando o orçamento é zero;
- `Message b(std::move(a))` copia o payload, e a origem continua "cheia";
- um `std::vector<Buffer>` copia todos os buffers a cada realocação, embora
  `Buffer` tenha um construtor de movimento escrito à mão;
- construir uma `Message` a partir de um `Buffer` temporário custa uma cópia;
  `Message::compose()` custa ainda mais;
- `b = std::move(a)` entre dois `Buffer`s termina em **double free**, e `a = std::move(a)`
  em **heap-use-after-free** (relatados pelo AddressSanitizer);
- `static_assert(std::is_nothrow_move_constructible<...>)` falha;
- o compilador emite alguns avisos (`-Wall`) que valem a leitura.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Medir cópias escondidas e encontrar o ponto exato em que um "movimento" vira cópia.
- Entender quando o compilador gera (ou deixa de gerar) as operações de movimento.
- Usar `noexcept` para que `std::vector` mova em vez de copiar ao realocar.
- Aplicar parâmetros *sink*, `emplace_back` e retorno por valor (NRVO) sem `std::move` desnecessário.
- Evitar `std::move` em objetos `const`; deixar objetos movidos válidos, inclusive em automovimentação.
- Manter a semântica de valor: cópia explícita continua sendo profunda e independente.

## Conhecimentos necessários

- Desafio-1 (RAII, regra dos três/cinco/zero, `std::unique_ptr`).
- Referências lvalue/rvalue (`T&`, `T&&`) e o que `std::move` faz (um *cast*, nada mais).
- `std::vector`, `std::map`, `std::function` e lambdas do C++11.

## Ambiente

- Padrão exigido: **C++11** (`-std=c++11 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, ASan/UBSan/LSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`.

Tudo compila com `-fsanitize=address,undefined` e cada teste roda em um processo próprio,
dentro de um container sem rede e com limites de CPU, memória e tempo.

**Como as cópias são medidas:** o binário de testes substitui o `operator new` global e conta
os blocos alocados com o tamanho exato de um payload (100003 bytes). Cada bloco desses é um
payload criado ou copiado — não importa como você implementa `Buffer` ou `Message`.
Além disso, `src/lifecycle_probe.h` conta quantas vezes uma `Message` é copiada ou movida.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial (leia os avisos!)
./test.sh           # mostra quais critérios falham e quantas cópias foram feitas
```

Para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                    # log completo de cada teste que falhar
cat build/logs/buffer_move_atribuicao.log # relatório do ASan de um teste específico
cat build/compile.log                     # avisos do compilador
```

Ferramentas úteis: os avisos do g++ (`-Wpessimizing-move`, `-Wredundant-move`), o relatório
do AddressSanitizer (primeira linha da pilha em `src/`) e `std::is_nothrow_move_constructible`
/ `std::is_move_constructible` em um `static_assert` seu.

## Arquivos que você pode modificar

- `src/buffer.h` e `src/buffer.cpp` — o bloco de bytes `Buffer` (payload);
- `src/message.h` e `src/message.cpp` — a `Message`;
- `src/batch.h` e `src/batch.cpp` — o lote `Batch`.

Você pode mudar membros privados, includes, funções membro especiais e a **forma** dos
parâmetros (ex.: `const T&` → `T`), desde que as chamadas dos testes continuem compilando e
o contrato descrito nos comentários dos headers seja respeitado. `Message` precisa continuar
guardando o membro `probe_`.

Não altere `src/lifecycle_probe.h`, `tests/`, `test.sh`, `challenge.yaml`, `Makefile`
nem `Dockerfile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada critério aparece com ✔ ou ✘ e, quando falha, o motivo: quantas cópias profundas foram
feitas (e quantas eram permitidas), a verificação que falhou ou o erro de memória e a linha
de `src/` onde ele nasceu.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o projeto compila com g++ 14 em `-std=c++11 -pedantic-errors`;
- [ ] `static_assert`: construção e atribuição por movimento de `Buffer` e `Message` são `noexcept`, e `Message` continua copiável;
- [ ] mover `Buffer` ou `Message` (construção e atribuição) não copia o payload e deixa a origem **vazia** e reutilizável;
- [ ] `a = std::move(a)` deixa o `Buffer` válido; nenhum double free, uso após liberação ou vazamento;
- [ ] `std::vector<Buffer>` realoca sem copiar; `compose()` não copia nem move a `Message`;
- [ ] construir uma `Message` e fazer `add`, `transform`, `dispatch` e `drain` com temporários/movidos: **0 cópias profundas**;
- [ ] pipeline de 1000 mensagens de ~98 KiB: exatamente 1000 alocações de payload (as criações);
- [ ] copiar explicitamente uma `Message` (ou `add` de um lvalue) faz exatamente 1 cópia profunda e o resultado é independente do original.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Effective Modern C++*, Scott Meyers — Item 14 (declare funções `noexcept` se não lançam), Item 17 (geração de funções membro especiais), Item 23 (`std::move` e `std::forward`), Item 25 (`std::move` em rvalue refs; não atrapalhe a RVO), Item 29 (assuma que movimentos podem não existir, ser baratos ou ser usados) e Item 41 (passagem por valor para parâmetros copiáveis e baratos de mover).
- *C++ High Performance* (2ª ed.), Björn Andrist e Viktor Sehr — cap. 2 "Essential C++ Techniques", seção "Move semantics explained" (regra do zero, `noexcept`, "não mova quando a cópia já é elidida").
- cppreference: [move constructors](https://en.cppreference.com/w/cpp/language/move_constructor), [`std::move_if_noexcept`](https://en.cppreference.com/w/cpp/utility/move_if_noexcept), [copy elision](https://en.cppreference.com/w/cpp/language/copy_elision), [regra dos três/cinco/zero](https://en.cppreference.com/w/cpp/language/rule_of_three).

## Próximo desafio

**desafio-2 — Lambdas sem fronteiras** (C++14): lambdas genéricas, *init capture* (que
finalmente deixa você **mover** algo para dentro de uma lambda) e `std::make_unique`.
