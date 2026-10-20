# desafio-5 · Erros que não se escondem

> Trilha: cpp · Nível: advanced · ~75 min · 250 pontos · C++23 (`-std=c++23 -pedantic-errors`) · toolchain `gcc:14.2.0`

## Contexto

Madrugada de segunda. O painel do plantão diz que a API teve **zero** linhas inválidas
no log de acesso do fim de semana, p95 de latência de 4294967293 ms e que o path mais
acessado é `\0login`. Ninguém acredita em nenhum dos três números.

O `loganalyzer` é um utilitário herdado de 2019: lê um log de acesso, calcula estatísticas
e imprime um relatório que o painel interpreta linha a linha. "Funciona em produção há anos."
Agora o time de plataforma quer migrá-lo para C++23 e, antes disso, que ele pare de mentir.

## Problema

Cada linha do log tem cinco campos separados por espaços ou tabs:

```text
2026-10-19T12:00:01Z GET /api/items 200 12ms
<timestamp>          <método> <path> <status> <latência>
```

Sintomas observados por quem usa a ferramenta:

- Linhas claramente quebradas (status `2OO`, latência `-3ms`, campos a mais) **não aparecem**
  na lista de erros — algumas somem, outras entram nas estatísticas como se fossem válidas.
- Os números de linha dos erros não batem com o que o editor mostra, principalmente quando
  o log tem linhas em branco.
- Status `999` é aceito; latências negativas viram valores gigantes.
- O mesmo log salvo no Windows (CRLF) produz um relatório diferente do original.
- Paths com acentos (`/café/menu`) são rejeitados.
- O p50/p95 não coincide com o cálculo da planilha da equipe (método *nearest-rank*).
- O relatório não segue o formato que o painel espera, e o top 3 de paths muda de ordem
  em empates.
- Com AddressSanitizer, o analisador acusa leitura de memória já liberada.
- `loganalyzer arquivo-que-nao-existe.log` imprime um relatório vazio e termina com sucesso.

O time definiu o **contrato** que o código precisa cumprir (os testes o verificam):

| Item | Regra |
|---|---|
| `parse_line(std::string_view)` | retorna `std::expected<Record, ParseError>` e é `noexcept`; linha inválida nunca cria exceção (nem capturada internamente) |
| `ErrorKind` | `enum class`: `empty`, `missing_fields`, `extra_fields`, `bad_timestamp`, `bad_method`, `bad_path`, `bad_status`, `bad_latency` |
| `ParseError::field` | campo culpado, 1-based (1 = timestamp … 5 = latência); `0` para linha vazia; `missing_fields` aponta o primeiro campo ausente; `extra_fields` aponta o campo 6 |
| Timestamp | exatamente `AAAA-MM-DDTHH:MM:SSZ`; mês 01–12, dia 01–31, hora 00–23, minuto e segundo 00–59 |
| Método | `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD` (maiúsculas) |
| Path | começa com `/`; sem bytes de controle (< 0x20 ou 0x7F); UTF-8 é aceito como está |
| Status | exatamente 3 dígitos, 100–599 |
| Latência | inteiro sem sinal (cabe em `uint32_t`) seguido de `ms` |
| Ordem | o erro reportado é o do primeiro campo inválido da esquerda para a direita |
| CRLF | um `\r` no fim da linha é ignorado |
| `analyze(text)` | linhas numeradas a partir de 1; um `\n` final não abre linha nova; linhas em branco (só espaços/tabs) são contadas em `blank` e não são erro; `lines = valid + errors.size() + blank` |
| Percentis | *nearest-rank*: posição `ceil(p/100 · n)` (1-based) das latências válidas ordenadas; 0 se não houver linhas válidas |
| Top paths | até 3, mais acessos primeiro; empate em ordem crescente de path (bytes) |
| CLI | código de saída 0 (tudo válido), 1 (há linhas inválidas), 2 (não conseguiu ler a entrada; mensagem no stderr, nada no stdout) |

Formato exato do relatório (`format_report`), com `-` quando não há linhas válidas e
`erros: nenhum` quando não há erros:

```text
linhas: 8 | válidas: 5 | inválidas: 2 | em branco: 1
status: 200=3 204=1 404=1
latência: p50=30ms p95=250ms
top: /api/items (2), /api/items/7 (2), /health (1)
erros:
  linha 4: bad_status (campo 4)
  linha 7: bad_latency (campo 5)
```

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Modelar falhas esperadas como valores com `std::expected<T, E>`, sem exceções para controle de fluxo.
- Encadear validações com `and_then`, `transform`, `or_else` e `transform_error`.
- Percorrer texto com ranges (`views::split`, `views::enumerate`, `views::chunk_by`, `views::transform`) e materializar com `std::ranges::to`.
- Preservar o contexto do erro: linha 1-based, campo culpado e motivo tipado (`enum class`).
- Produzir saída com formato exato usando `std::format`/`std::print`.
- Verificar o suporte a C++23 com *feature-test macros* e validar o código com ASan/UBSan.

## Conhecimentos necessários

- `std::optional`, `std::string_view` e `std::variant` (desafio-3).
- Concepts e templates com restrições (desafio-4) ajudam a ler as assinaturas dos ranges.
- Noções de exceções em C++ e de `std::from_chars`.
- Ler um relatório do AddressSanitizer (pilha de "freed by" / "previously allocated by").

## Ambiente

- Padrão: **C++23**, compilado com `g++ -std=c++23 -pedantic-errors -Wall -Wextra`.
- Toolchain oficial: imagem Docker **`gcc:14.2.0`** (g++ 14.2 + libstdc++ 14), fixada no `Dockerfile`.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker:** com g++ >= 14 e `make` instalados, use `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh` (o ASan/UBSan precisa de `libasan`/`libubsan`, que acompanham o GCC na maioria das distros). O resultado oficial é o do container.

Recursos de C++23 verificados por `tests/toolchain_check.cpp` (o teste falha com uma
mensagem clara se faltar algum). Versões mínimas aproximadas, segundo a tabela de suporte
do cppreference — confira para o seu compilador:

| Recurso | Macro | GCC | Clang (libc++) | MSVC |
|---|---|---|---|---|
| `std::expected` + operações monádicas | `__cpp_lib_expected >= 202211L` | 13 | 17 | 19.36 |
| `std::print` / `std::println` | `__cpp_lib_print` | 14 | 18 | 19.37 |
| `std::ranges::to` | `__cpp_lib_ranges_to_container` | 14 | 17 | 19.34 |
| `views::enumerate` | `__cpp_lib_ranges_enumerate` | 13 | 19 | 19.37 |
| `views::chunk_by` | `__cpp_lib_ranges_chunk_by` | 13 | 18 | 19.33 |
| `std::is_scoped_enum` | `__cpp_lib_is_scoped_enum` | 11 | 12 | 19.30 |

Em resumo: **GCC >= 14** (o desafio usa `<print>` e `ranges::to`), Clang >= 19 com libc++
ou MSVC 19.37+ (VS 2022 17.7). Atenção: a libstdc++ 14 **não** formata ranges
(`std::format("{}", vector)`, P2286); `std::generator` existe no GCC 14, mas não é necessário aqui.

## Como começar

```bash
./start.sh                       # constrói a imagem, verifica o C++23 e compila o código inicial
./test.sh                        # mostra quais critérios falham e por quê
```

O log de exemplo está em `tests/data/access.log` e o relatório esperado em
`tests/data/access.expected`. Comece lendo `src/log_analyzer.hpp` e `src/log_analyzer.cpp`.

## Arquivos que você pode modificar

- `src/` — `log_analyzer.hpp` (API pública), `log_analyzer.cpp` (implementação) e `main.cpp` (CLI).
  Mantenha os nomes e membros declarados no header: os testes dependem deles. Você pode
  criar outros arquivos `.cpp`/`.hpp` em `src/`.

Não altere `tests/`, `test.sh`, `challenge.yaml` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                        # ou: ./scripts/gitquest test
```

Cada critério aparece com ✔/✘ e, quando falha, um trecho com "obtido" × "esperado".
Os logs completos ficam em `build/logs/` (por exemplo `build/logs/analyze.txt`,
`build/logs/asan-analyze.txt`). Ferramentas de diagnóstico úteis:

- os testes comportamentais (`build/tests parse|analyze|report|exceptions`);
- o binário instrumentado `build/asan/tests` — leia as linhas `freed by` / `previously allocated by`;
- o erro do `static_assert` em `build/logs/contract.txt`.

## Critérios de conclusão

- [ ] Toolchain com os recursos de C++23 exigidos e `src/` compilando com `-std=c++23 -pedantic-errors`.
- [ ] Contrato de tipos: `parse_line` retorna `std::expected<Record, ParseError>` e é `noexcept`; `ErrorKind` é `enum class`.
- [ ] `parse_line` classifica cada linha (válida ou erro com o campo culpado), inclusive CRLF, campos extras e UTF-8.
- [ ] `analyze` conta linhas corretamente, numera erros a partir de 1, não perde nenhum erro e calcula p50/p95 e top 3.
- [ ] `format_report` produz o formato exato.
- [ ] Nenhuma exceção é criada ao processar linhas inválidas.
- [ ] `loganalyzer` gera o relatório esperado (também com CRLF via stdin) e usa os códigos de saída 0/1/2.
- [ ] ASan/UBSan não acusam nada.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++23 STL Cookbook*, Bill Weinman — cap. 1 ("Format text with the new print() and println() functions", "Initialize containers from ranges") e cap. 8 (classes utilitárias: `std::optional`, `std::variant`).
- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — cap. 6 ("Using std::expected to return a value or an error", "Chaining together computations that may or may not produce a value"), cap. 2 ("Formatting and printing text with std::format and std::print") e cap. 12 ("Exploring the standard range adaptors", "Converting a range to a container").
- cppreference: [`std::expected`](https://en.cppreference.com/w/cpp/utility/expected), [Ranges library](https://en.cppreference.com/w/cpp/ranges), [`std::from_chars`](https://en.cppreference.com/w/cpp/utility/from_chars), [suporte dos compiladores a C++23](https://en.cppreference.com/w/cpp/compiler_support/23), [feature-test macros](https://en.cppreference.com/w/cpp/feature_test).
- P0323 — *std::expected* (Vicente Botet, JF Bastien); P2505 — *Monadic Functions for std::expected*.
- Percentil *nearest-rank*: <https://en.wikipedia.org/wiki/Percentile#The_nearest-rank_method>.

## Próximo desafio

Este é o último desafio da trilha C++ por enquanto. Que tal mudar de ares com o
**desafio-6 · A tabela que esquecia** (trilha Estruturas de Dados): uma hash table com
endereçamento aberto, tombstones e rehash.
