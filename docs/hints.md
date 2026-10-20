# Dicas — desafio-5

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Siga uma linha com status `2OO` desde a leitura até o relatório. Em que ponto ela deixa
de ser um erro? Quantas formas diferentes de "falhar" o parser legado tem (códigos de
retorno, exceções, parâmetro de saída) — e qual delas o laço de `analyze` simplesmente ignora?
Faça a mesma pergunta para uma linha em branco salva com CRLF.

## Nível 2

- `src/log_analyzer.cpp`: `parse_record` mistura códigos mágicos, out-param e `std::stoi`
  (que aceita `"200x"` e `"-3"`, e lança para `"abc"`). O `catch (...)` de `analyze` é
  onde os erros desaparecem.
- Compare onde o contador `line_no` começa e onde ele é incrementado com o número que o
  editor mostra; o que acontece com ele nas linhas em branco?
- O relatório do ASan aponta duas linhas vizinhas de `analyze`: o que é guardado como chave
  do `unordered_map` e o que acontece com os elementos de um `std::vector` quando ele cresce?
- Faça a conta do *nearest-rank* à mão para 2 e 20 valores e compare com o índice usado.
- O contrato pede `noexcept` e `enum class`: o compilador mostra os `static_assert` que faltam.

## Nível 3

Abordagem sugerida (sem código pronto):

1. Escreva um validador por campo, cada um devolvendo `std::expected<T, ParseError>` com o
   número do campo; use `std::from_chars` (não lança, informa estouro e onde parou) e
   verifique que a conversão consumiu o campo inteiro.
2. Em `parse_line`: remova um `\r` final, separe os campos (por exemplo com
   `views::chunk_by` agrupando separadores e não separadores), trate 0, <5 e >5 campos e
   encadeie os validadores com `and_then`/`transform`. Declare-a `noexcept`.
3. Em `analyze`: divida o texto com `views::split('\n')`, materialize com `ranges::to`,
   descarte o pedaço vazio criado por um `\n` final e use `views::enumerate` para ter o
   índice (lembre: o relatório é 1-based). `or_else` é um bom lugar para separar "linha em
   branco" de "erro".
4. Para os paths, conte em um contêiner que seja dono das strings (`std::map<std::string, …>`
   já fica ordenado por path); ordene por acessos com um algoritmo **estável**.
5. Monte o relatório com `std::format_to` e no `main` devolva 0/1/2 conforme o resultado,
   escrevendo a mensagem de erro no stderr.

## Nível 4

Se só o critério de exceções falha: procure qualquer `std::stoi`/`std::stoul`, `throw` ou
`try` no caminho de linhas inválidas — mesmo exceções capturadas internamente contam.
Se só o ASan falha: rode `build/asan/tests analyze` e leia as três pilhas (uso, liberação,
alocação); a chave do mapa não pode apontar para memória que o vetor pode realocar.
