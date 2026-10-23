# desafio-13 · jthread e stop tokens

> Trilha: cpp · Padrão: **C++20** · Toolchain: **gcc:14.2.0** · Nível: advanced · ~60 min · 250 pontos

## Contexto

O `ingestor` é o serviço que recebe eventos de telemetria e os entrega ao armazenamento.
Por dentro é simples: alguns workers consomem uma fila bloqueante, e um heartbeat avisa
o monitoramento, a cada poucos segundos, quantos registros estão esperando.

Ele foi escrito em C++11 e "sempre funcionou" — até a equipe de plataforma começar a
fazer deploys várias vezes por dia. Agora o orquestrador mata o processo com `SIGKILL`
porque o encerramento não termina; nos dias em que termina, a auditoria acusa registros
aceitos que nunca chegaram ao armazenamento; e um pod caiu com `heap-use-after-free`
logo depois de o objeto ser destruído. Alguém começou a migração para C++20 (o
parâmetro `std::stop_token` de `BlockingQueue::pop`) e parou no meio.

Sua missão: fazer o serviço **encerrar rápido, sem perder nem duplicar nada e sem data
races**, mantendo a interface pública que outras equipes usam.

## Problema

Rodando `./test.sh`, você vai observar:

- `shutdown()` ou o destrutor **travam** (o teste é encerrado pelo watchdog, que diz em
  qual etapa ficou parado), ou levam **segundos** quando o heartbeat tem intervalo longo;
- depois de `close()`, só **parte** dos consumidores bloqueados em `pop()` acorda; um
  consumidor esperando com `pop(stop_token)` ignora o pedido de parada;
- consumidores recebem `std::nullopt` de `pop()` com a fila **ainda aberta**;
- `processed()` termina **menor** que o número de registros aceitos por `submit()`;
- depois que um `Heartbeat` ou `Ingestor` foi destruído, o processo ainda tem **threads a
  mais** e o AddressSanitizer acusa `heap-use-after-free` vindo de dentro delas;
- um `Ingestor` construído com intervalo de heartbeat inválido **trava ou derruba** o
  processo em vez de simplesmente lançar `std::invalid_argument`;
- o **ThreadSanitizer** relata `data race` em praticamente todos os testes de `Heartbeat` e
  `Ingestor`.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Usar `std::jthread` para que threads sejam paradas e juntadas automaticamente (RAII), inclusive quando um construtor lança.
- Implementar cancelamento cooperativo com `std::stop_token`, `std::stop_source` e `std::stop_callback`.
- Esperar em `std::condition_variable_any::wait(lock, stop_token, predicado)` sem despertares espúrios ou perdidos.
- Projetar shutdown gracioso (drena a fila) e interrupção (descarta) com garantia de exatamente uma vez.
- Diagnosticar data races, threads órfãs e travamentos com ThreadSanitizer, AddressSanitizer e testes com prazo.

## Conhecimentos necessários

- `std::thread`, `std::mutex`, `std::unique_lock` e `std::condition_variable` (o básico de C++11).
- `std::atomic` para contadores e flags simples.
- Ordem de construção e destruição de membros; o que acontece quando um construtor lança.
- Lambdas e `std::function`; noções de concepts/C++20 do desafio-4 ajudam a ler os headers.

## Ambiente

- Padrão exigido: **C++20** (`-std=c++20 -pedantic-errors`), g++ **14.2** (imagem Docker `gcc:14.2.0`).
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- O avaliador compila **duas variantes** — ASan+UBSan+LSan e ThreadSanitizer (não podem
  coexistir no mesmo binário) — e roda cada teste em processo próprio, com watchdog interno
  e `timeout` externo, num container sem rede e com limites de CPU, memória e PIDs.
- O ThreadSanitizer do gcc 14 não inicia em kernels com randomização de endereços agressiva
  (WSL2, Ubuntu 24.04: `FATAL: ThreadSanitizer: unexpected memory mapping`). Por isso os
  testes TSan rodam sob `setarch "$(uname -m)" -R`, que desliga a randomização só para o
  processo do teste. O perfil seccomp padrão do Docker Engine bloqueia essa chamada, então o
  `test.sh` usa `scripts/lib/seccomp-tsan.json`: o perfil padrão com uma única regra a mais
  (`personality(ADDR_NO_RANDOMIZE)`) — nada de `seccomp=unconfined` (veja docs/security.md no main).
- Sem Docker (somente Linux, g++ >= 14, `make` e `setarch` do util-linux):
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`.

## Como começar

```bash
./start.sh          # constrói a imagem da toolchain e compila o código inicial (2 variantes)
./test.sh           # ~1 min com o código inicial: alguns testes só falham no prazo
```

Para investigar:

```bash
GQ_VERBOSE=1 ./test.sh                          # log completo de cada teste que falhar
cat build/logs/tsan-heartbeat_periodico.log     # relatório inteiro do ThreadSanitizer
cat build/logs/asan-ingestor_destrutor_interrompe.log
```

Num relatório do TSan, leia os **dois** acessos: o atual e o `Previous`. As linhas em `src/`
mostram quais threads tocam a mesma memória sem sincronização. Uma mensagem
`tempo esgotado ... travou na etapa "..."` indica uma thread que nunca acordou.

## Arquivos que você pode modificar

- `src/blocking_queue.h` — `BlockingQueue<T>`;
- `src/heartbeat.h` e `src/heartbeat.cpp` — `Heartbeat`;
- `src/ingestor.h` e `src/ingestor.cpp` — `Ingestor`.

Mude à vontade membros privados, includes, tipos de thread, primitivas de sincronização e a
ordem dos membros. Mantenha a **API pública** e as garantias descritas nos comentários dos
headers (nomes, parâmetros, retornos, exceções, semântica de `shutdown()` × destrutor): os
testes dependem dela.

Não altere `tests/`, `test.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, g++ >= 14)
```

Cada verificação aparece como `[asan]` ou `[tsan]` com ✔ ou ✘ e, quando falha, o motivo: a
verificação que falhou e o tempo medido, a etapa em que travou, ou o tipo de relato do
sanitizer com as linhas de `src/` envolvidas.

## Critérios de conclusão

`./test.sh` termina com código 0 — as 16 verificações passam nas duas variantes (32 ✔):

- [ ] compila com g++ 14 em `-std=c++20 -pedantic-errors`;
- [ ] `close()` acorda todos os consumidores; `pop(stop_token)` volta em < 200 ms quando a parada é pedida; `pop()` nunca devolve `nullopt` com a fila aberta;
- [ ] `Heartbeat::stop()` e o destrutor levam < 200 ms mesmo com intervalo de 3 s, e nenhum beat acontece depois deles;
- [ ] `Ingestor::shutdown()` processa cada registro aceito exatamente uma vez e, ocioso, termina em < 200 ms;
- [ ] o destrutor sem `shutdown()` termina em < 200 ms, descarta a fila e nenhum handler roda depois dele;
- [ ] construtor que lança não aborta, não trava e não deixa threads;
- [ ] nenhuma thread sobra no processo depois de destruir qualquer objeto (50 ciclos de estresse inclusos);
- [ ] nenhum relato do ThreadSanitizer, AddressSanitizer, LeakSanitizer ou UBSan.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Asynchronous Programming with C++*, Javier Reguera-Salgado e Juan Antonio Rufes — cap. 3,
  "How to Create and Manage Threads in C++" (`std::jthread`, "Threads cancellation" com
  `stop_token`/`stop_source`/`stop_callback`, "Implementing a timer"); cap. 4, "Thread
  Synchronization with Locks" (condition variables, despertares espúrios, fila thread-safe);
  cap. 12, "Sanitizing and Testing Asynchronous Software" (ThreadSanitizer).
- *Modern C++ Programming Cookbook*, 3ª ed., Marius Bancila — cap. 8, "Leveraging Threading
  and Concurrency": "Sending notifications between threads" e "Using joinable threads and
  cancellation mechanisms".
- *C++ Concurrency in Action*, 2ª ed., Anthony Williams — cap. 2 (threads e exceções, `join`
  em todos os caminhos) e cap. 4 (fila thread-safe com variável de condição). *Citado de
  memória; o livro não está na biblioteca do projeto.*
- cppreference: [`std::jthread`](https://en.cppreference.com/w/cpp/thread/jthread),
  [`std::stop_token`](https://en.cppreference.com/w/cpp/thread/stop_token),
  [`std::stop_callback`](https://en.cppreference.com/w/cpp/thread/stop_callback),
  [`std::condition_variable_any::wait`](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait).
- [P0660R10 — Stop Token and Joining Thread](https://wg21.link/p0660r10), a proposta que trouxe `jthread` e `stop_token` ao C++20.
- [ThreadSanitizer](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual).

## Próximo desafio

**desafio-12 — Corrotinas que geram** (C++20/23): depois de encerrar threads com
elegância, troque threads por corrotinas e `std::generator` num leitor de arquivos preguiçoso.
