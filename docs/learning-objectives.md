# Objetivos de aprendizagem — desafio-33

Ao concluir este desafio você será capaz de:

1. **Publicar dados entre threads com o modelo de memória do C++**: explicar por que
   `memory_order_relaxed` não basta numa fila (ele garante atomicidade e a ordem das
   modificações *daquela* variável, mas não ordena as escritas não atômicas do slot em
   relação ao índice, então não cria *happens-before*), e usar `store(release)` no índice
   próprio + `load(acquire)` no índice oposto. Saber que `seq_cst` também é correto, só mais
   caro, e que "funciona no x86" não é argumento: data race é comportamento indefinido e o
   compilador também reordena.
2. **Gerenciar o ciclo de vida de objetos em memória crua**: construir com
   `std::construct_at`/placement new só quando o item entra, destruir com `std::destroy_at`
   quando sai e, no destrutor da fila, destruir exatamente os itens restantes — sem
   vazamento, destruição dupla, "T de reserva" nem item fantasma após exceção.
3. **Implementar um anel de capacidade arbitrária**: separar capacidade pedida e tamanho
   do anel (potência de 2 para a máscara), usar contadores livres para distinguir cheia de
   vazia sem desperdiçar slot e recusar capacidade inválida.
4. **Eliminar false sharing e verificá-lo de forma determinística**: colocar em linhas de
   cache diferentes o que cada thread escreve (`alignas(64)`) e descrever o layout por uma API
   de introspecção conferida contra endereços reais, em vez de depender de medições ruidosas.
5. **Reduzir true sharing**: manter em cada lado uma cópia local do índice oposto e relê-lo
   só quando a cópia indicar fila cheia/vazia.
6. **Validar código lock-free**: ler relatórios do ThreadSanitizer (os dois acessos
   conflitantes), do LeakSanitizer e de testes de estresse com sequência e checksum.

## Como os testes verificam cada objetivo

Cada teste roda em processo próprio, nas variantes `[asan]` (ASan+UBSan+LSan) e `[tsan]`
(ThreadSanitizer, `halt_on_error=1`). Um watchdog encerra o teste que passar do prazo e
informa a etapa. A medição `[perf]` contra `std::mutex` + `std::deque` é só informativa.

| Objetivo | Verificação |
|---|---|
| 1. release/acquire | `ordem_publicacao`: com o índice instrumentado (`tests/support/probe_index.h`), toda escrita do próprio índice precisa ser `release`/`seq_cst` e toda leitura do índice oposto, `acquire`/`seq_cst`. `estresse_sequencia` (1–4 milhões de inteiros, capacidade 1000), `estresse_objetos` e `feed_mercado` com duas threads: o TSan relata data race no slot se não houver *happens-before* — não depende da sorte do agendamento. |
| 2. Ciclo de vida | `vida_elementos`, `vida_fila_cheia_destruida`, `vida_excecao_no_construtor` com o tipo `Tracked` (conta construções/destruições e detecta destruição dupla ou de memória crua). `vida_strings_heap` e `estresse_objetos`: o LeakSanitizer acusa strings/objetos que sobram na fila e não são destruídos. `tipo_so_movivel` com `std::unique_ptr`. |
| 3. Anel e capacidade | `capacidade_exata`: 1, 2, 3, 4, 5, 7, 8, 64, 100, 1000 e 1024, cada uma com 3 voltas completas e alternância push/pop. `capacidade_zero`. `fifo_basico`. |
| 4. False sharing | `layout_indices_separados`: endereços reais dos dois índices (heap, pilha e array de filas) em linhas de 64 bytes diferentes e fila alinhada a 64; `layout_info()` precisa bater com esses endereços. `layout_info_linhas`: `alignof >= 64` e nenhuma linha com campos escritos pelos dois lados, para `int`, `std::string` e `Quote`. |
| 5. True sharing | `cache_indice_oposto`: em 50 voltas de "enche/esvazia" (3250 operações por lado) cada lado pode ler o índice oposto no máximo 406 vezes (1/8); sem cópia local são ~3250. |
| 6. Ferramentas | O avaliador mostra o tipo de relato do TSan/ASan/LSan com as linhas de `src/` dos dois acessos; travamentos mostram a etapa; medições aparecem nas linhas `·`. |
