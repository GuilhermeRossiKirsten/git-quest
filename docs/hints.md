# Dicas — desafio-27

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Compare a linha de compilação num caminho com e sem espaços (`cmake --build <dir> --verbose`).
Quais flags chegam ao compilador como um único argumento e quais chegam "picadas"? Quem as
monta: um target ou uma variável global?

Depois de um build, rode `git status` (ou compare a árvore de fonte antes e depois): quem
escreveu ali, e por quê? E num segundo build sem mudanças, `ninja -C <dir> -d explain`
diz exatamente qual passo ainda roda e por quê.

## Nível 2

Onde olhar, um sintoma por vez:

- `CMAKE_CXX_FLAGS` e `include_directories()` valem para tudo, não são exportados e não
  entendem caminhos com espaço. Cada target pode carregar os próprios requisitos:
  `target_include_directories`, `target_compile_features`, `target_compile_options`.
- O `config.h` é gerado no diretório de fonte por um alvo que roda **em todo build**.
  `configure_file()` existe exatamente para isso — e só reescreve o arquivo quando o conteúdo muda.
- A `textstats` usa a `utf8`, mas quem declara essa dependência hoje é o `wordfreq`.
- `add_library(textstats STATIC ...)` ignora `BUILD_SHARED_LIBS`. E uma estática que
  entra numa compartilhada precisa de código independente de posição.
- Nada é instalado: sem `install(TARGETS ...)`, `install(EXPORT ...)` e um `TextStatsConfig.cmake`,
  o `find_package` não tem o que encontrar.

## Nível 3

Abordagem (livro-base: *Modern CMake for C++*, caps. 5, 7, 8 e 14):

1. Apague a configuração global. Para cada target, pergunte: "isto é necessário para
   **compilar** o target (PRIVATE), para **usá-lo** (INTERFACE) ou para os dois (PUBLIC)?".
   O padrão C++20 é necessário para usar a API (std::span no header): é PUBLIC, e como
   *feature* mínima, não como flag `-std`. Warnings e `-Werror` são problema seu: PRIVATE
   (há também a propriedade `COMPILE_WARNING_AS_ERROR`).
2. Caminhos que aparecem em requisitos exportados não podem apontar para a fonte nem para o
   build: use `$<BUILD_INTERFACE:...>`/`$<INSTALL_INTERFACE:...>` ou, melhor, um
   `FILE_SET HEADERS` só com o header público. Headers internos ficam em `PRIVATE`.
3. Gere o `config.h` com `configure_file()` em `${CMAKE_CURRENT_BINARY_DIR}` (na configuração,
   não a cada build) e adicione esse diretório como include PRIVATE.
4. A `utf8` é uma biblioteca interna: ligue-a à `textstats` com `PRIVATE`, dê a ela
   `POSITION_INDEPENDENT_CODE`, e lembre que, com a `textstats` estática, quem a consome
   também precisa linkar a `utf8` — ela precisa entrar no mesmo export.
5. Empacote: `install(TARGETS ... EXPORT TextStatsTargets ...)`, `install(EXPORT ... NAMESPACE TextStats::)`,
   e um config + versão gerados com `CMakePackageConfigHelpers`
   (`configure_package_config_file`, `write_basic_package_version_file`), tudo em
   `lib/cmake/TextStats` com caminhos relativos ao prefixo.

## Nível 4

Checklist final, critério a critério:

- (1) nenhum `CMAKE_SOURCE_DIR` como destino de arquivo gerado; nenhuma flag montada em string;
- (2) o consumidor só vê o que está em `<prefixo>/lib/cmake/TextStats/` — teste você mesmo:
  instale em `/tmp/p`, mova para `/tmp/q` e compile `tests/consumer` com `-DCMAKE_PREFIX_PATH=/tmp/q`;
- (3) `find /tmp/q -name '*.h'` deve listar só `textstats/textstats.h`;
- (4) `target_compile_features(... PUBLIC cxx_std_20)`, nunca `set(CMAKE_CXX_STANDARD ...)` sozinho
  (não é exportado) nem `-std=c++20` em opção pública (rebaixa quem quer C++23);
- (5) nenhum `add_custom_target(... ALL ...)` que reescreve headers;
- (6) `add_library(textstats ...)` sem `STATIC`, e PIC na `utf8`;
- (7) `-Wall -Wextra` + erro em PRIVATE, em todos os targets do projeto (biblioteca, app e testes).
