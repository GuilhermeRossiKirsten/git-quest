# Objetivos de aprendizagem — desafio-14

Ao concluir este desafio você será capaz de:

1. **Distinguir cheia de vazia** num buffer circular sem desperdiçar um lugar, de modo
   que a capacidade real seja exatamente N (inclusive N = 1).
2. **Calcular posições com aritmética modular correta**: todo acesso parte do elemento
   mais antigo e dá a volta no array sem depender de N ser potência de 2 nem de subtração
   sem sinal.
3. **Evitar o heap**: guardar os N lugares dentro do próprio objeto, como se exige em
   firmware.
4. **Controlar o tempo de vida dos elementos**: um `T` só existe enquanto está na fila;
   tipos sem construtor padrão funcionam; nada é destruído duas vezes.
5. **Validar uma estrutura de dados** com testes de borda e com um teste diferencial
   contra um modelo da STL (`std::deque`).

## Como os testes verificam cada objetivo

Todos os testes compilam com `-std=c++17 -pedantic-errors` e `-fsanitize=address,undefined`;
cada arquivo `tests/test_*.cpp` é um binário e cada teste roda num processo separado.

| Objetivo | Verificação |
|---|---|
| 1. Cheia × vazia | `basico_vazia`, `basico_um_elemento`, `basico_cheia` (5 elementos cabem numa fila de 5; o 6º é recusado sem alterar nada), `basico_capacidade_1` e `diferencial_capacidade_1` (N = 1). Cada passo confere `size()`, `empty()` e `full()`. |
| 2. Wrap-around | `basico_wrap` (12 voltas com N = 5), `basico_back_apos_volta`, `basico_overwrite`, `basico_potencia_de_2` (N = 8), conferindo `front`, `back`, `operator[]` e a iteração a cada passo; `diferencial_capacidade_7`/`_16`: 100 000 operações com semente fixa comparadas com `std::deque`, com a operação exata da divergência na mensagem. |
| 3. Sem heap | `test_06_sem_alocacao.cpp` substitui o `operator new` global e exige zero alocações ao criar e usar `RingBuffer<std::uint64_t, 128>`, `RingBuffer<Tracked, 8>` e `TelemetryLog`; `alocacao_inline` exige `sizeof` >= N elementos; `alocacao_contador_funciona` prova que o contador funciona. |
| 4. Tempo de vida | `test_04_ciclo_de_vida.cpp` usa o tipo instrumentado `Tracked`: fila nova não constrói nada; objetos vivos == `size()` depois de cada `push`, `pop`, `push_overwrite`, `clear` e em 20 000 operações aleatórias; construções == destruições ao final; destruição dupla e cópia de memória não construída são detectadas; a cópia da fila é independente (ou proibida). `test_05_sem_construtor_padrao.cpp` só compila se a fila não exigir `T()`; `sem_padrao_string` deixa o ASan vigiar strings no heap. |
| 5. Validação | Os próprios testes diferenciais (`test_03`) e o consumidor real `TelemetryLog` (`test_07`: os 5 eventos mais recentes, contagens e `dropped()`); `toolchain_cpp17` confere C++17 e g++ >= 14. |
