# desafio-1 · O vazamento silencioso

> Trilha: cpp · Padrão: **C++11** · Toolchain: **gcc:14.2.0** · Nível: beginner · ~45 min · 100 pontos

## Contexto

O serviço de chat da empresa roda há anos sobre um pequeno módulo de sessões escrito
"no C++ de antigamente": `new`, `delete` e ponteiros crus por toda parte. Ninguém mexe
nele porque "funciona". Só que o processo precisa ser reiniciado toda madrugada: a
memória sobe sem parar ao longo do dia. E na semana passada um colega tentou guardar
sessões num `std::vector<Session>` e o servidor caiu com `free(): double free detected`.

Você foi escalado para consertar o módulo **sem reescrever o serviço**: a interface
pública (`src/session.h` e `src/session_manager.h`) é usada por outras equipes.

## Problema

Rodando os testes com os sanitizers ligados, você vai observar:

- o processo termina com relatórios do **LeakSanitizer** (`detected memory leaks`)
  mesmo em testes cujas verificações funcionais passam;
- `Session::live_count()` continua maior que zero depois que o `SessionManager`
  saiu de escopo, ou depois de `close()`/`close_all()`;
- quando o construtor de `Session` ou o `open()` lançam exceção (usuário inválido,
  capacidade inválida, gerenciador lotado), memória fica para trás;
- copiar ou mover uma `Session` — ou deixá-la num `std::vector` que realoca — termina
  em **double free** ou **heap-use-after-free** relatados pelo AddressSanitizer;
- mover um `SessionManager` "funciona", mas nada é liberado (ou, depois de um conserto
  apressado, tudo é liberado duas vezes).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Ler relatórios do AddressSanitizer/LeakSanitizer e chegar à linha que aloca o recurso perdido.
- Aplicar RAII: todo recurso tem um dono cujo destrutor o libera, inclusive quando uma exceção interrompe o caminho.
- Usar `std::unique_ptr` (para objetos e para arrays) para expressar posse exclusiva em C++11.
- Aplicar a regra dos três/cinco/zero: decidir entre cópia profunda, cópia proibida (`= delete`) e movimento.

## Conhecimentos necessários

- Classes, construtores e destrutores em C++.
- `new`/`delete` e `new[]`/`delete[]`.
- Noções de exceções (`throw`, `try`/`catch`) e de `std::vector`.

## Ambiente

- Padrão exigido: **C++11** (`-std=c++11 -pedantic-errors`). Não há `std::make_unique` em C++11.
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, ASan/UBSan/LSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux (o LeakSanitizer não funciona no macOS), usando
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`.

Tudo compila com `-fsanitize=address,undefined` e cada teste roda em um processo próprio
com `ASAN_OPTIONS=detect_leaks=1`, dentro de um container sem rede e com limites de CPU,
memória e tempo.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial
./test.sh           # mostra quais critérios falham e por quê
```

Para investigar um teste específico com o relatório completo dos sanitizers:

```bash
GQ_VERBOSE=1 ./test.sh          # imprime o log inteiro de cada teste que falhar
cat build/logs/session_move.log # ou leia o log de um teste depois da execução
```

Nos relatórios do ASan/LSan, procure a primeira linha da pilha que aponta para `src/`:
ela diz **onde** a memória foi alocada (vazamento) ou liberada pela primeira vez (double free).

## Arquivos que você pode modificar

- `src/session.h` e `src/session.cpp` — a classe `Session`;
- `src/session_manager.h` e `src/session_manager.cpp` — a classe `SessionManager`.

Você pode mudar membros privados, includes e as funções membro especiais (construtores,
destrutores, atribuições) — inclusive adicioná-las, removê-las ou marcá-las `= default`/`= delete`.
Mantenha a **API pública** descrita nos comentários dos headers (nomes, parâmetros,
retornos, exceções lançadas): os testes dependem dela. Copiar uma `Session` pode ser
proibido ou implementado como cópia profunda — as duas escolhas são aceitas; mover é obrigatório.

Não altere `src/instance_counter.h`, `tests/`, `test.sh`, `challenge.yaml`, `Makefile`
nem `Dockerfile`: o avaliador usa as versões originais (o padrão C++11 e as flags fazem
parte do desafio).

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada critério aparece com ✔ ou ✘ e, quando falha, o motivo: a verificação que falhou,
o tipo de erro de memória e a linha de `src/` onde ele nasceu.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o projeto compila com g++ 14 em `-std=c++11 -pedantic-errors` (sem `std::make_unique`);
- [ ] nenhum teste relata vazamento (LeakSanitizer), double free, uso após liberação (AddressSanitizer) ou comportamento indefinido (UBSan);
- [ ] `Session::live_count()` volta a 0 ao fim de cada teste e acompanha `count()` do gerenciador a cada operação;
- [ ] construtores e `open()` que lançam exceção não deixam memória para trás nem alteram o gerenciador;
- [ ] reabrir a sessão de um usuário substitui a antiga sem vazá-la;
- [ ] `Session` pode ser movida e guardada em `std::vector`; se for copiável, a cópia é independente;
- [ ] `SessionManager` pode ser movido sem perder nem duplicar sessões.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Effective Modern C++*, Scott Meyers — Item 17 (geração de funções membro especiais), Item 18 (`std::unique_ptr` para posse exclusiva) e Item 21 (prefira `make_unique`… que só existe a partir de C++14).
- *C++ Memory Management*, Patrice Roy — cap. 4 "Using Destructors" (RAII e funções membro especiais) e cap. 5 "Using Standard Smart Pointers" (`unique_ptr` para objetos e arrays).
- cppreference: [RAII](https://en.cppreference.com/w/cpp/language/raii), [regra dos três/cinco/zero](https://en.cppreference.com/w/cpp/language/rule_of_three), [`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr).
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer) e [LeakSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer).

## Próximo desafio

Concluído este, dois caminhos se abrem:

- **desafio-2 — Lambdas sem fronteiras** (C++14): lambdas genéricas, init capture e, finalmente, `std::make_unique`;
- **desafio-3 — O valor que não estava lá** (C++17): `std::optional`, `std::string_view` e `std::variant`.
