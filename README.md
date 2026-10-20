# desafio-6 · A tabela que esquecia

> Trilha: dsa (estruturas de dados) · Nível: intermediate · ~75 min · 200 pontos · C++17 · gcc 14.2.0

## Contexto

O serviço de sessões trocou `std::unordered_map` por `gq::FlatMap`, uma tabela
hash "caseira" com endereçamento aberto: todos os pares num único vetor
contíguo, sem um nó alocado por elemento. Nos testes de bancada ela voou.
Em produção, começaram os relatos estranhos: usuários deslogados do nada,
sessões encerradas que voltavam a valer, um contador de sessões ativas que só
crescia — e, no dia da migração em massa, um nó que simplesmente parou de
responder com a CPU a 100%.

Você herdou `src/flat_map.hpp`. Ninguém mais sabe por que ele foi escrito assim.

## Problema

Sintomas observados (todos reproduzidos pelos testes):

- Depois de apagar uma chave, **outras chaves** que nunca foram apagadas deixam de ser encontradas.
- `size()` diverge do número real de elementos depois de sobrescrever valores com `insert_or_assign`.
- A tabela fica mais cheia do que o fator de carga máximo prometido.
- Com chaves compostas `id_do_usuario << 32` (os 32 bits baixos são zero),
  1 milhão de inserções não termina em tempo razoável, enquanto chaves
  "comuns" do mesmo tamanho são rápidas.

Atenção: corrigir um sintoma pode **revelar** outros que estavam escondidos
(chaves apagadas que "ressuscitam", chaves duplicadas, buscas que nunca terminam,
memória que cresce sem limite sob ciclos de insere/apaga). Os testes cobrem todos eles.

### Contrato de `gq::FlatMap<K, V, Hash = std::hash<K>>`

| Operação | Comportamento |
|---|---|
| `bool insert_or_assign(const K&, V)` | insere ou sobrescreve; `true` se a chave era nova |
| `V* find(const K&)` (e versão `const`) | ponteiro para o valor, ou `nullptr` |
| `bool contains(const K&) const` | se a chave existe |
| `bool erase(const K&)` | remove; `true` se a chave existia |
| `size()`, `empty()`, `capacity()`, `clear()` | o óbvio; `capacity()` = número de slots |
| `for_each(f)` | chama `f(chave, valor)` para cada elemento uma única vez |

Restrições (verificadas pelos testes):

1. Endereçamento aberto com **sondagem linear**, num vetor de slots; `capacity()` é sempre **potência de 2** (≥ 8).
2. Depois de qualquer inserção, `size() / capacity() <= 0,75`.
3. `erase` nunca pode tornar outra chave inalcançável.
4. Operações em tempo O(1) amortizado — inclusive para chaves cujos bits baixos são todos iguais e sob ciclos longos de insere/apaga.
5. Com no máximo 17 elementos vivos, `capacity()` não passa de 128, não importa quantas chaves diferentes já passaram pela tabela.
6. Proibido delegar para contêineres associativos da STL (`std::unordered_map`, `std::map`, `set`...).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Explicar por que a remoção em endereçamento aberto exige lápides (*tombstones*) ou deslocamento para trás (*backward-shift deletion*).
- Manter corretamente `size`, lápides e fator de carga, e decidir quando e para qual capacidade fazer *rehash*.
- Reconhecer quando `hash & (capacidade - 1)` descarta a informação útil da chave e aplicar uma mistura de bits.
- Validar uma estrutura de dados com testes diferenciais contra uma implementação de referência, invariantes e sanitizers.
- Separar medição de desempenho (`-O2`, sem sanitizers) de verificação de correção (ASan/UBSan).

## Conhecimentos necessários

- C++17: templates, `std::vector`, `std::hash`, move semantics.
- Tabelas hash: colisões, sondagem linear, fator de carga.
- Operações com bits: máscara `& (n - 1)` para `n` potência de 2, deslocamentos.
- Ler relatórios do AddressSanitizer/UBSan e usar `gdb`/prints para inspecionar o vetor de slots.

## Ambiente

- Padrão: **C++17** (`-std=c++17`). Toolchain: **gcc 14.2.0**, na imagem Docker `gcc:14.2.0` (versão fixada no `Dockerfile`).
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash). Os testes rodam no container sem rede, com 2 CPUs, 2 GB e timeout.
- Dois builds (veja o `Makefile`):
  - `build/correctness`: testes de unidade, invariantes e diferencial com **AddressSanitizer + UBSan** (`-O1 -g`);
  - `build/perf`: teste de desempenho com **`-O2` e sem sanitizers**.
- Sem Docker: com g++ >= 14 (com suporte a ASan/UBSan, Linux/macOS), make e coreutils, use
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`. O tempo medido localmente pode diferir do container.

## Como começar

```bash
./start.sh        # constrói a imagem da toolchain e compila o código inicial
./test.sh         # veja o que falha
```

Ferramentas de diagnóstico úteis:

- Rode um teste isolado para ler a saída inteira:
  `docker run --rm -v "$PWD":/work -w /work gcc:14.2.0 build/correctness erase_keeps_rest_of_cluster_reachable`
  (`build/correctness --list` lista os nomes).
- Escreva um pequeno programa que imprime o estado de cada slot (vazio/ocupado/...) depois de cada operação — com `capacity()` = 8 dá para acompanhar à mão.
- Um hash constante (`struct H { size_t operator()(int) const { return 0; } };`) põe todas as chaves no mesmo cluster: ótimo para experimentar.
- Para o desempenho: quantos slots iniciais *distintos* as chaves do teste realmente usam?

## Arquivos que você pode modificar

- `src/` — em especial `src/flat_map.hpp`. Mantenha a API pública (os testes dependem dela); a estrutura interna é sua.

Não altere `tests/`, `test.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Cada teste roda num processo próprio com timeout de 20 s (um laço infinito reprova só aquele teste).
A saída mostra ✔/✘ por critério, com o motivo da falha (arquivo:linha do teste e valores obtido/esperado).

## Critérios de conclusão

- [ ] O código compila com `-std=c++17 -Wall -Wextra -pedantic` no gcc 14.2.0.
- [ ] Todos os testes de unidade passam sem erros do AddressSanitizer/UBSan.
- [ ] Invariantes: `capacity()` potência de 2, fator de carga ≤ 0,75 após cada inserção, `size()` igual ao número de elementos, capacidade limitada sob ciclos de insere/apaga.
- [ ] Teste diferencial: 100 mil operações aleatórias (semente fixa) com chaves inteiras, mais 100 mil com chaves espalhadas e 30 mil com strings, idênticas a `std::unordered_map`.
- [ ] Desempenho: 1 milhão de chaves `id << 32` (inserir, buscar todas, buscar 1 milhão ausentes, apagar metade) em até **2,5 s** no container. A solução de referência leva ~0,25 s; o limite é ~10x isso.
- [ ] `src/` não usa contêineres associativos da STL.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Data Structures the Fun Way*, Jeremy Kubica — Capítulo 10 (Hash Tables: colisões, sondagem linear, funções hash).
- *Data Structures and Algorithms with the C++ STL*, John Farrier — Capítulo 8 (Advanced Unordered Associative Container Usage: `load_factor`, `max_load_factor`, `rehash`).
- *Estrutura de Dados* (material FIAP) — Capítulo 5 (Espalhamento: tabelas de espalhamento, fator de carga e funções hash).
- *Introduction to Algorithms* (CLRS), Cormen, Leiserson, Rivest e Stein — Capítulo 11 (Hash Tables, seção de endereçamento aberto).
- cppreference: [`std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map), [`std::hash`](https://en.cppreference.com/w/cpp/utility/hash).
- Sebastiano Vigna, [splitmix64](https://prng.di.unimi.it/splitmix64.c) (finalizador usado como mistura de bits).

## Próximo desafio

Quer mudar de trilha? O **desafio-7 — "O pool que transbordou"** leva você para Go:
um worker pool com concorrência limitada, ordem preservada e sem data races.
