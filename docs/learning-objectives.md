# Objetivos de aprendizagem — desafio-1

Ao concluir este desafio você será capaz de:

1. **Diagnosticar** vazamentos, double free e uso após liberação lendo relatórios do
   AddressSanitizer e do LeakSanitizer até a linha de código responsável.
2. **Aplicar RAII**: garantir que todo recurso adquirido seja liberado em qualquer saída
   de escopo — retorno normal ou exceção, inclusive dentro de um construtor.
3. **Expressar posse exclusiva** com `std::unique_ptr` (objetos e arrays) em C++11, sabendo
   que `std::make_unique` só chega em C++14.
4. **Aplicar a regra dos três/cinco/zero**: reconhecer quando a cópia gerada pelo
   compilador é perigosa, proibi-la ou implementá-la, e garantir que o tipo seja movível.
5. **Trabalhar sob um padrão fixo** (C++11, `-pedantic-errors`, g++ 14) sem depender de
   recursos de padrões posteriores.

## Como os testes verificam cada objetivo

Todos os testes rodam com `-fsanitize=address,undefined` e `detect_leaks=1`, um processo
por teste; ao final de cada um, `Session::live_count()` precisa ser 0.

| Objetivo | Verificação |
|---|---|
| 1. Diagnosticar com sanitizers | Toda falha de memória é relatada com o tipo (leak, double-free, heap-use-after-free) e a primeira linha de `src/` da pilha; vazamentos reprovam mesmo testes cujas verificações funcionais passam. |
| 2. RAII e exceções | `session_usuario_invalido` e `session_capacidade_invalida` (construtor lança após alocar), `manager_lotado` e `manager_entrada_invalida` (`open()` lança): LSan não pode relatar vazamento e `live_count()`/`count()` não podem mudar. `manager_basico` e `manager_close` exigem liberação no destrutor, em `close()` e em `close_all()`; `manager_reabrir` exige que a sessão substituída seja destruída. |
| 3. `std::unique_ptr` sem `make_unique` | O projeto é compilado com `-std=c++11 -pedantic-errors`: usar `std::make_unique` é erro de compilação (o avaliador mostra uma dica). `manager_carga` executa 1000 operações determinísticas conferindo `live_count() == count()` a cada passo. |
| 4. Regra dos três/cinco/zero | `session_copia`: se `Session` é copiável, cópia e atribuição precisam ser profundas e a autoatribuição segura; se não é, o teste aceita. `session_move` e `session_em_vector`: mover e realocar não podem gerar double free nem uso após liberação. `manager_move`: mover o `SessionManager` não pode perder, duplicar nem vazar sessões. |
| 5. Padrão fixo | `toolchain_cpp11` confere `__cplusplus == 201103L` e g++ >= 14; o `Makefile` recusa g++ < 14. |
