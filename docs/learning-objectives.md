# Objetivos de aprendizagem — desafio-27

Ao concluir este desafio você será capaz de:

1. **Pensar em targets**: substituir `include_directories()`, `link_libraries()` e
   `CMAKE_CXX_FLAGS` globais por requisitos de uso declarados em cada target, escolhendo
   `PRIVATE`, `PUBLIC` ou `INTERFACE` conforme quem precisa do requisito.
2. **Fazer builds fora da árvore e independentes de caminho**: nada gerado na fonte,
   caminhos com espaços tratados pelo CMake, arquivos gerados com `configure_file()` no
   diretório de build.
3. **Declarar dependências transitivas e o padrão de C++** como requisitos propagados
   (`target_link_libraries`, `target_compile_features`), em vez de depender de quem linka.
4. **Instalar e exportar** um pacote CMake relocável: `install(TARGETS/EXPORT)`,
   `FILE_SET HEADERS`, `configure_package_config_file()` e `write_basic_package_version_file()`,
   consumível com `find_package(TextStats 1.4 CONFIG)` e o alvo `TextStats::textstats`.
5. **Encapsular**: headers internos e flags de warning/erro ficam privados ao projeto.
6. **Suportar estática e compartilhada** (`BUILD_SHARED_LIBS`, `POSITION_INDEPENDENT_CODE`) e
   manter o **build incremental** correto.

## Como os testes verificam cada objetivo

O avaliador (`tests/run.sh`) nunca lê os seus `CMakeLists.txt`: ele executa o build e observa
o resultado, sempre numa cópia do projeto num caminho com espaços.

| Objetivo | Verificação |
|---|---|
| 1. Targets e requisitos de uso | Critério 1: configurar e compilar num caminho com espaços (flags montadas em string quebram). Critério 7: uma variável não usada inserida pelo avaliador em `src/tokenizer.cpp` e em `app/main.cpp` precisa quebrar o build, e um `noisy.cpp` do consumidor (variável não usada, comparação com sinal, função `[[deprecated]]`) precisa compilar. |
| 2. Fora da árvore | Critério 1: a árvore de fonte é fotografada (lista de diretórios + sha256 de cada arquivo) antes e depois do configure/build; qualquer arquivo criado ou alterado é listado. O `ctest` e o `wordfreq` (comparado com `tests/data/sample.expected`) rodam a partir do build. |
| 3. Dependências e padrão | Critério 2: o consumidor estático só linka se a dependência `utf8` vier pelo pacote. Critério 4: o consumidor sem `CMAKE_CXX_STANDARD` precisa compilar `<textstats/textstats.h>` (usa `std::span`), e um alvo com `CXX_STANDARD 23` faz `static_assert(__cplusplus >= 202302L)` — uma flag `-std=c++20` forçada pela biblioteca o reprova. |
| 4. Pacote relocável | Critério 2: `cmake --install` num prefixo que depois é movido; fonte e build são escondidos; o consumidor de `tests/consumer` faz `find_package(TextStats 1.4 CONFIG REQUIRED)` (exige arquivo de versão), linka `TextStats::textstats` e roda verificações da API (`GQ_CONSUMER_OK`). Registros de pacote do usuário/sistema são desligados. |
| 5. Encapsulamento | Critério 3: uma sonda do consumidor usa `__has_include` para `tokenizer.h`, `utf8.h` e `config.h` (em várias grafias) e falha com `#error GQ_LEAK ...` se algum estiver alcançável; o prefixo instalado também não pode conter esses arquivos nem `.cpp`. |
| 6. Estática/compartilhada e incremental | Critério 6: com `BUILD_SHARED_LIBS=OFF` o alvo importado é `STATIC_LIBRARY` e há `libtextstats.a`; com `ON` (build, `ctest`, install, consumidor) é `SHARED_LIBRARY` e há `libtextstats.so*`. Critério 5: um segundo `cmake --build` precisa dizer `ninja: no work to do.` e, depois de tocar `src/tokenizer.cpp`, só um objeto pode ser recompilado. |
