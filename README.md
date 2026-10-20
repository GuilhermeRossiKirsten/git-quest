# desafio-4 · Contratos de template

> Trilha: cpp · Nível: intermediate · ~60 min · 200 pontos · C++20 (`-std=c++20 -pedantic-errors`, g++ 14.2)

## Contexto

O time de observabilidade mantém a `gq`, uma pequena biblioteca header-only de
métricas: média, limites, histograma, serialização e um registro de objetos.
É toda feita de templates "genéricos", e por isso todo mundo a usa para tudo.

Hoje de manhã o relatório diário do serviço `checkout` saiu assim:

```text
latency_mean_ms=2
availability_pct=0
endpoints_distinct=4
hits_login=1
config.region=sa-east-1
audit.=
config_has_region=0
```

Ninguém recebeu um único aviso do compilador. Alguém lembrou que, meses atrás,
a equipe tinha combinado **contratos** para esses templates — os nomes até
foram declarados em `src/gq/concepts.hpp` —, mas as definições nunca foram
escritas. Sua missão é fazer a `gq` recusar, já na compilação, o que não faz
sentido, e calcular corretamente o que faz.

## Problema

Sintomas observáveis (rode `./start.sh` e veja a saída de `build/report`):

- a latência média de `{2, 3, 2, 3}` ms aparece como `2`; médias de inteiros negativos ou muito grandes saem erradas, e a média de um container vazio derruba o processo;
- a "disponibilidade" calculada como média de um `std::vector<bool>` sai `0`;
- `/login` aparece em 3 requisições, mas o histograma conta 4 endpoints distintos e só 1 acesso a `/login`;
- números serializados perdem casas (`3.14159`), `bool` vira `1`, texto sai sem aspas, e um ponteiro seria serializado como endereço;
- o segundo `snapshot()` do registro de configuração vem vazio, e depois dele o registro "esquece" as chaves;
- `gq::mean(std::vector<std::string>{...})` e `gq::mean(std::string("abc"))` compilam (ou falham com erros enormes vindos de dentro da biblioteca).

### Contratos combinados pela equipe

Os nomes abaixo são públicos e **não podem mudar**. Os testes verificam
exatamente estas regras:

| Contrato | Regra |
|---|---|
| `gq::Numeric<T>` | tipo inteiro ou de ponto flutuante, **exceto** `bool` e os tipos de caractere (`char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`) |
| `gq::NumericRange<R>` | `R` é um *input range* cujos elementos são `Numeric` |
| `gq::Hashable<T>` | `std::hash<T>` pode ser usado com `T` (resultado conversível para `std::size_t`) **e** `T` tem `==` |
| `gq::Serializable<T>` | `T` não é ponteiro (nem `std::nullptr_t`) e é `bool`, `Numeric` ou conversível para `std::string_view` |

E o que cada parte da API deve garantir:

| API | Aceita | Comportamento |
|---|---|---|
| `gq::mean(r)` | somente `NumericRange` | retorna `double`, sem truncar nem estourar o tipo do elemento; range vazio lança `std::invalid_argument` |
| `gq::clamp_all(r, lo, hi)` | `NumericRange` **modificável** e limites `Numeric` (`clamp_all(vector<double>&, 0, 1)` continua valendo) | `lo > hi` lança `std::invalid_argument` sem alterar nada |
| `gq::Histogram<Key>` | `Key` `Hashable` e que **não** seja ponteiro (chaves são comparadas por valor) | igual ao atual |
| `gq::serialize(v)` | somente `Serializable` | `bool` → `true`/`false`; números → menor texto que preserva exatamente o valor (`0.1` → `0.1`, `3.141592653589793` → `3.141592653589793`); texto → entre aspas duplas, com `"` e `\` escapados com `\` |
| `gq::Registry<T>` | qualquer `T` móvel em `add`/`find`/`size` (inclusive `std::unique_ptr`) | `snapshot()` é `const`, devolve uma **cópia** e só existe quando `T` é copiável |

Usos inválidos devem ser rejeitados **na assinatura** (overload resolution /
instanciação do template), não por um erro dentro do corpo — por isso os testes
usam `requires`-expressions, que só enxergam a assinatura.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Definir concepts combinando concepts da biblioteca padrão e `requires`-expressions.
- Restringir function templates, class templates e funções-membro (`requires` em membros).
- Separar o que o tipo garante (compilação) do que o valor garante (execução).
- Testar contratos com `static_assert`, inclusive casos que **não** devem compilar.
- Obter diagnósticos de compilação que citam o contrato violado.

## Conhecimentos necessários

- Templates de função e de classe, dedução de argumentos (desafio-2).
- Noções de `std::vector`, `std::unordered_map`, iteradores e ranges.
- Leitura básica de mensagens de erro do g++.

## Ambiente

- Docker >= 20.10; o compilador roda no container `gcc:14.2.0` (padrão **C++20**, `-std=c++20 -pedantic-errors`, com AddressSanitizer e UndefinedBehaviorSanitizer nos testes de execução).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker:** com g++ >= 14 e `make` instalados, use `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`. No Windows (MinGW) os sanitizers são desligados; o resultado oficial é o do container.

## Como começar

```bash
./start.sh              # constrói a imagem, compila e mostra a saída do relatório
```

Ferramentas de diagnóstico úteis:

- `./test.sh` — cada contrato é compilado isoladamente e aparece como ✔/✘ com a mensagem do `static_assert` violado;
- `build/report` — o relatório com os sintomas;
- compile um caso só para ver o erro completo, por exemplo:
  `docker run --rm -v "$PWD":/w -w /w gcc:14.2.0 g++ -std=c++20 -fsyntax-only -Isrc -Itests tests/contracts/08_mean_rejeita_texto.cpp`
- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints) e a [biblioteca `<concepts>`](https://en.cppreference.com/w/cpp/concepts).

## Arquivos que você pode modificar

- `src/gq/*.hpp` — a biblioteca (comece por `concepts.hpp`);
- `src/report.cpp` — o relatório; quando os contratos estiverem de pé, o compilador vai apontar usos inválidos nele, e eles precisam ser corrigidos.

Não altere `tests/`, `test.sh`, `challenge.yaml` nem o `Makefile`: o avaliador usa as versões originais.
Os nomes `gq::Numeric`, `gq::NumericRange`, `gq::Hashable` e `gq::Serializable` fazem parte do contrato.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

A saída tem quatro blocos: compilação, comportamento em execução (um processo
por teste, para que um crash derrube só aquele caso), contratos em tempo de
compilação (um arquivo de `tests/contracts/` por contrato) e qualidade do
diagnóstico.

## Critérios de conclusão

- [ ] `tests/runtime_tests.cpp` e `src/report.cpp` compilam com `-std=c++20 -pedantic-errors`, e o relatório executa sem erros.
- [ ] Todos os testes de comportamento passam sob ASan/UBSan.
- [ ] Todos os 22 casos de `tests/contracts/` compilam (cada `static_assert` é satisfeito: usos válidos aceitos, usos inválidos rejeitados).
- [ ] Chamar `gq::mean` com `std::vector<std::string>` produz um erro que menciona o concept `Numeric`/`NumericRange`.
- [ ] `./test.sh` termina com código 0.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++20 STL Cookbook*, Bill Weinman — cap. 1, "Create safer templates with concepts and constraints".
- *Template Metaprogramming with C++*, Marius Bancila — cap. 6, "Concepts and Constraints" (definindo concepts, `requires`-expressions, compondo constraints, restringindo class templates e funções-membro).
- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — cap. 12, "C++20 Core Features": "Specifying requirements on template arguments with concepts" e "Using requires expressions and clauses".
- cppreference: [Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints), [`std::to_chars`](https://en.cppreference.com/w/cpp/utility/to_chars), [`std::ranges::range_value_t`](https://en.cppreference.com/w/cpp/ranges/iterator_t).

## Próximo desafio

**desafio-5 — Erros que não se escondem**: com os contratos de tipo resolvidos,
o próximo passo é tratar explicitamente os erros de *valor* em C++23 com
`std::expected` e adaptadores de ranges.
