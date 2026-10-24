# desafio-32 · O pacote que derrubava o coletor

> Trilha: security · Linguagem: **C17** · Toolchain: **gcc:14.2.0** · Nível: advanced · ~75 min · 250 pontos

## Contexto

Uma rede de estações meteorológicas manda telemetria para um coletor central. Cada sensor de campo
envia, a cada minuto, um pacote binário pequeno: nome da estação, rótulos, leituras e séries de
amostras. Quem decodifica esses pacotes é o `tlvparse`, um programinha em C que roda há anos
"sem problemas", porque os sensores sempre mandaram pacotes bem-formados.

Até que um lote de sensores recebeu um firmware novo com defeito. Desde então o coletor cai algumas
vezes por dia, às vezes publica médias absurdas e, numa reinicialização, tinha crescido de memória
sem motivo aparente. Ninguém controla o que chega pelo rádio: um pacote pode vir cortado, com campos
de tamanho que mentem, com contagens gigantes ou com textos maiores do que o combinado. O coletor
precisa **rejeitar com educação** tudo o que não seguir o formato, sem cair e sem aceitar lixo,
e continuar aceitando, byte a byte igual, tudo o que for válido.

## Problema

Com pacotes normais, o `tlvparse` funciona. Rodando `./start.sh` (que inclui uma demonstração) e
`./test.sh`, você vai observar:

- um pacote **cortado no meio** faz o programa abortar com `AddressSanitizer: heap-buffer-overflow`
  em vez de responder `tlvparse: ...` e sair com código 2;
- certos pacotes com uma **série de amostras** derrubam o programa, embora o campo de tamanho do
  registro pareça bater com a contagem declarada;
- um **nome de estação comprido** derruba o programa (`stack-buffer-overflow`), e um rótulo de
  exatamente 64 bytes provoca uma escrita fora do bloco alocado;
- um sensor que reporta **dois picos de leitura** (valores perto do máximo de `int32`) faz o UBSan
  acusar `signed integer overflow`; na build de produção, sem sanitizers, a média sai absurda;
- um pacote com **rótulos demais** é aceito (código 0), e o LeakSanitizer acusa memória vazada;
- o fuzzing do avaliador derruba o parser nas primeiras dezenas de mutações.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Validar todo comprimento declarado na entrada contra os bytes que realmente restam (CERT C ARR30-C).
- Calcular tamanhos em `size_t`, conferindo limites **antes** da multiplicação (INT30-C, MEM35-C).
- Copiar textos para buffers fixos com limite e espaço para o `'\0'` (STR31-C).
- Acumular valores num tipo largo o bastante: overflow com sinal é comportamento indefinido (INT32-C).
- Nunca ignorar um retorno de erro e liberar tudo em qualquer caminho de rejeição (EXP12-C, MEM31-C).
- Usar ASan, UBSan, LSan e fuzzing por mutação para achar e reproduzir defeitos de parsing.

## Conhecimentos necessários

- C (ponteiros, `memcpy`, `malloc`/`free`, `struct`), tipos de `<stdint.h>` e `size_t`.
- Regras de promoção e conversão de inteiros em C (por que `uint32_t * 2u` continua em 32 bits).
- Leitura de relatórios de sanitizers (pilha de chamadas, tipo de acesso, tamanho do bloco).
- Ter feito o desafio-21 ajuda (C17, Makefile com sanitizers, avaliador em Python).

## Ambiente

- Linguagem: **C17** (`-std=c17 -D_GNU_SOURCE`).
- Toolchain: imagem Docker `gcc:14.2.0` (gcc 14.2, make, ASan/UBSan/LSan, glibc com `_FORTIFY_SOURCE=3`, python3).
- Build dos testes: `-Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-sanitize-recover=all`.
- Build endurecida (a de "produção"): `-O2 -D_FORTIFY_SOURCE=3 -fstack-protector-strong -Wall -Wextra
  -Wpedantic -Wformat=2 -Wconversion -Wsign-conversion -Werror`, sem sanitizers.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: Linux com gcc >= 14, make e python3, usando `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`.

Os testes rodam dentro de um container sem rede, com limites de CPU, memória, PIDs e tempo.

### O contrato do `tlvparse` (não mude)

`tlvparse < pacote.bin` lê um pacote de stdin (no máximo 1 MiB). Inteiros em big-endian:

| Campo | Tamanho | Regra |
|---|---|---|
| magic | 4 | `SNS1` |
| version | 1 | `1` |
| count | 2 | quantidade de registros que vêm a seguir |
| registro × count | 3 + length | `type` (1), `length` (2), `value` (`length` bytes, todos dentro do pacote) |

Depois do último registro não pode sobrar nenhum byte. Tipos:

- `0x01 NAME`: ASCII imprimível (`0x20`..`0x7e`), 1 a 31 bytes; exatamente um por pacote.
- `0x02 READING`: exatamente 4 bytes, `int32`; no máximo 1024.
- `0x03 LABEL`: ASCII imprimível, 1 a 63 bytes; no máximo 64.
- `0x04 SERIES`: `uint32 n` seguido de exatamente `n` amostras `int16` (`length == 4 + 2n`); no máximo 64.
- qualquer outro tipo: o pacote é inválido.

Pacote aceito: código 0 e este relatório em stdout, nesta ordem: `sensor <nome>`; uma linha
`label <rótulo>` por LABEL; uma linha `reading <valor>` por READING; `avg <média>` (só se houver
leituras: média **exata** de todas, truncada em direção a zero); uma linha `series <a1> <a2> ...`
por SERIES (só `series` se `n == 0`). Rótulos, leituras e séries saem na ordem em que vieram.

Pacote rejeitado (qualquer violação): **nada** em stdout, uma linha `tlvparse: <motivo>` em stderr e
código **2**. Nunca abortar, nunca vazar memória, nunca ler ou escrever fora de um bloco.

O avaliador também chama `tlv_process()` (declarada em `src/tlv.h`) várias vezes no mesmo processo,
como um harness de fuzzing: mantenha essa assinatura e não guarde estado entre chamadas.

## Como começar

```bash
./start.sh          # constrói a imagem, compila e mostra três pacotes de campo
./test.sh           # mostra quais critérios falham e por quê
```

Ferramentas de diagnóstico, dentro de um container da toolchain
(`docker run --rm -it -v "$PWD":/work -w /work gcc:14.2.0 bash`; no Git Bash use
`MSYS_NO_PATHCONV=1 docker run --rm -it -v "$(pwd -W)":/work -w /work gcc:14.2.0 bash`):

- `python3 tests/packets.py list` lista os pacotes do avaliador; `python3 tests/packets.py write <caso> x.bin`
  grava um deles e `python3 tests/packets.py expect <caso>` mostra o que o contrato manda responder;
- todo pacote que reprova no `./test.sh` fica em `build/failures/`: `./build/tlvparse < build/failures/<caso>.bin; echo $?`;
- `xxd build/failures/<caso>.bin` para ver o pacote byte a byte;
- o relatório do ASan/UBSan aponta o arquivo e a linha; `gdb ./build/tlvparse` com
  `run < build/failures/<caso>.bin` (e `ASAN_OPTIONS=abort_on_error=1`) para parar no ponto exato;
- `make harden` compila a build endurecida e mostra os avisos de conversão.

## Arquivos que você pode modificar

- `src/` — o `tlvparse` (`tlv.c`, `tlv.h`, `main.c`). Pode reorganizar funções e estruturas à vontade,
  desde que o contrato acima e a assinatura de `tlv_process()` continuem valendo.

Não altere `tests/`, `test.sh`, `start.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`:
o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, gcc >= 14, python3)
```

O avaliador compila o `tlvparse` com ASan + UBSan + LSan (`-fno-sanitize-recover`) e compara cada resposta
com um modelo executável do contrato (`tests/model.py`):

1. pacotes **válidos**, inclusive nos limites máximos (nome de 31 bytes, 64 rótulos de 63 bytes,
   1024 leituras, 64 séries, série de 32 765 amostras), cada um num processo novo;
2. pacotes válidos com **leituras extremas**, cuja soma não cabe em 32 bits;
3. quatro grupos de pacotes **malformados** gerados de forma determinística (`tests/packets.py`):
   comprimentos e truncamento, contagens de série, textos, limites de quantidade e erros no meio do pacote;
4. **fuzzing** por mutação: 4000 variações (semente fixa 32032) de pacotes válidos, chamando
   `tlv_process()` em laço sob ASan + UBSan, com limite de tempo;
5. a **build endurecida** precisa compilar sem avisos e gerar o mesmo relatório para os pacotes válidos.

Cada critério aparece com ✔ ou ✘ e o motivo (primeira linha do relatório do sanitizer, arquivo e linha).

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o `tlvparse` compila em C17 com `-Wall -Wextra -Wpedantic -Werror`, ASan e UBSan;
- [ ] pacotes válidos, inclusive nos limites máximos, geram exatamente o relatório de referência;
- [ ] leituras extremas dão a média exata, sem comportamento indefinido;
- [ ] comprimentos declarados maiores que o pacote e pacotes truncados são rejeitados com código 2;
- [ ] contagens de SERIES enormes ou inconsistentes são rejeitadas com código 2;
- [ ] NAME e LABEL fora do tamanho ou com bytes proibidos são rejeitados com código 2;
- [ ] limites de quantidade e erros no meio do pacote levam à rejeição sem vazamento de memória;
- [ ] as 4000 mutações do fuzzing não derrubam o parser e todas as respostas seguem o contrato;
- [ ] a build endurecida (`-O2 -D_FORTIFY_SOURCE=3 -Wconversion -Wsign-conversion -Werror`) compila e gera o mesmo relatório.

Rejeitar tudo não passa: os pacotes válidos precisam continuar sendo aceitos.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- SEI CERT C Coding Standard (Carnegie Mellon): **ARR30-C** (não formar nem usar ponteiros/índices fora
  dos limites), **INT30-C** (operações sem sinal não podem dar a volta), **INT32-C** (operações com
  sinal não podem transbordar), **MEM35-C** (alocar memória suficiente para o objeto), **STR31-C**
  (garantir espaço para os caracteres e o terminador), **EXP12-C** (não ignorar valores devolvidos por
  funções), **MEM31-C** (liberar a memória dinâmica quando não for mais necessária).
- *Secure Coding in C/C++ — Cheat Sheet*, Károly Nyisztor — "Principles of Secure C and C++ Programming"
  (validação de entrada, *fail-safe defaults*, defesa em profundidade) e "Common Security Pitfalls":
  item 1 (estouro de buffer e as soluções com cópia limitada e terminador), item 2 (conversões de tipo
  arriscadas), item 4 (tratamento de erros: retornos ignorados e vazamento de recursos) e item 6
  (erros de gerenciamento de memória).
- *Programação em Baixo Nível*, Igor Zhirkov — seção 9.1.4 (conversões implícitas e promoção de
  inteiros), seção 11.5 (modelos de dados, LP64 e tipos de tamanho fixo de `stdint.h`), seção 12.3.1
  (comportamento indefinido: overflow com sinal, acesso fora dos limites de um array, corrupção de heap
  como "bomba-relógio") e seção 12.4.2 (por que ler cabeçalhos binários campo a campo, e não com `fread`
  direto numa `struct`).
- Classificação dos defeitos (MITRE CWE): CWE-125 (leitura fora dos limites), CWE-787 (escrita fora
  dos limites), CWE-190 (overflow de inteiro), CWE-193 (erro por um, *off-by-one*), CWE-401 (vazamento de memória).
- Documentação do GCC: `-fsanitize=address,undefined`, `-Wconversion`, `-Wsign-conversion`;
  `feature_test_macros(7)` (`_FORTIFY_SOURCE`); documentação do AddressSanitizer/LeakSanitizer (google/sanitizers).

## Próximo desafio

Este é o último desafio da trilha security por enquanto. Volte ao catálogo (`./scripts/gitquest list`)
e aplique o mesmo olhar desconfiado a qualquer código que leia bytes vindos de fora: arquivos,
sockets, filas, configurações.
