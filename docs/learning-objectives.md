# Objetivos de aprendizagem — desafio-6

Ao concluir este desafio você será capaz de:

1. Explicar por que, em endereçamento aberto, remover um elemento marcando o slot como vazio quebra buscas, e implementar remoção com lápides (ou *backward-shift deletion*).
2. Manter os contadores de uma tabela hash (`size`, lápides) coerentes em inserção, sobrescrita, remoção e *rehash*.
3. Respeitar um fator de carga máximo e escolher quando e para qual capacidade fazer *rehash*, garantindo término das buscas e custo O(1) amortizado sob ciclos de insere/apaga.
4. Reconhecer quando `hash & (capacidade - 1)` concentra chaves num único cluster e aplicar uma função de mistura de bits.
5. Validar uma estrutura de dados com teste diferencial contra `std::unordered_map`, verificação de invariantes, sanitizers e um teste de desempenho compilado separadamente.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Remoção sem quebrar clusters | `erase_keeps_rest_of_cluster_reachable` (hash constante: um único cluster, remoções no meio e no início); `reinsert_after_erase_does_not_duplicate` (inserção após remoção não cria cópia duplicada); testes diferenciais |
| 2. Contadores coerentes | `insert_or_assign_overwrites_without_growing_size`, `size_matches_elements` (compara `size()` com `for_each`), `erased_keys_stay_erased_after_growth` (chaves apagadas não voltam no *rehash*); `size()` comparado a cada operação nos testes diferenciais |
| 3. Fator de carga e *rehash* | `load_factor_never_exceeds_limit` e `load_factor_holds_with_erases_and_overwrites` (≤ 0,75 e potência de 2 após cada inserção); `churn_keeps_capacity_bounded` (200 mil chaves com ≤ 17 vivas: capacidade ≤ 128 e sem laço infinito, timeout de 20 s por teste) |
| 4. Mistura de bits | Teste de desempenho: 1 milhão de chaves `id << 32` em ≤ 2,5 s com `-O2` (referência ~0,25 s); sem mistura, o teste estoura o timeout |
| 5. Validação | `differential_*_vs_unordered_map` (230 mil operações com semente fixa); build de correção com ASan/UBSan e build de desempenho `-O2` separados no `Makefile` |
