# Objetivos de aprendizagem — desafio-30

Ao concluir este desafio você será capaz de:

1. Explicar por que o custo de memória depende das **linhas de cache** tocadas e medir misses
   de L1/LL de forma reprodutível com o simulador de cache do Valgrind (cache fixa, regiões
   delimitadas por client requests).
2. Separar dados quentes de frios: transformar um *array of structs* gordo em *structure of
   arrays* (ou numa struct quente compacta).
3. Eliminar indireção por ponteiro (`std::vector<std::unique_ptr<T>>`) e listas ligadas com
   nós espalhados no heap.
4. Percorrer uma matriz na ordem em que ela está na memória (passo de 8 bytes) em vez de
   coluna a coluna.
5. Trocar um contêiner baseado em nós (`std::map`) por um vetor ordenado + busca binária
   quando os dados são carregados uma vez e só lidos depois.
6. Otimizar sem mudar a API nem os resultados, conferindo cada resposta contra um gabarito.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Medir linhas de cache | `tests/perf.sh` roda `tests/bench.cpp` (-O2) sob `valgrind --tool=callgrind --cache-sim=yes` com D1 32 KiB/8 vias e LL 2 MiB/16 vias, sem ASLR; cada consulta é uma região (`CALLGRIND_TOGGLE_COLLECT`/`DUMP_STATS_AT`) começando com a cache "fria"; confere que a cache simulada foi a pedida |
| 2. Quente × frio | `revenue`: misses de D1 ≤ 180 000 e de LL ≤ 60 000 (código inicial: ~633 000 cada); `refunded_cents`: D1 ≤ 60 000 e LL ≤ 15 000 (inicial: ~160 000 e ~136 000) |
| 3. Sem indireção / sem lista | mesmos critérios de `revenue` e `refunded_cents`: um `vector<unique_ptr<T>>` ou um vetor de registros gordos continua acima do limite |
| 4. Ordem da matriz | `units_by_day`: misses de D1 ≤ 119 000 (inicial: ~407 000) |
| 5. Vetor ordenado | `basket_value`: misses de D1 ≤ 520 000 (inicial: ~1,3 milhão; `std::map` enxuto ou `unordered_map` continuam acima) |
| 1–5. Sem trabalho extra | Ir de cada consulta ≤ 1,5× o inicial; ingestão ≤ 2× o inicial (Ir e D1) |
| 6. Mesmos resultados | `test_01_api` (cenário calculado à mão, campos completos), `test_02_diferencial` (várias redes e sementes contra o `Model` + checksum), `test_03_bordas` (limites, máscaras, SKUs extremos), tudo com ASan + UBSan; o bench também confere cada resposta com o gabarito |
