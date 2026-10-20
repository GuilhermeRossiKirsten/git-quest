# desafio-12 · Corrotinas que geram

> Trilha: cpp · Nível: advanced · ~90 min · 300 pontos · C++23 (`-std=c++23 -pedantic-errors`, g++ 14.2)

## Contexto

O `logscan` é a ferramenta que o time de plataforma usa para investigar
incidentes: lê exports de log de vários GiB direto do storage e lista
inventários de buckets com milhares de níveis de diretório. Quando o C++23
chegou, alguém reescreveu tudo com `std::generator` — "agora é preguiçoso,
como em Python!" — e a equipe comemorou.

A comemoração durou pouco. Pedir "os 5 primeiros erros" de um arquivo de
8 MiB lê o arquivo inteiro. As mensagens saem com lixo no lugar do horário.
Um storage que caiu no meio da leitura gerou um relatório "completo" com
metade dos dados. E listar um bucket fundo demora minutos. Sua missão é fazer
os generators do `logscan` honrarem o que prometem.

## Problema

Rode `./start.sh`: ele compila e executa `build/logscan`, que mostra os
sintomas abaixo.

- Para entregar 5 registros, o relatório lê **8,09 MiB de 8,09 MiB** da fonte.
- Os registros listados vêm com bytes estranhos no timestamp (e às vezes um
  erro some da lista). Sob AddressSanitizer o mesmo código para com
  `heap-use-after-free`.
- Com o storage falhando no meio do arquivo, o relatório diz
  "concluído com 500 registros (de 1000) e nenhum erro reportado".
- Linhas corrompidas somem do relatório sem que ninguém fique sabendo.
- Listar uma árvore de 300 níveis copia **90 601** diretórios. Com 1500 níveis,
  cada arquivo fica dezenas de vezes mais caro do que na raiz.

Depois de corrigir as primeiras coisas, fique de olho também na fonte: ela
precisa ser fechada **sempre** — no fim, num erro e quando o consumidor para
no meio (`break`, `views::take`).

### O contrato (`src/gq/logscan.hpp`)

| API | Comportamento esperado |
|---|---|
| `lines(src)` | abre a fonte só quando a iteração começa; lê em blocos de `kChunkSize`, **só o necessário** para a próxima linha; remove `\n` e `\r` final; última linha sem `\n` também sai; fecha a fonte exatamente uma vez em qualquer saída |
| `records(src)` | pula linhas vazias e `#comentários`; linha malformada lança `gq::parse_error` (com o número da linha) **no ponto de consumo**, depois dos registros anteriores; `gq::io_error` da fonte chega ao consumidor |
| `Record` | registros coletados (em um `std::vector`, por exemplo) continuam válidos depois que o generator acabou |
| `first_matching(src, level, n)` | os `n` primeiros registros com aquele nível, lendo da fonte só até o `n`-ésimo |
| `walk(root)` | caminhos `raiz/sub/arquivo` em pré-ordem, sem copiar nenhum `gq::Dir`, funcionando com milhares de níveis e com custo por arquivo que não cresce com a profundidade |

Os nomes, parâmetros e tipos de retorno (`std::generator<...>`) não podem
mudar: os testes e `src/main.cpp` os usam. O conteúdo de `Record` e toda a
implementação são seus.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Escrever corrotinas `std::generator` que produzem valores sob demanda.
- Liberar recursos com RAII no quadro da corrotina, inclusive quando ela é destruída suspensa.
- Propagar exceções do corpo do generator até quem consome.
- Controlar o tempo de vida do que é produzido (`std::string_view` × valores donos).
- Compor generators com `views::filter`, `views::take` e `ranges::to`.
- Escrever generators recursivos com `std::ranges::elements_of`.

## Conhecimentos necessários

- Concepts e ranges do C++20 (desafio-4).
- RAII, `std::string` × `std::string_view` e tempo de vida de objetos.
- Noção do que é uma corrotina: uma função que pode suspender (`co_yield`) e ser retomada depois.

## Ambiente

- Docker >= 20.10; o compilador roda no container `gcc:14.2.0` (padrão **C++23**,
  `-std=c++23 -pedantic-errors`). Os testes rodam com AddressSanitizer e
  UndefinedBehaviorSanitizer; `build/logscan` é compilado **sem** sanitizers
  para que os sintomas apareçam como na produção.
- `std::generator` exige libstdc++ 14 ou mais nova (`__cpp_lib_generator >= 202207L`); o primeiro teste confere isso.
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker:** com g++ >= 14 e `make`, use `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. No Windows (MinGW) os sanitizers ficam
  desligados; o resultado oficial é o do container.

## Como começar

```bash
./start.sh              # constrói a imagem, compila e mostra a saída de build/logscan
./test.sh               # 20 critérios com ✔/✘
```

Ferramentas de diagnóstico úteis:

- `./test.sh` — cada teste roda em processo próprio; o motivo da falha aparece abaixo do ✘ (inclusive o primeiro quadro do relatório do ASan);
- `GQ_VERBOSE=1 ./test.sh` e `build/logs/<teste>.log` — o log completo de cada teste;
- os contadores de `gq::MemorySource::stats()` (aberturas, fechamentos, bytes lidos) e `gq::Dir::copies()` — use-os no seu próprio código de experimento;
- [cppreference: `std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator).

## Arquivos que você pode modificar

- `src/gq/logscan.hpp` — o contrato; pode mudar `Record` e adicionar o que quiser, sem mudar as assinaturas existentes;
- `src/logscan.cpp` — a implementação.

`src/gq/source.hpp`, `src/gq/vfs.hpp` e `src/main.cpp` são fornecidos e não
podem ser alterados, assim como `tests/`, `test.sh`, `challenge.yaml` e o `Makefile`.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

A saída tem dois blocos: compilação (testes e relatório) e comportamento
(um processo por teste, com tempo limite de 30 s, sob ASan/UBSan).

## Critérios de conclusão

- [ ] `tests/runtime_tests.cpp` e `src/main.cpp` compilam com `-std=c++23 -pedantic-errors`, e `build/logscan` executa sem erros.
- [ ] `lines()`, `records()` e `first_matching()` leem da fonte no máximo o consumido mais dois blocos de `kChunkSize`.
- [ ] A fonte é aberta só quando a iteração começa e fechada exatamente uma vez: no fim, em erro e quando o generator é destruído no meio.
- [ ] `gq::io_error` e `gq::parse_error` chegam ao consumidor depois dos itens anteriores.
- [ ] Registros coletados permanecem válidos (nenhum erro do AddressSanitizer).
- [ ] `walk()` não copia diretórios, lista árvores com 4000 níveis e tem custo por arquivo independente da profundidade.
- [ ] `./test.sh` termina com código 0.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++23 STL Cookbook*, Bill Weinman — cap. 1, "Use synchronous generators" (`std::generator`), e cap. 4, "Create a generator with coroutines".
- *Asynchronous Programming with C++*, Javier Reguera-Salgado e Juan Antonio Rufes — cap. 8, "Asynchronous Programming Using Coroutines" (coroutine generators, coroutines e exceções).
- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — cap. 12, "Creating a coroutine generator type for sequences of values" e "Generating a sequence of values with the std::generator type".
- cppreference: [`std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator), [`std::ranges::elements_of`](https://en.cppreference.com/w/cpp/ranges/elements_of), [Coroutines](https://en.cppreference.com/w/cpp/language/coroutines).
- P2502R2, *std::generator: Synchronous Coroutine Generator for Ranges* (Casey Carter) — motivação de `elements_of` e do custo da recursão.

## Próximo desafio

**desafio-13 — jthread e stop tokens**: depois de parar um generator no meio
com segurança, o próximo passo é parar *threads* no meio: `std::jthread`,
`std::stop_token` e cancelamento cooperativo.
