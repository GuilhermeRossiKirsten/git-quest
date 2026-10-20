# desafio-2 · Lambdas sem fronteiras

> Trilha: cpp · Nível: intermediate · ~45 min · 150 pontos · Padrão: **C++14** (`-std=c++14 -pedantic-errors`) · Toolchain: **gcc 14.2.0**

## Contexto

O pipeline de auditoria da fintech onde você trabalha filtra, ordena, etiqueta e
entrega milhões de eventos por dia. Tudo isso passa por `gq/functional.hpp`, uma
pequena biblioteca header-only de utilitários funcionais escrita em 2013, "ainda
em C++11". O projeto passou a compilar com `-std=c++14` há anos, mas ninguém teve
coragem de mexer na biblioteca: ela tem um functor para cada coisa, `std::function`
em todo lugar e comentários que justificam gambiarras com "C++11 não deixa".

Depois de um incidente em que mensagens de auditoria saíram com etiquetas
corrompidas, você foi escalado para revisar a biblioteca. A equipe de plataforma
escreveu uma bateria de testes com a API esperada; agora é com você.

## Problema

Os testes e a própria demonstração (`src/main.cpp`) mostram sintomas como:

- o pipeline de demonstração é abortado pelo AddressSanitizer
  (`stack-use-after-return`) logo no primeiro evento;
- `gq::compose(inc, dbl)(5)` devolve `12`, mas a documentação promete `inc(dbl(5)) == 11`;
- ordenar um `std::vector` de objetos pesados com `gq::by(...)` faz dezenas de
  cópias dos elementos;
- `gq::apply_n(halve, 3)(10.0)` devolve `1` em vez de `1.25`;
- criar um segundo contador com `gq::make_counter(100)` altera o primeiro;
- o sink entregue a `gq::bind_sink` é destruído antes mesmo de ser usado
  (`heap-use-after-free`);
- `gq::tagger(...)` e `gq::at_least(...)` devolvem predicados que leem memória que
  não existe mais;
- `gq::is_small_v<std::unique_ptr<int>>` diz que é "barato passar por valor".

A API pública (nomes e forma de uso, documentados no topo de
`src/gq/functional.hpp`) deve ser mantida: os testes a usam com vários tipos
diferentes (`int`, `double`, `long long`, `std::string`, structs, tipos
move-only e um tipo instrumentado que conta cópias).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Escrever lambdas genéricas (`auto` nos parâmetros) em vez de um functor por tipo.
- Usar *init capture* para mover objetos move-only (como `std::unique_ptr`) para dentro de closures.
- Evitar modos de captura padrão (`[&]`, `[=]`) e referências pendentes.
- Encaminhar argumentos com `auto&&` + `std::forward<decltype(x)>(x)` sem cópias extras.
- Trocar `std::function` por dedução de tipo de retorno (`auto`) quando o tipo exato importa.
- Definir *variable templates* com *type traits*, em C++14 estrito.

## Conhecimentos necessários

- Lambdas de C++11 (lista de captura, `mutable`) e templates de função.
- `std::unique_ptr`, semântica de movimento e `std::move` (desafio-1).
- Noções de *type traits* (`<type_traits>`) e templates variádicos.
- Ler um relatório do AddressSanitizer (tipo de erro, pilha de chamadas, onde a memória foi liberada).

## Ambiente

- Docker >= 20.10. O compilador roda dentro de um container com a imagem fixada
  `gcc:14.2.0`, com `-std=c++14 -pedantic-errors -Wall -Wextra` e
  AddressSanitizer + UndefinedBehaviorSanitizer.
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: num Linux/WSL com **g++ >= 14** (com suporte a ASan/UBSan), rode
  `GITQUEST_LOCAL=1 ./test.sh`. Os resultados oficiais são os do container.

O código **precisa** compilar em C++14: recursos de C++17 (`if constexpr`, *fold
expressions*, variáveis `inline`, CTAD, `std::invoke`, `std::string_view`) são
rejeitados pelo compilador com `-pedantic-errors`.

## Como começar

```bash
./start.sh        # constrói a imagem de toolchain e compila src/main.cpp
./test.sh         # mostra quais critérios ainda falham e por quê
```

Para experimentar à mão dentro do container (com o mesmo compilador e sanitizers):

```bash
docker run --rm -it -v "$PWD":/work -w /work gcc:14.2.0 bash
make build && ./build/pipeline
```

No Git Bash do Windows, prefixe o `docker run` com `MSYS_NO_PATHCONV=1` e use `-v "$(pwd -W)":/work`.

## Arquivos que você pode modificar

- `src/gq/functional.hpp` — a biblioteca (onde estão os problemas).
- `src/main.cpp` — o pipeline de demonstração (precisa continuar compilando e rodando limpo).

Não altere `tests/`, `test.sh`, `challenge.yaml` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Cada critério é um executável separado (`tests/test_*.cpp`), compilado com
sanitizers: um crash ou um erro de compilação reprova só aquele critério. Os logs
completos ficam em `build/tests/*.log`. Lendo os testes você vê exatamente o que é
esperado de cada utilitário.

## Critérios de conclusão

- [ ] `src/main.cpp` compila em C++14 estrito **sem avisos** e roda limpo no ASan/UBSan.
- [ ] `compose`: ordem `f(g(x))`, qualquer número de funções, tipos mistos, callables move-only e nenhuma cópia além das que as próprias funções exigem.
- [ ] `by`: comparador genérico por projeção que não copia os elementos.
- [ ] `apply_n`: o tipo do resultado é deduzido (nada de truncar para `int`).
- [ ] `make_counter`: cada contador, e cada cópia de um contador, tem estado próprio.
- [ ] `bind_sink`: o callable é dono do sink — ele vive enquanto o callable viver e é destruído exatamente uma vez.
- [ ] `tagger`/`at_least`: as closures continuam válidas depois que a função que as criou retornou.
- [ ] `is_small_v`: só é `true` para tipos trivialmente copiáveis que cabem em dois ponteiros.

`./test.sh` termina com código 0 somente quando os 8 critérios passam.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Effective Modern C++*, Scott Meyers — Item 31 (evite modos de captura padrão), Item 32 (use init capture para mover objetos para closures), Item 33 (use `decltype` em parâmetros `auto&&` para `std::forward`), Item 34 (prefira lambdas a `std::bind`) e Item 21 (prefira `std::make_unique`).
- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — Capítulo 3, *Exploring Functions* (lambdas genéricas, templates variádicos, composição de funções).
- cppreference: [Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda), [Variable template](https://en.cppreference.com/w/cpp/language/variable_template), [Return type deduction](https://en.cppreference.com/w/cpp/language/function#Return_type_deduction), [`std::is_trivially_copyable`](https://en.cppreference.com/w/cpp/types/is_trivially_copyable).
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer) — como ler os relatórios de `heap-use-after-free` e `stack-use-after-return`.

## Próximo desafio

**desafio-4 — Contratos de template** (C++20): com templates genéricos em mãos,
você vai usar *concepts* para que eles rejeitem tipos inválidos com erros claros.
