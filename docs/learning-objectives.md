# Objetivos de aprendizagem — desafio-13

Ao concluir este desafio você será capaz de:

1. **Gerenciar o ciclo de vida de threads com `std::jthread`**: garantir que toda thread
   seja parada e juntada quando seu dono morre — no destrutor e quando um construtor
   lança no meio do caminho — sem `detach()` e sem `std::terminate`.
2. **Implementar cancelamento cooperativo** com `std::stop_token`, `std::stop_source` e
   `std::stop_callback`: quem pede parada não precisa conhecer a espera em que a outra
   thread está bloqueada.
3. **Esperar corretamente numa variável de condição**: usar predicados (despertares
   espúrios e "roubados"), `notify_all` quando todos precisam reagir, e
   `std::condition_variable_any::wait`/`wait_for` com `stop_token` em vez de `sleep_for`.
4. **Projetar encerramentos com garantias**: shutdown gracioso que processa cada item
   aceito exatamente uma vez, e interrupção rápida que descarta a fila sem rodar handlers
   depois do destrutor.
5. **Diagnosticar concorrência com ferramentas**: ler relatórios do ThreadSanitizer (data
   race com os dois acessos), do AddressSanitizer (uso após liberação por threads órfãs) e
   interpretar testes com prazo (deadlock/travamento).

## Como os testes verificam cada objetivo

Cada teste roda em processo próprio, duas vezes: com ASan+UBSan+LSan e com ThreadSanitizer
(`halt_on_error=1`; qualquer data race reprova). Um watchdog encerra o teste que passar do
prazo e informa a etapa em que travou. A contagem de threads do processo
(`/proc/self/task`) é comparada antes e depois de cada objeto.

| Objetivo | Verificação |
|---|---|
| 1. `std::jthread` e RAII | `heartbeat_destrutor` e `ingestor_destrutor_interrompe`: destrutor em < 200 ms, nenhuma thread a mais no processo, nenhum callback depois do destrutor. `ingestor_construtor_lanca`: intervalo de heartbeat inválido lança `invalid_argument` depois que os workers já existem — o processo não pode abortar nem travar e não pode sobrar thread. `estresse_50_ciclos` repete o ciclo de vida 50 vezes. |
| 2. Cancelamento cooperativo | `fila_stop_token`: `pop(stop_token)` bloqueado volta em < 200 ms depois de `jthread::request_stop()` e de `stop_source::request_stop()`, sem acordar quem espera com outro token. `heartbeat_stop_rapido` e `ingestor_shutdown_rapido`: parar com heartbeat de 3 s leva < 200 ms. |
| 3. Variável de condição | `fila_close_acorda_todos`: `close()` acorda os 4 consumidores em < 200 ms. `fila_mpmc`: 4×4 threads, 20 000 itens, nenhum `nullopt` com a fila aberta e cada item exatamente uma vez. |
| 4. Encerramentos com garantias | `ingestor_exatamente_uma_vez` (12 000 registros), `ingestor_submit_concorrente` (todo registro aceito é processado uma vez, recusados nunca), `ingestor_heartbeat` (heartbeat relata pendentes e para junto), `ingestor_destrutor_interrompe` (descarta a fila, < 200 ms, sem duplicatas). |
| 5. Diagnóstico | O avaliador mostra o tipo de relato do TSan/ASan e as linhas de `src/` dos dois acessos conflitantes; travamentos mostram a etapa; tempos medidos aparecem na mensagem. |
