# desafio-27 · Só compila na minha máquina

> Trilha: cpp · Build: **CMake moderno** · Toolchain: **gcc:14.2.0 + CMake 3.31.6 + Ninja 1.12.1** · Nível: intermediate · ~60 min · 200 pontos

## Contexto

A `textstats` é uma pequena biblioteca de estatísticas de texto (contagem de palavras
em UTF-8) com uma ferramenta de linha de comando, o `wordfreq`. Quem a escreveu
compilava assim, sempre na mesma pasta: `cd textstats && cmake . && make`. Funcionava.

Agora outras duas equipes querem usá-la. A primeira clonou o repositório em
`/home/ana/Meus Projetos/` e não conseguiu nem compilar. A segunda pediu "um pacote
instalável, como qualquer outra biblioteca": elas querem escrever só
`find_package(TextStats 1.4 CONFIG REQUIRED)` e `target_link_libraries(app PRIVATE TextStats::textstats)`.
E o pessoal de CI reclama que todo build recompila coisas, mesmo sem mudança nenhuma.

O código C++ está bom e tem testes. O problema está **todo no build**. Sua missão é
transformar os `CMakeLists.txt` em CMake moderno, sem mexer no código C++.

## Problema

O avaliador trata `textstats/` como faria outra máquina: copia o projeto para um
caminho **com espaços**, compila fora da árvore, instala num prefixo temporário, **move**
esse prefixo para outro lugar, esconde as pastas de fonte e de build e então compila um
projeto consumidor (`tests/consumer/`). Com o código inicial você vai observar:

- num caminho com espaços a compilação quebra com `fatal error: utf8.h: No such file or directory`
  — embora o arquivo esteja lá;
- depois de um build, aparecem arquivos novos **dentro da árvore de fonte**;
- `cmake --install` não instala nada que o `find_package` consiga achar;
- rodar `cmake --build` duas vezes seguidas, sem mudar nada, executa passos de novo, e
  tocar um único `.cpp` recompila mais de um arquivo;
- `-DBUILD_SHARED_LIBS=ON` é ignorado: continua saindo uma `libtextstats.a`.

Ao corrigir esses pontos, outros sintomas vão surgir — eles fazem parte do desafio:
referências indefinidas no link do consumidor, `relocation ... recompile with -fPIC`,
um consumidor que não compila `std::span` porque "não está em C++20", headers internos
visíveis para quem usa a biblioteca, `-Werror` estourando no código de outra equipe.

Ferramentas de diagnóstico úteis: `cmake --build <dir> --verbose` (mostra a linha de
comando de cada compilação), `ninja -C <dir> -d explain` (diz **por que** algo será
recompilado), `cmake --install <dir> --prefix /tmp/p` seguido de `find /tmp/p`, o
`compile_commands.json` (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) e `git status` (o que
o build escreveu na sua árvore?).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Trocar configuração global (`include_directories`, `CMAKE_CXX_FLAGS`) por requisitos de uso em targets, escolhendo entre `PUBLIC`, `PRIVATE` e `INTERFACE`.
- Fazer builds fora da árvore que funcionam em qualquer caminho e só escrevem no diretório de build.
- Declarar dependências transitivas e o padrão de C++ como requisitos (`target_link_libraries`, `target_compile_features`).
- Instalar e exportar a biblioteca como pacote CMake relocável (`install(TARGETS/EXPORT)`, arquivos de config e de versão).
- Manter headers internos e flags de warning privados ao projeto.
- Suportar `BUILD_SHARED_LIBS` e builds incrementais corretos.

## Conhecimentos necessários

- C++ básico e o ciclo compilar → linkar (bibliotecas estáticas e compartilhadas).
- Ter usado CMake ao menos uma vez (`cmake -S . -B build`, `cmake --build build`).
- Noções de linha de comando Linux (`find`, `touch`, variáveis de ambiente).

## Ambiente

- Toolchain: imagem Docker própria `FROM gcc:14.2.0` com **CMake 3.31.6** e **Ninja 1.12.1**
  baixados das releases oficiais por versão exata e conferidos por sha256 (veja o `Dockerfile`).
  A tag da imagem é derivada do conteúdo do `Dockerfile`.
- O projeto usa C++20 (a API pública tem `std::span`); o mínimo de CMake aceito é 3.25.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14, CMake >= 3.25, Ninja e make num Linux, usando
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`.

Todos os builds do avaliador rodam num container sem rede, com limites de CPU, memória e tempo,
sempre em cópias do projeto em `/tmp` — sua árvore nunca é usada como diretório de build.

## Como começar

```bash
./start.sh          # constrói a imagem (gcc + cmake + ninja) e compila o código inicial
./test.sh           # mostra os 7 critérios com ✔/✘ e o motivo de cada falha
```

Para experimentar à mão dentro do container (o diretório atual vira `/work`):

```bash
docker run --rm -it -v "$PWD":/work -w /work <imagem mostrada pelo start.sh> bash
cp -r textstats "/tmp/com espaco" && cmake -S "/tmp/com espaco" -B /tmp/b -G Ninja
cmake --build /tmp/b --verbose && cmake --install /tmp/b --prefix /tmp/p && find /tmp/p
```

## Arquivos que você pode modificar

- `textstats/CMakeLists.txt`, `textstats/src/CMakeLists.txt`, `textstats/app/CMakeLists.txt`,
  `textstats/test/CMakeLists.txt`;
- `textstats/src/config.h.in` (o modelo do header gerado);
- `textstats/cmake/` — pode editar, apagar ou criar arquivos aqui (ex.: um `TextStatsConfig.cmake.in`).

O contrato que o consumidor espera:

| Item | Valor |
|---|---|
| Pacote | `find_package(TextStats 1.4 CONFIG REQUIRED)` — versão 1.4.2, compatível com pedidos da mesma major |
| Alvo | `TextStats::textstats` |
| Header público | `#include <textstats/textstats.h>` (o único) |
| Headers internos | `tokenizer.h`, `utf8.h`, `config.h` — nunca visíveis para o consumidor |
| Opções | `BUILD_SHARED_LIBS` (padrão OFF) |
| Warnings | o projeto compila com `-Wall -Wextra` (ou mais) tratados como erro |

Não altere o código C++ (`.h`/`.cpp` em `textstats/`), `tests/`, `test.sh`, `challenge.yaml`,
`Makefile` nem `Dockerfile`. Os testes unitários de `textstats/test/` precisam continuar
registrados no `ctest`. Bônus (não avaliado): um `CMakePresets.json` com presets de
desenvolvimento e release — mas lembre que ele não pode apontar o build para dentro da fonte.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GQ_VERBOSE=1 ./test.sh        # imprime o log inteiro de cada etapa que falhar
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14, cmake >= 3.25, ninja)
```

Os logs de cada etapa ficam em `build/logs/` (ex.: `02-consumer-build.log`). Nas mensagens,
`<fonte>`, `<build>` e `<prefixo>` são as cópias temporárias criadas pelo avaliador.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] **1.** configurar, compilar, rodar o `ctest` e o `wordfreq` com sucesso fora da árvore, num caminho com espaços, sem criar nem alterar nenhum arquivo na árvore de fonte;
- [ ] **2.** `cmake --install` para um prefixo qualquer, que depois é **movido**, e um projeto consumidor que só faz `find_package(TextStats 1.4 CONFIG REQUIRED)` + `target_link_libraries(... TextStats::textstats)` compila, linka e roda — sem acesso à fonte nem ao build;
- [ ] **3.** o consumidor não enxerga headers internos (nem pelo include path exportado, nem por arquivos instalados);
- [ ] **4.** o consumidor recebe o requisito de C++20 sem configurar nada, e um consumidor que pede C++23 não é rebaixado;
- [ ] **5.** um segundo build sem mudanças é no-op, e tocar `src/tokenizer.cpp` recompila só esse arquivo;
- [ ] **6.** `BUILD_SHARED_LIBS=OFF` gera e instala uma biblioteca estática e `=ON` uma compartilhada; ambas compilam, passam no `ctest` e servem ao consumidor;
- [ ] **7.** uma variável não usada no código do projeto (biblioteca ou app) quebra o build, mas warnings no código do consumidor continuam sendo só warnings.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Modern CMake for C++* (2ª ed.), Rafał Świdziński — cap. 1 "First Steps with CMake" (builds fora da árvore, `-S`/`-B`) e cap. 4 "Setting Up Your First CMake Project" (seção "Disabling in-source builds").
- Idem — cap. 5 "Working with Targets", seção "What are Transitive Usage Requirements?" (`PUBLIC`/`PRIVATE`/`INTERFACE`).
- Idem — cap. 7 "Compiling C++ Sources with CMake": "Requiring specific features from the compiler" (`target_compile_features`), "Configuring the headers" (`configure_file` no diretório de build) e "Configuring errors and warnings".
- Idem — cap. 8 "Linking Executables and Libraries": "Building different library types" e "Position-independent code (PIC)".
- Idem — cap. 9 "Managing Dependencies in CMake": "Finding packages with CMake's find_package()".
- Idem — cap. 14 "Installing and Packaging": "Installing logical targets", "Dealing with public headers" (`FILE_SET HEADERS`), "Understanding the issues with relocatable targets", "Installing target export files", "Writing basic config files" e "Generating package version files".
- Idem — cap. 16 "Writing CMake Presets" (para o bônus).
- Documentação do CMake: [cmake-buildsystem(7)](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html), [cmake-packages(7)](https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html), [`install()`](https://cmake.org/cmake/help/latest/command/install.html), [CMakePackageConfigHelpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html).

## Próximo desafio

**desafio-28 — O switch que crescia**: C++ Software Design — trocar um `switch` por tipo
que cresce a cada feature por type erasure/strategy, sem slicing e sem quebrar o comportamento.
