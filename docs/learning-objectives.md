# Objetivos de aprendizagem — desafio-4

Ao concluir este desafio você será capaz de:

1. Definir concepts C++20 combinando concepts da biblioteca padrão (`std::integral`, `std::floating_point`, `std::ranges::input_range`, `std::equality_comparable`) e `requires`-expressions.
2. Restringir function templates, class templates e funções-membro com constraints e `requires`-clauses, para que usos inválidos sejam recusados na assinatura.
3. Separar o que o tipo garante (contrato em tempo de compilação) do que o valor garante (verificação em execução: range vazio, `lo > hi`).
4. Verificar contratos com `static_assert` e `requires`-expressions, inclusive os casos que **não** devem compilar.
5. Produzir diagnósticos de compilação que citam o contrato violado, em vez de erros vindos do corpo do template.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Definir concepts | `tests/contracts/01`–`06`, `13`, `14`, `18`–`20`: `static_assert` sobre `gq::Numeric`, `NumericRange`, `Hashable` e `Serializable` com tipos que devem ser aceitos e rejeitados (`bool`, tipos de caractere, `std::complex`, `std::vector<bool>`, tipo sem `std::hash`, tipo sem `==`, ponteiros) |
| 2. Restringir templates | `tests/contracts/07`–`12`, `15`–`17`, `19`–`22`: sondas (`tests/support/probes.hpp`) verificam se `mean`, `clamp_all`, `Histogram<Key>`, `serialize` e `Registry::snapshot` são aceitos ou rejeitados na assinatura; cada caso é compilado isoladamente e reportado com ✔/✘ |
| 3. Tipo vs. valor | `tests/runtime_tests.cpp` (um processo por teste, sob ASan/UBSan): média sem truncamento nem overflow, range vazio lança `std::invalid_argument`, `clamp_all` com `lo > hi` lança e não altera nada, serialização exata, `snapshot()` repetido não esvazia o registro, `Registry` continua aceitando `std::unique_ptr` |
| 4. Testar o que não compila | Os próprios casos de `tests/contracts/` são o modelo: cada um usa `static_assert(!probe::...)` dentro de um concept auxiliar, para que o resultado seja `false` e não um erro de compilação |
| 5. Diagnósticos claros | `tests/diagnostics/mean_de_texto.cpp` precisa falhar ao compilar, e a mensagem do g++ precisa citar `Numeric`/`NumericRange`; também `src/report.cpp` precisa compilar e executar depois que os usos inválidos apontados pelo compilador forem corrigidos |
