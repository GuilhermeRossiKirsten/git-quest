# Dicas — desafio-30

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

A cache não traz bytes, traz **linhas de 64 bytes**. Para cada consulta, faça a conta:
quantos bytes ela *precisa* ler (por pedido, por devolução, por loja × dia, por busca) e
quantas linhas o código atual *realmente* toca? Onde está cada campo que a consulta usa
(`sizeof`, `offsetof`)? Para chegar ao campo, quantos ponteiros são seguidos, e para onde
eles apontam no heap?

## Nível 2

Abra `build/perf/callgrind.out.<n>` com `callgrind_annotate --auto=yes` (uma região por
arquivo: 1 ingest, 2 revenue, 3 units_by_day, 4 refunded_cents, 5 basket_value) e olhe as
colunas `D1mr`/`DLmr`. Depois, olhe os membros privados de `SalesStore` em
`src/sales_store.h`: há um registro gordo atrás de `std::unique_ptr`, uma matriz indexada
por `store * days_ + day` percorrida com o laço de fora em `day`, um `std::list` e um
`std::map` que nunca mudam depois de carregados. Compare cada um com o padrão de acesso da
consulta que o usa.

## Nível 3

- Separe o que é **quente** (lido pelas consultas) do que é **frio** (lido só por
  `find_order`/`find_product`): vetores paralelos (structure of arrays) ou uma struct quente
  pequena num `std::vector` de valores — sem `unique_ptr` no caminho.
- Na matriz de unidades, inverta a ordem dos laços para que o de dentro ande de 8 em 8 bytes
  (acumulando num vetor de totais por dia).
- Devoluções: um `std::vector` só com o par (dia, valor), e o resto à parte.
- Catálogo: o `load_catalog` recebe tudo de uma vez — ordene uma vez e busque com
  `std::lower_bound` num vetor só de SKUs (o preço num vetor paralelo, o produto completo em
  outro). Um `std::unordered_map` ainda segue um ponteiro por busca.
- Rode `./test.sh` depois de cada mudança: as respostas têm que continuar iguais.

## Nível 4

Se uma consulta ficou rápida mas um teste funcional quebrou, confira os detalhes do contrato:
intervalo de dias invertido devolve 0; os limites `min_amount_cents` são inclusivos; bits do
`status_mask` acima de 3 não selecionam nada; uma loja repetida na lista conta de novo; SKU
fora do catálogo conta em `unknown_lines`. Se o Ir estourou, procure trabalho extra no laço
quente (por exemplo, uma busca linear onde cabia uma binária).
