# desafio-3 · O valor que não estava lá

> Trilha: cpp · Nível: intermediate · ~50 min · 150 pontos · C++17 (gcc 14.2.0)

## Contexto

O **relay** é um serviço antigo que encaminha mensagens entre sistemas internos. No ano
passado alguém modernizou a interface do leitor de configuração: `src/config.hpp` agora
fala C++17 — `std::optional`, `std::string_view`, `std::variant`. Bonito no header.

Por baixo, porém, `src/config.cpp` ainda embrulha as rotinas do parser em C de 2009.
Na última mudança de produção o time de operações configurou `retries = -1` ("tentar
para sempre"), desligou o banner e as métricas, e trocou a senha do banco. O relay
ignorou metade disso, inventou a outra metade e, em alguns ambientes, simplesmente
caiu. Você foi chamado para fazer a implementação cumprir o que a interface promete.

## Problema

Sintomas observados no relay (rode `./start.sh` e compare a saída com `examples/relay.ini`) e nos testes (`./test.sh`):

- Com os sanitizers ligados, o relay aborta logo na primeira leitura com um relatório
  do **AddressSanitizer** apontando para `src/config.cpp`.
- `retries = -1` é tratado como se a chave não existisse (o serviço usa o padrão 3);
  o mesmo acontece com `sample_rate = 0.0` e com o banner vazio.
- `debug = TRUE` vira `false`; `debug = banana` também vira `false`, e uma chave
  `debug` que nem existe... também.
- A senha `s3nh4#forte!2024` chega truncada; um comentário `; ...` no fim da linha
  vira parte do valor.
- Ler `workers = auto` com `get_int` derruba o processo com `terminate called after throwing an
  instance of 'std::invalid_argument'`, enquanto `10MB` vira silenciosamente `10`.
- `statement_timeout = 2.5` é inferido como inteiro `2`; `10.0.0.1` vira o número `10`.
- Arquivos editados no Windows (CRLF) ou indentados com tab não são lidos direito.
- Quando uma chave aparece duas vezes, vale a primeira — a especificação diz o contrário.

Ferramentas de diagnóstico: os relatórios do **AddressSanitizer/UBSan** (já ligados em
todos os builds), as mensagens de `./test.sh` (cada caso mostra valor obtido × esperado)
e, se quiser, `gdb` dentro do container.

### Especificação do formato

O comportamento esperado de `cfg::Config` (os testes seguem exatamente isto):

| Tema | Regra |
|---|---|
| Linhas | separadas por `\n`; um `\r` final (CRLF) é ignorado |
| Espaços | espaço, tab e `\r` nas pontas de chaves, valores e nomes de seção são removidos |
| Seções | `[nome]` (espaços internos nas pontas ignorados); chaves viram `nome.chave`; chaves antes da primeira seção são globais (`chave`); uma seção pode reaparecer |
| Pares | `chave = valor`, separados no **primeiro** `=`; linhas sem `=` ou com chave vazia são ignoradas |
| Comentários | linha cujo primeiro caractere não branco é `#` ou `;`; no valor, `#` ou `;` **precedido de espaço/tab** inicia um comentário (`s3nh4#x` é um valor, `80 # http` é `80`) |
| Valor vazio | `chave =` existe e vale `""` |
| Duplicatas | a **última** ocorrência vence |
| Ausência | `get*` devolvem `std::nullopt`; `typed` devolve `std::monostate` |
| `get_int` | o valor inteiro deve ser um número decimal (sinal `-` opcional) que caiba em `long`; caso contrário `std::nullopt` |
| `get_double` | o valor inteiro deve ser um número de ponto flutuante (`2.5`, `-1e3`, `7`); caso contrário `std::nullopt` |
| `get_bool` | `true/yes/on/1` → `true`, `false/no/off/0` → `false`, sem diferenciar maiúsculas; outro texto → `std::nullopt` |
| `typed` | palavra booleana (exceto `1`/`0`) → `bool`; inteiro que cabe em `long` → `long`; outro número completo → `double`; qualquer outro texto (inclusive vazio) → `std::string` |
| `entries(secao)` | pares (chave sem prefixo, valor) em ordem alfabética, incluindo valores vazios; `entries("")` lista as chaves globais |
| Exceções | nenhuma função de leitura lança exceção por causa do conteúdo do arquivo |
| Tempo de vida | os `std::string_view` devolvidos por `get`/`entries` são válidos enquanto o `Config` existir |

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Representar ausência com `std::optional` em vez de valores sentinela.
- Garantir que todo `std::string_view` devolvido aponte para memória viva — e provar com o ASan.
- Converter texto em número sem exceções e sem aceitar sobras (`std::from_chars`).
- Escolher a alternativa correta de um `std::variant` e consumi-lo com `std::visit`.
- Implementar uma especificação de formato com seus casos de borda.

## Conhecimentos necessários

- C++ moderno básico (desafio-1): RAII, `std::string`, `std::map`, referências.
- Noções de `std::optional`, `std::string_view` e `std::variant` (as referências abaixo cobrem o necessário).
- Ler um relatório do AddressSanitizer (pilha do acesso inválido e da liberação).

## Ambiente

- **Padrão:** C++17 exato — `-std=c++17 -pedantic-errors -Wall -Wextra` (recursos de C++20 não compilam).
- **Toolchain:** `gcc:14.2.0` em container Docker, com `-fsanitize=address,undefined`.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker:** com `g++ >= 14` e `make` locais (Linux/WSL/macOS com suporte a ASan),
  use `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`. O MinGW no Windows não
  tem AddressSanitizer; nesse caso use o Docker.

## Como começar

```bash
./start.sh          # constrói a imagem, compila e roda o relay com examples/relay.ini
```

Para rodar o serviço de novo depois de mudar o código, rode `./start.sh` outra vez
(é idempotente). O código do serviço está em `src/main.cpp`: ele é só um cliente da
API e já está correto.

## Arquivos que você pode modificar

- `src/config.cpp` — a implementação (é aqui que mora quase todo o trabalho).
- `src/config.hpp` — apenas a parte privada, se precisar; as **assinaturas públicas**
  são verificadas pelos testes com `static_assert`.

Não altere `src/main.cpp`, `examples/`, `tests/`, `test.sh`, `challenge.yaml` nem o
`Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh   # sem Docker, com g++ >= 14 local
```

Cada critério aparece com ✔/✘; casos reprovados mostram o valor obtido e o esperado,
e falhas de memória mostram o trecho relevante do relatório do sanitizer (o relatório
completo fica em `build/group-<grupo>.log`).

## Critérios de conclusão

- [ ] O código compila com `-std=c++17 -pedantic-errors` e a API pública mantém `std::optional`, `std::string_view` e `std::variant`.
- [ ] Chave ausente → `std::nullopt` / `std::monostate`.
- [ ] Valores legítimos `-1`, `0`, `0.0` e vazio não são confundidos com ausência.
- [ ] Conversões rejeitam texto inválido, sobras e overflow de `long` — sem lançar exceções.
- [ ] `get_bool` só aceita as palavras da especificação, sem diferenciar maiúsculas.
- [ ] `typed()` escolhe `bool`, `long`, `double` ou `std::string` conforme a especificação.
- [ ] Tabs, CRLF, comentários, seções e chaves duplicadas seguem a especificação.
- [ ] Nenhum erro do AddressSanitizer/UBSan: as views devolvidas apontam para dados vivos.
- [ ] O relay produz a saída esperada para `tests/fixtures/relay.ini`.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — cap. 6 (General-Purpose Utilities): "Using std::optional to store optional values", "Using std::variant as a type-safe union", "Visiting a std::variant"; cap. 2: "Using std::string_view instead of constant string references", "Converting between numeric and string types"; cap. 1: "Using structured bindings to handle multi-return values".
- *C++20 STL Cookbook*, Bill Weinman — cap. 8 (Utility Classes): "Manage optional values with std::optional", "Store different types with std::variant"; cap. 7: "Use string_view as a lightweight string object"; cap. 2: structured bindings.
- *C++ Software Design*, Klaus Iglberger — Guideline 17: "Consider std::variant for Implementing Visitor" (como o `std::visit` de `src/main.cpp`).
- cppreference: [`std::optional`](https://en.cppreference.com/w/cpp/utility/optional), [`std::basic_string_view`](https://en.cppreference.com/w/cpp/string/basic_string_view) (seção sobre tempo de vida), [`std::from_chars`](https://en.cppreference.com/w/cpp/utility/from_chars), [`std::variant`](https://en.cppreference.com/w/cpp/utility/variant), [`std::stol`](https://en.cppreference.com/w/cpp/string/basic_string/stol) (exceções).
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer): heap-use-after-free e stack-use-after-return.

## Próximo desafio

**desafio-5** — C++23: `std::expected` e adaptadores de ranges num analisador de logs que
trata erros explicitamente. Lá, além de "não há valor", você vai precisar dizer *por quê*.
