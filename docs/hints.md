# Dicas — desafio-32

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelos sintomas, um critério de cada vez. Todo pacote que reprova fica em `build/failures/`;
reproduza-o dentro do container com `./build/tlvparse < build/failures/<caso>.bin; echo $?` e leia o
relatório do sanitizer de cima para baixo: tipo de erro, `READ`/`WRITE`, tamanho, e a primeira linha
da pilha que está em `src/`.

Perguntas para cada valor que vem do pacote:

- Quem garante que os `length` bytes que o registro **diz** ter realmente **existem** no buffer?
- Em que tipo é feita a conta `count * 2`? Qual é o maior valor que ela pode ter, e o que acontece
  quando o resultado não cabe nesse tipo?
- Um buffer de 32 bytes guarda uma string de quantos caracteres, contando o `'\0'`?
- Qual é a maior soma possível de 1024 valores `int32`? Cabe em 32 bits?
- Quando uma função devolve erro, quem fica responsável pelo que ela não guardou?

## Nível 2

Onde olhar, em `src/tlv.c`:

- `tlv_process`: entre ler `vlen` e chamar `do_name`/`do_label`/`do_reading`/`do_series`, nada compara
  `vlen` com o que resta de `in`. Os handlers confiam que `val[0..vlen)` é memória válida.
- `do_series`: `bytes` é `uint32_t`. Com `count = 0x80000002`, quanto vale `count * 2u`? A comparação
  com `vlen` passa, o `malloc` recebe um tamanho pequeno e o laço anda `count` vezes.
- `do_name`: há um limite inferior para `vlen`, mas e o superior? `buf` tem `NAME_CAP` bytes.
- `do_label`: o limite usa `LABEL_CAP`, mas o terminador vai em `text[vlen]`. Com `vlen == LABEL_CAP`?
- `do_reading` e `struct report`: o tipo de `sum`. Compare com a seção 12.3.1 do Zhirkov (overflow com sinal).
- `label_push` devolve `-1` quando a tabela está cheia. Quem lê esse retorno? O que acontece com `text`?
- `make harden` mostra onde a build endurecida reclama de conversões implícitas (`-Wconversion`).

## Nível 3

Abordagem (sem entregar o código):

- Valide **antes** de usar: para cada registro, confira que cabem os 3 bytes de cabeçalho e depois
  que `vlen <= len - off - 3`. Escreva a comparação pelo lado que não transborda (subtraia de quem
  é maior, nunca some ao índice e compare com `len`), como recomenda a ARR30-C.
- Em `do_series`, não multiplique um valor que veio da rede antes de saber que ele é pequeno.
  Derive o número máximo de amostras de `vlen` (que é limitado a 16 bits) e compare `count` com isso,
  exigindo também que `vlen - 4` seja par (INT30-C, MEM35-C). Cuidado com `malloc(0)` quando `n == 0`.
- Para textos, o tamanho máximo aceito é a capacidade **menos um**; use `sizeof buf - 1` em vez de
  repetir constantes (STR31-C).
- Acumule a soma em `int64_t` (1024 · 2³¹ < 2⁶³) e imprima a média com o especificador certo.
  Não rejeite o pacote: ele é válido.
- Verifique o retorno de `label_push`; se falhar, libere `text` e rejeite. Depois, confira que todo
  `return` de erro em `tlv_process` passa por `report_free`.
- Rode `./test.sh` depois de cada correção: cada critério corresponde a um grupo de defeitos.

## Nível 4

Se só o fuzzing ainda falha, abra o pacote salvo (`xxd build/failures/fuzz-NNNN.bin`) e escreva à mão o
que o contrato manda responder (`python3 tests/packets.py` mostra como os pacotes são montados;
`tests/model.py` é o contrato executável). Divergências comuns: aceitar um pacote com bytes sobrando,
rejeitar uma série com `n == 0`, contar o limite de rótulos antes ou depois do registro errado,
ou imprimir algo em stdout antes de descobrir que o pacote é inválido.
