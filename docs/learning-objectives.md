# Objetivos de aprendizagem — desafio-26

Ao concluir este desafio você será capaz de:

1. **Escrever uma arena que respeita alinhamento**, inclusive para tipos superalinhados
   (`alignas(64)`) e pedidos maiores que um bloco, sem sobrepor pedaços.
2. **Separar memória crua de tempo de vida**: registrar destrutores de objetos colocados na
   arena e executá-los (em ordem inversa) antes de devolver os blocos.
3. **Implementar `std::pmr::memory_resource` corretamente**, em especial `do_is_equal`, e
   entender que allocators iguais autorizam um contêiner a adotar a memória do outro.
4. **Propagar o resource para elementos aninhados** (`pmr::string` em `pmr::vector` e
   `pmr::map`) via *uses-allocator construction*, sem temporários no heap global.
5. **Escolher o resource certo para cada resultado**: rascunho num
   `monotonic_buffer_resource` reaproveitado, resultado no resource do chamador.
6. **Medir e diagnosticar**: contar alocações no heap global, pedidos ao upstream e usar
   ASan/UBSan/LSan para achar desalinhamento, vazamento e uso após liberação.

## Como os testes verificam cada objetivo

Todos os testes rodam com ASan/UBSan/LSan, um processo por teste. As alocações globais são
contadas num `operator new` substituído (`tests/support/heap_meter.cpp`); o upstream das
arenas é um resource de teste baseado em `malloc` que conta blocos e devolve com `free`.

| Objetivo | Verificação |
|---|---|
| 1. Alinhamento | `arena_alinhamento_basico` (2–16 bytes após pedidos de tamanho ímpar; `make<Vec4>`, `make<long double>`), `arena_alinhamento_64` (64 a 4096 bytes, inclusive em bloco novo e pedido maior que o bloco), `arena_sem_sobreposicao` (padrões de bytes conferidos), `resource_alinhamento_pmr` (`pmr::vector` de `alignas(64)` e `monotonic_buffer_resource` com upstream `ArenaResource`); o UBSan acusa os contadores `alignas(64)` do índice em todos os testes `index_*`. |
| 2. Destrutores | `arena_destrutores`: `LifeProbe` vivos = 0 depois de `reset()` e de `~Arena()`, ordem `[4, 3, 2, 1, 0]`, e LSan sem vazamento de um objeto que possui `std::string`/`std::vector`; `arena_reset_devolve_blocos` (0 blocos pendentes no upstream). |
| 3. `do_is_equal` | `resource_igualdade` (arenas diferentes não são iguais; simetria), `resource_move_entre_arenas` (mover `pmr::vector`/`pmr::string` e destruir a arena de origem: sem use-after-free), `index_absorb_shards` (shards destruídos logo após `absorb`). |
| 4. Propagação | `index_ingest_sem_heap_global` (20 mil linhas, 0 alocações globais) e `index_absorb_shards` (0 alocações globais); `index_equivalente_referencia` garante que o conteúdo continua igual ao de uma implementação ingênua com `std::map`/`std::string`. |
| 5. Resource do resultado | `index_top_services` (ranking igual à referência; vetor e strings com `resource() == out`; 0 alocações globais) e `index_top_services_sobrevive` (resultado intacto após novas consultas e após destruir o índice, sob ASan). |
| 6. Medir | `arena_blocos_grandes` e `index_ingest_em_blocos` (orçamento de chamadas e de bytes ao upstream: uma ida por alocação reprova), além dos relatórios de ASan/UBSan/LSan exibidos em cada ✘. |
