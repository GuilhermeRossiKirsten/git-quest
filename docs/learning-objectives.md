# Objetivos de aprendizagem — desafio-2

Ao concluir este desafio você será capaz de:

1. Escrever lambdas genéricas (parâmetros `auto`) que funcionam com tipos diferentes sem duplicar functors.
2. Usar *init capture* para mover objetos move-only (`std::unique_ptr`) para dentro de closures e entender por que essas closures não cabem num `std::function`.
3. Evitar modos de captura padrão e referências pendentes em closures que sobrevivem ao escopo que as criou.
4. Dar a cada closure o seu próprio estado (init capture + `mutable`) em vez de estado compartilhado.
5. Encaminhar argumentos com `auto&&` e `std::forward<decltype(x)>(x)` sem cópias desnecessárias.
6. Usar dedução de tipo de retorno (`auto`) no lugar de `std::function` quando o tipo exato importa.
7. Definir *variable templates* com *type traits* em C++14 estrito, sem recursos de C++17.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Lambdas genéricas | `tests/test_by.cpp` ordena `Person`, `Product` e `Tracked` com o mesmo `gq::by`; `tests/test_compose.cpp` compõe funções de `int`, `double` e `std::string`; `tests/test_captures.cpp` usa `at_least` com `int`, `double` e `std::string`. |
| 2. Init capture com move-only | `tests/test_ownership.cpp` passa um `std::unique_ptr` a `bind_sink`, exige que o sink não seja destruído enquanto o callable (ou uma cópia movida dele) existir e que seja destruído exatamente uma vez; ASan detecta `heap-use-after-free`. `tests/test_compose.cpp` compõe uma lambda move-only. O critério do pipeline exige que `src/main.cpp` compile com o novo `bind_sink`. |
| 3. Sem referências pendentes | `tests/test_captures.cpp` e a execução de `src/main.cpp` rodam com AddressSanitizer (`detect_stack_use_after_return=1`): qualquer leitura de variável morta aborta o critério. |
| 4. Estado próprio por closure | `tests/test_counter.cpp` intercala três contadores e uma cópia e confere cada sequência. |
| 5. Encaminhamento sem cópias | `tests/test_compose.cpp` conta cópias de `Tracked` (0 para temporário, 1 para lvalue); `tests/test_by.cpp` exige 0 cópias durante `std::sort`. |
| 6. Dedução de tipo de retorno | `tests/test_apply_n.cpp` confere valor **e** tipo (`double`, `long long`) via `decltype`; `tests/test_compose.cpp` confere que `compose(plus_half, halve_int)(5)` é `double`. |
| 7. Variable templates em C++14 | `tests/test_traits.cpp` usa `gq::is_small_v<T>` como argumento de template (expressão constante) e confere 13 tipos; todos os testes compilam com `-std=c++14 -pedantic-errors`, que rejeita recursos de C++17. |
