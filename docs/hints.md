# Dicas — desafio-1

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Para cada `new` do projeto, quem é o responsável pelo `delete` correspondente — e esse
`delete` acontece em **todos** os caminhos, inclusive quando uma exceção é lançada no meio
de uma função ou de um construtor? E quando uma `Session` é copiada, quantos objetos
passam a achar que são donos do mesmo buffer?

Rode `GQ_VERBOSE=1 ./test.sh` e leia um relatório do LeakSanitizer de cima para baixo:
a primeira linha em `src/` diz onde nasceu a memória perdida.

## Nível 2

Há quatro lugares a investigar:

- `Session::Session` em `src/session.cpp`: o que já foi alocado quando a validação lança?
  Lembre-se: se o construtor não termina, o destrutor do objeto **não** roda — só os
  destrutores dos membros já construídos.
- `SessionManager` em `src/session_manager.cpp`: `close`, `close_all`, o destrutor, o
  `throw` de "limite atingido" e a substituição na reconexão.
- As funções membro especiais de `Session`: o que a cópia gerada pelo compilador faz com
  um `char*`? E por que mover uma `Session` acaba copiando? (*Effective Modern C++*, Item 17).
- As funções membro especiais de `SessionManager`: o que acontece ao mover um objeto que
  guarda `std::vector<Session*>`?

## Nível 3

Troque a posse manual por objetos que liberam o recurso no próprio destrutor (RAII):

- o buffer de `Session` pode ser um `std::unique_ptr<char[]>` (ou um `std::vector<char>`);
- `SessionManager` pode guardar `std::vector<std::unique_ptr<Session>>`; `erase`, `clear`
  e a atribuição de um `unique_ptr` destroem a sessão antiga sozinhos;
- crie a sessão já dentro de um `unique_ptr` antes de qualquer coisa que possa lançar.
  Em C++11 não existe `std::make_unique`: use `std::unique_ptr<T>(new T(...))`;
- depois de trocar os membros, apague os destrutores manuais que não fazem mais nada.
  Um destrutor declarado pelo usuário, mesmo vazio, impede o compilador de gerar o
  construtor de movimento — e um `unique_ptr` não pode ser copiado. Se o compilador
  reclamar de cópia de `unique_ptr`, a culpa é de uma função especial que você declarou;
- decida a cópia de `Session`: proibida (`= delete`, que `unique_ptr` já implica) ou
  profunda (que `std::vector<char>` já oferece). Regra do zero quando possível; regra
  dos cinco quando você precisa escrever alguma delas.

## Nível 4

Checklist final, se ainda sobrar algum ✘:

- `session_em_vector` / `session_move`: a `Session` tem construtor e atribuição por
  movimento (escritos por você ou gerados pelo compilador)? Um movimento escrito à mão
  deve deixar a origem num estado destrutível (`capacity`/`size` zerados).
- `manager_reabrir` / `manager_entrada_invalida`: a sessão antiga é destruída **depois** de a nova ter sido criada
  com sucesso? Se o `open()` da reabertura lançar, a antiga precisa continuar lá.
- `manager_move`: o `SessionManager` ainda declara um destrutor?
- Erro "`make_unique` is not a member of `std`": o padrão é C++11; o `Makefile` não pode mudar.
