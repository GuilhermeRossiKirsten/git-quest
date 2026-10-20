# Objetivos de aprendizagem — desafio-12

Ao concluir este desafio você será capaz de:

1. Escrever corrotinas `std::generator` que produzem valores sob demanda, lendo da fonte só o necessário para o próximo item.
2. Garantir a liberação de recursos com RAII no quadro da corrotina, inclusive quando o generator é destruído enquanto está suspenso.
3. Propagar exceções do corpo de um generator até o ponto de consumo, sem engoli-las e depois de entregar os itens anteriores.
4. Controlar o tempo de vida do que é produzido: quando um `std::string_view` basta e quando o valor precisa ser dono do texto.
5. Compor generators com adaptadores de ranges (`views::filter`, `views::take`, `ranges::to`).
6. Escrever generators recursivos com `std::ranges::elements_of`, sem cópias e com custo por item independente da profundidade.

## Como os testes verificam cada objetivo

Todos os testes estão em `tests/runtime_tests.cpp`; cada um roda em processo próprio, sob ASan/UBSan, com tempo limite de 30 s.

| Objetivo | Verificação |
|---|---|
| 0. Toolchain | `toolchain_generator`: `__cpp_lib_generator >= 202207L` e `__cpp_impl_coroutine` presentes |
| 1. Preguiça | `lines_preguicoso` (nada é aberto antes de iterar; 10 linhas de ~2 MiB leem no máximo o consumido + 2 blocos, medido em `MemorySource::stats().bytes_read`), `pipeline_composto`, `first_matching_preguicoso`; corretude da divisão em `lines_divide_corretamente` e `lines_atravessa_blocos` (blocos de 3 e 1000 bytes, linhas maiores que um bloco) |
| 2. RAII na corrotina | `lines_fecha_ao_parar` (fonte aberta enquanto o consumidor pode pedir mais; destruir o generator no meio fecha), `lines_fecha_no_fim`, e o contador de fechamentos nos testes de erro e de `first_matching` |
| 3. Exceções | `lines_erro_de_leitura` e `records_erro_de_leitura` (`gq::io_error` chega depois das 1200/100 linhas completas), `records_erro_de_parse` (`gq::parse_error` da linha 4 chega depois dos 2 registros anteriores) |
| 4. Tempo de vida | `records_sobrevivem` e `records_interpreta`: registros coletados com `views::take | ranges::to` são lidos depois que o generator acabou; views penduradas são apontadas pelo AddressSanitizer (`heap-use-after-free`) ou pelo conteúdo errado; `first_matching_bordas` cobre limite 0, nível inexistente e menos ocorrências que o limite |
| 5. Composição | `pipeline_composto` usa `records(src) | views::filter | views::take(3)` e confere resultado e bytes lidos; `first_matching_preguicoso` confere o mesmo para a API |
| 6. Recursão | `walk_ordem` (pré-ordem e caminhos), `walk_sem_copias` (`gq::Dir::copies() == 0` em 421 diretórios), `walk_profundo` (4000 níveis sem estouro de pilha), `walk_custo_por_item` (o custo de produzir 3000 arquivos a 1500 níveis é comparado com o de 3000 arquivos na raiz, com caminhos do mesmo tamanho; precisa ficar abaixo de 5×) |
