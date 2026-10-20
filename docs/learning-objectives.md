# Objetivos de aprendizagem — desafio-5

Ao concluir este desafio você será capaz de:

1. Modelar falhas esperadas como valores com `std::expected<T, E>` em vez de exceções,
   códigos mágicos ou parâmetros de saída.
2. Encadear etapas de validação com as operações monádicas `and_then`, `transform`,
   `or_else` e `transform_error`.
3. Percorrer texto com adaptadores de ranges (`views::split`, `views::enumerate`,
   `views::chunk_by`, `views::transform`) e materializar resultados com `std::ranges::to`.
4. Preservar o contexto de um erro: número da linha 1-based, campo culpado e motivo
   tipado (`enum class`), sem perder nenhuma linha inválida.
5. Calcular estatísticas com definições precisas (percentil *nearest-rank*, ordenação estável
   com desempate) e produzir saída com formato exato usando `std::format`/`std::print`.
6. Verificar o suporte do compilador a C++23 com *feature-test macros* e diagnosticar
   problemas de memória (referências penduradas) com ASan/UBSan.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Falhas como valores | `tests/contract_test.cpp` (`static_assert`: retorno `std::expected<Record, ParseError>`, `noexcept`); grupo `exceptions` conta toda exceção criada no processo (inclusive dentro da biblioteca padrão) ao processar linhas inválidas e exige zero |
| 2. Encadeamento | grupo `parse`: o erro reportado é o do primeiro campo inválido, com o número do campo; cada tipo de erro tem casos próprios (status `999`, `2OO`, `200x`; latência `-5ms`, `1.5ms`, estouro de `uint32_t` …) |
| 3. Ranges | grupo `analyze`: texto vazio, `\n` sozinho, com e sem `\n` final, CRLF idêntico a LF, campos separados por vários espaços/tabs, log com 1000 linhas |
| 4. Contexto do erro | `contract_test.cpp` (`std::is_scoped_enum_v<ErrorKind>`, `LineError::line`); grupo `analyze`: números de linha 1-based com linhas em branco no meio, todo tipo de erro aparece em `errors`, `lines = valid + inválidas + em branco` |
| 5. Estatísticas e formato | grupo `analyze` (p50/p95 para 1, 2, 4, 20 e 1000 valores; top 3 com empates e UTF-8); grupo `report` (formato exato, `-`, `erros: nenhum`); CLI comparada byte a byte com `tests/data/access.expected`, também com CRLF via stdin, e códigos de saída 0/1/2 |
| 6. Toolchain e sanitizers | `tests/toolchain_check.cpp` falha com `#error` explicativo se faltar algum recurso; testes e CLI recompilados com `-fsanitize=address,undefined` não podem acusar nenhum erro nem vazamento |
