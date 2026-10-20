# desafio-14 · Fila circular sem alocação

> Trilha: dsa · Padrão: **C++17** · Toolchain: **gcc:14.2.0** · Nível: beginner · ~40 min · 100 pontos

## Contexto

O sensor de vibração da fábrica guarda os **últimos 5 eventos** de telemetria num pequeno
log (`gq::TelemetryLog`). Quando algo dá errado, o técnico conecta o notebook e lê esses
eventos para entender o que aconteceu logo antes da falha. O log se apoia numa fila
circular genérica, `gq::RingBuffer<T, N>`, escrita às pressas por alguém que já saiu da
empresa.

Dois problemas chegaram ao mesmo tempo. O técnico reclama que o log "esquece" eventos:
depois de um incidente com 12 eventos, só apareceram 2. E a equipe de firmware quer usar a
mesma fila no microcontrolador, onde **não existe heap** — mas o código atual quebra a
regra na primeira linha. Sua missão é consertar `src/ring_buffer.h` sem mudar a API pública,
que já é usada pelo log.

## Problema

Rode `./start.sh`: ele compila e executa uma demonstração que registra 12 eventos num log
de 5. A saída esperada é:

```text
eventos registrados: 12, guardados: 5, descartados: 7
#8 aviso 80 | #9 erro 90 | #10 info 100 | #11 info 110 | #12 aviso 120
mais antigo: #8 · mais recente: #12 · erros guardados: 1
```

A saída atual guarda bem menos. Rodando `./test.sh`, você também vai observar:

- uma fila de capacidade 5 recusa o 5º elemento, e uma fila de capacidade 1 não aceita nenhum;
- depois de `push_overwrite()` com a fila "cheia", ela passa a dizer que tem poucos
  elementos (ou nenhum);
- `rb[0]` nem sempre é o elemento mais antigo, e a iteração (range-for) mostra elementos
  na ordem errada depois que a fila dá a volta;
- `back()` devolve um valor antigo justamente depois que a escrita volta ao início do
  array — e só com algumas capacidades (com 8 parece funcionar; com 5 ou 7, não);
- o teste diferencial, que repete 100 000 operações aleatórias numa `std::deque`, diverge
  logo nas primeiras operações;
- com um tipo instrumentado que conta objetos vivos, a fila nova já "contém" N objetos,
  e `pop()`/`clear()` não destroem nada;
- o arquivo de teste com um tipo **sem construtor padrão** nem compila;
- o contador de `operator new` do teste registra alocações no heap.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Distinguir fila cheia de fila vazia num buffer circular sem desperdiçar um lugar.
- Calcular posições com aritmética modular correta (wrap-around), sempre a partir do elemento mais antigo.
- Guardar os N elementos dentro do próprio objeto, sem alocação dinâmica.
- Controlar o tempo de vida dos elementos: construir só ao inserir, destruir ao remover.
- Validar uma estrutura de dados com testes de borda e um teste diferencial contra `std::deque`.

## Conhecimentos necessários

- Classes e templates simples em C++ (`template <typename T, std::size_t N>`).
- Arrays, o operador `%` e aritmética com `std::size_t` (sem sinal).
- Noções de construtor e destrutor; `std::optional` ajuda (o C++17 o traz na biblioteca padrão).

## Ambiente

- Padrão exigido: **C++17** (`-std=c++17 -pedantic-errors -Wall -Wextra`).
- Toolchain: imagem Docker `gcc:14.2.0` (g++ 14.2, make, AddressSanitizer e UBSan); nada além disso.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: g++ >= 14 e make num Linux, usando `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`.

Tudo compila com `-fsanitize=address,undefined`. Cada arquivo `tests/test_*.cpp` vira um
binário próprio (um arquivo que não compila não derruba os outros) e cada teste roda num
processo separado, dentro de um container sem rede e com limites de CPU, memória e tempo.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain, compila e roda a demonstração
./test.sh           # mostra quais critérios falham e por quê
```

Ferramentas para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                     # log completo de cada teste que falhar
cat build/logs/diferencial_capacidade_7.log
cat build/logs/test_05_sem_construtor_padrao.compile.log
```

- O **teste diferencial** mostra a operação exata em que a fila divergiu da `std::deque`,
  as últimas operações e o conteúdo dos dois lados: reproduza no papel, desenhando o array,
  `head_` e `tail_`.
- Nos testes de **ciclo de vida**, o tipo `Tracked` (`tests/support/tracked.h`) conta
  construções, destruições e objetos vivos: compare "objetos vivos" com `size()`.
- O teste de **alocação** (`tests/test_06_sem_alocacao.cpp`) substitui o `operator new`
  global e conta quantas vezes o heap é usado.
- Erros de compilação de template: siga as linhas `required from` até `src/ring_buffer.h`.

## Arquivos que você pode modificar

- `src/ring_buffer.h` — a classe `gq::RingBuffer<T, N>`.

Mantenha a **API pública** descrita no comentário do topo do arquivo (nomes, parâmetros,
retornos); membros privados, includes e funções membro especiais são seus. Copiar a fila
pode ser implementado (cópia independente) ou proibido com `= delete` — as duas escolhas
são aceitas.

Não altere `src/telemetry_log.h`, `examples/`, `tests/`, `test.sh`, `challenge.yaml`,
`Makefile` nem `Dockerfile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada teste aparece com ✔ ou ✘ e, quando falha, o motivo: a verificação que falhou, a
operação em que a divergência apareceu ou o erro relatado pelos sanitizers.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] tudo compila com g++ 14 em `-std=c++17 -pedantic-errors`, inclusive com um tipo sem construtor padrão;
- [ ] a fila aceita exatamente `capacity()` elementos (inclusive N = 1) e `full()`/`empty()`/`size()` estão sempre certos;
- [ ] `front()`, `back()`, `operator[]` e a iteração seguem a ordem do mais antigo ao mais novo, mesmo depois de muitas voltas;
- [ ] `push_overwrite()` com a fila cheia descarta exatamente o elemento mais antigo;
- [ ] 100 000 operações aleatórias dão o mesmo resultado que uma `std::deque` (capacidades 1, 7 e 16);
- [ ] objetos vivos == `size()` a todo momento: nada construído antes de entrar, nada vivo depois de sair, nenhuma destruição dupla;
- [ ] criar e usar a fila (e o log de telemetria) faz **zero** alocações no heap;
- [ ] nenhum erro do AddressSanitizer ou do UBSan;
- [ ] o log de telemetria guarda exatamente os 5 eventos mais recentes.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Data Structures the Fun Way*, Jeremy Kubica — cap. 4 "Stacks and Queues" (seção "Queues as Arrays": fila em array com índices que dão a volta).
- *Making Embedded Systems*, Elecia White — cap. 6, seção "Circular Buffers" (cheia × vazia: o lugar desperdiçado ou um contador; descartar dados novos ou antigos quando a fila enche).
- *C++ in Embedded Systems*, Amar Mahmutbegović — cap. 1, seção "Generic types" (ring buffer em C × template em C++, sem alocação dinâmica) e cap. 2 (por que evitar o heap em sistemas com poucos recursos).
- *Data Structures and Algorithms with the C++ STL*, John Farrier — cap. 6 (`std::deque`, usada aqui como modelo de referência) e cap. 9 (`std::queue` e adaptadores de contêiner).
- cppreference: [`std::optional`](https://en.cppreference.com/w/cpp/utility/optional), [placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new), [`std::deque`](https://en.cppreference.com/w/cpp/container/deque).

## Próximo desafio

**desafio-15 — Heap de prioridades**: um binary heap com decrease-key, usado por um
escalonador de tarefas. A disciplina de hoje — invariantes claros, testes de borda e
comparação com um modelo da STL — continua valendo.
