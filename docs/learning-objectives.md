# Objetivos de aprendizagem — desafio-3

Ao concluir este desafio você será capaz de:

1. Representar a ausência de um valor com `std::optional` em vez de valores sentinela
   (`-1`, `0`, `0.0`, string vazia), preservando valores legítimos que coincidem com o sentinela.
2. Raciocinar sobre o tempo de vida de dados referenciados por `std::string_view` e
   garantir que toda view devolvida aponte para memória viva — usando o AddressSanitizer
   como prova, não como palpite.
3. Converter texto em número de forma total e sem exceções com `std::from_chars`,
   rejeitando sobras, bases erradas e overflow.
4. Inferir a alternativa correta de um `std::variant` e consumi-lo com `std::visit`
   (e iterar pares com structured bindings).
5. Implementar fielmente uma especificação de formato textual com seus casos de borda
   (tabs, CRLF, comentários inline, seções repetidas, chaves duplicadas).

## Como os testes verificam cada objetivo

| Objetivo | Verificação (grupo em `tests/test_config.cpp`) |
|---|---|
| 1. `std::optional` no lugar de sentinelas | `api` (ausência → `nullopt`/`monostate`, `static_assert` nos tipos de retorno) e `presenca` (`-1`, `0`, `-0`, `0.0` e vazio são valores presentes; `value_or` só aplica o padrão quando a chave falta) |
| 2. Tempo de vida de `std::string_view` | `memoria`: views longas (no heap) e curtas (SSO) lidas depois de outras chamadas, `entries()` com structured bindings, views após mover o `Config`; tudo compilado com `-fsanitize=address,undefined` e `detect_stack_use_after_return=1` |
| 3. Conversão sem exceções | `conversao`: texto não numérico, overflow de `long` (±2^63), sobras (`10MB`, `12abc`, `1.5s`), `0x1F`, extremos `LONG_MIN`/`LONG_MAX`; qualquer exceção reprova o caso com o tipo e o `what()` |
| 4. `std::variant` / `std::visit` | `tipos` (`long`, `double`, `bool`, `std::string`, `monostate`, inteiro grande → `double`, `10.0.0.1` → texto) e o critério ponta a ponta do relay, que imprime cada valor via `std::visit` |
| 5. Especificação do formato | `formato` (tabs, CRLF, comentários de linha e inline, `#` dentro do valor, `=` no valor, seção reaberta, última duplicata vence) e `booleanos` (palavras aceitas, maiúsculas, lixo → `nullopt`) |
| Integração | o relay compilado com sanitizers lê `tests/fixtures/relay.ini` e sua saída é comparada com `tests/fixtures/relay.expected` |
