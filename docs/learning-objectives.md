# Objetivos de aprendizagem — desafio-29

Ao concluir este desafio você será capaz de:

1. **Codificar informação física no tipo** com parâmetros de template não-tipo (`Dim<L, M, T>`) e aliases.
2. **Escrever metafunções** (*type functions*) que calculam a dimensão de um produto ou quociente em tempo de compilação.
3. **Fechar as portas das conversões implícitas**: construtores `explicit`, ausência de operadores de conversão e de construtores-template entre tipos que não deveriam se converter.
4. **Restringir operadores** para que só existam entre grandezas compatíveis (mesma dimensão para `+`, `-` e comparações).
5. **Garantir abstração de custo zero**: `sizeof` igual ao da representação, cópia trivial, tudo `constexpr`.
6. **Definir literais** (`operator""`) corretamente, com as duas sobrecargas numéricas e conversão de escala feita uma única vez.
7. **Testar o que não deve compilar**, distinguindo recusa pelo motivo certo de qualquer outro erro.

## Como os testes verificam cada objetivo

| Objetivo | Verificação em `./test.sh` |
|---|---|
| 1, 2 | `tests/contracts/01_*`, `02_*`, `09_*`: `static_assert(std::is_same_v<decltype(a * b), …>)` para produtos e quocientes; `tests/unit_tests.cpp` (`impressao_da_dimensao`): `operator<<` mostra os expoentes corretos |
| 3 | `tests/contracts/08_*` (`is_convertible`, `is_constructible`, `is_assignable`); `tests/rejects/03`–`05`, `09`, `10`, `12`, `13`, `14` |
| 4 | `tests/rejects/01`, `02`, `06`, `07`, `08`, `11`; `tests/contracts/03_*` (escala mantém a dimensão) |
| 5 | `tests/contracts/06_*` (`sizeof`/`alignof`), `07_*` (`is_trivially_copyable` e afins), `04_*` e `05_*` (literais e aritmética em `static_assert`) |
| 6 | `tests/unit_tests.cpp` (`literais_inteiros`, `quilometros`, `soma_km_com_m`, `libra_forca`) e `tests/contracts/04_*` |
| 6 (aplicado) | `tests/mission_tests.cpp`: o empuxo de cada queima em N, impulso total ≈ 40,034 N·s, delta-v e desvio ≈ 921 km, comparados com contas em `double` feitas no próprio teste |
| 7 | `tests/rejects/*.cpp`: cada arquivo precisa falhar, o erro precisa citar a linha marcada `← não deve compilar`, e a forma `-DGQ_VALID` precisa compilar |
