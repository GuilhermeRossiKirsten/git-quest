# Objetivos de aprendizagem — desafio-32

Ao concluir este desafio você será capaz de:

1. **Validar comprimentos declarados**: tratar todo campo de tamanho vindo de fora como uma
   alegação a ser conferida contra os bytes realmente disponíveis, antes de qualquer leitura
   (SEI CERT C ARR30-C; defeito do tipo CWE-125).
2. **Calcular tamanhos sem overflow**: saber em que tipo uma expressão é avaliada (promoção de
   inteiros), fazer contas de tamanho em `size_t` e conferir o limite antes de multiplicar
   (INT30-C, MEM35-C; CWE-190 levando a CWE-787).
3. **Copiar texto com limite e terminador**: reservar o byte do `'\0'` e derivar o limite do próprio
   buffer (`sizeof buf - 1`), eliminando erros por um (STR31-C; CWE-193/CWE-787).
4. **Evitar comportamento indefinido aritmético**: escolher um acumulador largo o bastante para o
   pior caso e entender por que overflow com sinal não é "só um valor errado" (INT32-C).
5. **Tratar erros por completo**: verificar todo retorno de erro e liberar, em qualquer caminho de
   rejeição, o que já foi alocado (EXP12-C, MEM31-C; CWE-401).
6. **Usar as ferramentas de diagnóstico**: ler relatórios de ASan/UBSan/LSan, reproduzir um caso a
   partir de um arquivo, e entender o que o fuzzing por mutação e a build endurecida
   (`_FORTIFY_SOURCE=3`, `-Wconversion`) acrescentam.

## Como os testes verificam cada objetivo

Todo pacote roda num processo novo do `tlvparse` compilado com `-fsanitize=address,undefined
-fno-sanitize-recover=all` (LSan ativo), e a resposta é comparada com o modelo executável do contrato
(`tests/model.py`). Qualquer relatório de sanitizer reprova o caso.

| Objetivo | Verificação |
|---|---|
| 1. Comprimentos | Grupo `comprimentos` (15 pacotes): pacote vazio, cabeçalho incompleto, registro com cabeçalho pela metade, `length` maior que o restante (NAME, LABEL, READING, SERIES, `0xFFFF` no fim), contagem maior que os registros, bytes sobrando. Todos precisam terminar com código 2, stdout vazio e `tlvparse: <motivo>` em stderr. |
| 2. Tamanhos | Grupo `contagens-de-serie` (7 pacotes): `n = 2³¹`, `n = 2³¹ + 2` e `n = 0xFFFFFFFF` com corpos pequenos (a conta em 32 bits "bate" com o `length`), `n` maior/menor que o corpo, corpo ímpar, série sem contagem. No grupo válido, uma série de 32 765 amostras (o máximo que cabe em 16 bits de `length`) e uma série vazia precisam ser aceitas. |
| 3. Textos | Grupo `textos` (12 pacotes): NAME de 32, 64 e 300 bytes, LABEL de 64 e 200 bytes, textos vazios, com `\n`, `\0` ou `0x7f`, dois NAMEs, nenhum NAME. No grupo válido, NAME de 31 e LABEL de 63 bytes são aceitos e impressos literalmente (inclusive `%`, `{`, `\`). |
| 4. Aritmética | Grupo `extremo` (6 pacotes válidos): duas leituras `INT32_MAX`, `INT32_MAX + 1`, três negativos extremos, 1024 × `INT32_MAX`, 1024 × `INT32_MIN`, `INT32_MIN` com `INT32_MAX`. A média tem de ser a exata (truncada em direção a zero) e o UBSan não pode acusar nada. Rejeitar esses pacotes reprova. |
| 5. Erros | Grupo `limites` (8 pacotes): 65 rótulos, 1025 leituras, 65 séries, e erros (tipo desconhecido, truncamento, READING de 3 bytes, NAME repetido) depois de já haver rótulos e séries alocados. Rejeição com código 2 e nenhum vazamento acusado pelo LeakSanitizer. |
| 6. Ferramentas | Fuzzing: 4000 pacotes (semente 32032) — 20% válidos sorteados com valores extremos e 80% mutações de pacotes válidos (bits invertidos, campos de 16/32 bits trocados por valores-limite, truncamento, remoção, duplicação e enxerto de registros) — passam por `tlv_process()` em laço sob ASan + UBSan; nenhum pode derrubar o parser e todos têm de concordar com o modelo (código e relatório). Build endurecida: `-O2 -D_FORTIFY_SOURCE=3 -Wconversion -Wsign-conversion -Werror` precisa compilar e repetir o relatório dos pacotes válidos. Todo caso reprovado é salvo em `build/failures/` para reprodução. |

O grupo válido (13 pacotes, incluindo um com todos os limites máximos ao mesmo tempo) garante que a
correção não troca um defeito por outro: um parser que rejeita tudo reprova em quatro critérios (válidos, extremos, fuzzing e build endurecida).
