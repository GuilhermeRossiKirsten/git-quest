# desafio-21 · O processo que não morria

> Trilha: linux · Linguagem: **C17 + POSIX** · Toolchain: **gcc:14.2.0** · Nível: intermediate · ~60 min · 200 pontos

## Contexto

A equipe de plataforma roda seus workers dentro de containers com um supervisor caseiro,
o `minisup`: um programinha em C que lê uma lista de serviços, inicia cada um, junta a
saída de todos no log do container (com o nome do serviço na frente de cada linha),
reinicia quem cai e, quando o orquestrador manda `SIGTERM`, encerra tudo com educação.

Pelo menos é o que diz o comentário no topo do `minisup.c`. Na prática, o plantão coleciona
reclamações: o `ps` de um container antigo mostra dezenas de processos `<defunct>`; jobs em
lote que terminaram há horas continuam "rodando" segundo o orquestrador; um worker que o
OOM killer derrubou nunca voltou, e o container saiu com código 0 como se nada tivesse
acontecido; um deploy esperou o timeout inteiro porque o container não morria; e,
depois que ele morreu, ainda havia processos do worker vivos no nó.

Você herdou o `minisup`. A interface (linha de comando, arquivo de configuração e formato
da saída) é usada por dezenas de serviços e não pode mudar — o problema está nas entranhas.

## Problema

O código compila sem avisos e "funciona" numa demonstração rápida. Rodando `./start.sh`
(que inclui uma demonstração com `ps` e `/proc`) e `./test.sh`, você vai observar:

- processos no estado **`Z` (zumbi)** cujo pai (`PPID`) é o `minisup`, mesmo bem depois de terem terminado;
- quando todos os serviços já terminaram, o `minisup` **continua rodando para sempre**, em vez de sair sozinho;
- `ls -l /proc/<pid>/fd` de um serviço mostra descritores que **ele nunca abriu**: o arquivo de log do
  supervisor e pipes de outros serviços;
- um serviço morto por `SIGKILL` aparece no resumo como **"saiu com código 0"**, não é reiniciado e o
  `minisup` termina com código 0;
- ao receber `SIGTERM`, as últimas linhas que os serviços escrevem ao se despedir **somem**;
- um serviço que ignora `SIGTERM` faz o `minisup` **esperar para sempre**, mesmo com a carência (`-g`) configurada;
- depois que o `minisup` sai, **processos criados pelos serviços (netos) continuam vivos**.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Criar processos com `fork`/`exec` e colher **todos** os filhos com `waitpid`, inclusive órfãos adotados por um *subreaper*.
- Entender quem precisa fechar cada ponta de um pipe para que o leitor veja EOF.
- Impedir que descritores vazem através do `exec` (`O_CLOEXEC`, `pipe2`, `fopen(..., "e")`).
- Decodificar o status de término (`WIFEXITED`/`WEXITSTATUS`, `WIFSIGNALED`/`WTERMSIG`).
- Escrever handlers de sinal async-signal-safe (self-pipe) e tratar `EINTR`.
- Desligar uma árvore de processos com grupos de processos, carência e `SIGKILL`.

## Conhecimentos necessários

- C (ponteiros, structs, `errno`) e compilação com `make`.
- Noções de processos no Linux: PID, processo pai, `fork`, `exec`, sinais.
- Leitura de páginas de manual (`man 2 waitpid`, `man 7 signal-safety`, `man 2 pipe`).

## Ambiente

- Linguagem: **C17** (`-std=c17 -D_GNU_SOURCE`) com APIs POSIX/Linux (`prctl`, `pipe2`, `sigabbrev_np`).
- Toolchain: imagem Docker `gcc:14.2.0` (gcc 14.2, make, ASan/UBSan, python3 para o avaliador, procps).
- Flags fixas: `-Wall -Wextra -Wpedantic -Werror` e `-fsanitize=address,undefined`.
- Docker >= 20.10 e Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- Sem Docker: Linux com gcc >= 14, make, python3 e procps, usando
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh` (o desafio depende de `/proc`, `prctl` e
  grupos de processos: macOS não serve).

Os testes rodam dentro de um container sem rede, com limites de CPU, memória, PIDs e tempo.

### O contrato do `minisup` (não mude)

```text
minisup [-g carencia_ms] [-b backoff_ms] [-l arquivo_de_log] arquivo.conf
```

- `arquivo.conf`: uma linha por serviço, `<nome> <max_reinicios> <comando> [argumentos...]`
  (`#` inicia comentário). Veja `examples/demo.conf`.
- Cada serviço roda com stdout e stderr ligados a um pipe; cada linha vira `<nome> | <linha>` na saída
  padrão do `minisup`. Um resto final sem `\n` também é repassado.
- Um serviço que termina **com falha** (código ≠ 0 ou morte por sinal) é reiniciado até
  `max_reinicios` vezes, com espera de `backoff_ms · 2^n` (máx. 5 s). Saída com 0 não é falha.
- O `minisup` é *subreaper* (`PR_SET_CHILD_SUBREAPER`): órfãos criados pelos serviços viram filhos dele.
- **Fim natural**: quando todos os serviços terminaram sem reinício pendente e a saída deles acabou,
  o `minisup` imprime o resumo e sai.
- **Desligamento** (`SIGTERM` ou `SIGINT`): nenhum reinício novo; `SIGTERM` para cada serviço **e para
  os processos que ele criou**; quem não sair em `carencia_ms` (padrão 2000) leva `SIGKILL`. A saída
  escrita pelos serviços até o fim é repassada. Morrer pelo `SIGTERM` do desligamento conta como término limpo.
- **Resumo**: uma linha por serviço, na ordem do arquivo:
  `minisup: <nome>: saiu com código N, reinícios: K` ou
  `minisup: <nome>: morto pelo sinal N (SIGXXX), reinícios: K`.
- **Código de saída**: 0 se todos terminaram limpos; senão, o do primeiro serviço (na ordem do arquivo)
  que não terminou limpo: `N` para "saiu com código N", `128 + N` para "morto pelo sinal N".
  Erro de uso ou de configuração: 2.
- Eventos (inícios, términos, reinícios) vão para o arquivo de `-l` (ou para stderr sem `-l`).

## Como começar

```bash
./start.sh          # constrói a imagem, compila e roda a demonstração (ps, /proc/<pid>/fd, SIGTERM)
./test.sh           # mostra quais critérios falham e por quê
GQ_VERBOSE=1 ./test.sh   # inclui a saída e o log de eventos do minisup nos cenários que falharem
```

Ferramentas de diagnóstico — no seu Linux ou dentro de um container da toolchain
(`docker run --rm -it -v "$PWD":/work -w /work gcc:14.2.0 bash`; no Git Bash use
`MSYS_NO_PATHCONV=1 docker run --rm -it -v "$(pwd -W)":/work -w /work gcc:14.2.0 bash`):

- `ps -eo pid,ppid,pgid,stat,args` — estado (`Z` = zumbi), pai e grupo de cada processo;
- `ls -l /proc/<pid>/fd` — os descritores abertos de um processo;
- `cat /proc/<pid>/status` — `PPid`, `State`, `SigIgn`/`SigBlk`;
- `strace -f -e trace=process,signal,pipe2,close,dup2 build/minisup ...` (se tiver `strace` instalado);
- o log de eventos (`-l`) e o código de saída (`echo $?`).

## Arquivos que você pode modificar

- `src/` — todo o código do `minisup` (`minisup.c`, `supervisor.c/.h`, `eventlog.c/.h`, `relay.c/.h`,
  `config.c/.h`). Pode reorganizar funções e estruturas à vontade, desde que o contrato acima continue valendo.

Não altere `tests/`, `examples/`, `test.sh`, `start.sh`, `challenge.yaml`, `Makefile` nem `Dockerfile`:
o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                     # ou: ./scripts/gitquest test
GITQUEST_LOCAL=1 ./test.sh    # sem Docker (Linux, gcc >= 14, python3)
```

O avaliador compila o `minisup` com ASan + UBSan e o executa em cenários reais com serviços de mentira
(`tests/helper.c`): um que escreve 20 000 linhas, um que cria um neto e sai, trinta que terminam no
mesmo instante, um que lista os próprios descritores, um que morre por `SIGKILL`, um que se despede ao
receber `SIGTERM`, um que ignora `SIGTERM` e um que deixa um neto dormindo. Ele inspeciona `/proc`
(estado, `PPID`, ambiente) e mostra cada critério com ✔ ou ✘ e o motivo.

## Critérios de conclusão

`./test.sh` termina com código 0, o que exige:

- [ ] o `minisup` compila em C17 com `-Wall -Wextra -Wpedantic -Werror`, ASan e UBSan;
- [ ] a saída dos serviços é repassada linha a linha, completa e em ordem, e o `minisup` sai sozinho (código 0) quando todos terminam;
- [ ] nenhum processo zumbi com `PPID` = `minisup`, inclusive órfãos adotados e uma rajada de 30 términos simultâneos;
- [ ] um serviço só enxerga os descritores 0, 1 e 2;
- [ ] morte por sinal é reconhecida como falha, reiniciada até o limite e refletida no resumo e no código de saída (`128 + sinal`);
- [ ] `SIGTERM` e `SIGINT` encerram tudo de forma ordenada, sem perder as últimas linhas dos serviços;
- [ ] a carência é respeitada e quem ignora `SIGTERM` recebe `SIGKILL` (código 137);
- [ ] nenhum processo criado por um serviço sobrevive ao `minisup`;
- [ ] nenhum relatório do AddressSanitizer/UndefinedBehaviorSanitizer/LeakSanitizer.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *C++ Programming for Linux Systems*, Desislav Andreev e Stanimir Lukanov — cap. 2 "Learning More about
  Process Management" (`fork`, `exec`, `wait`, término de processos) e cap. 3 "Navigating through the
  Filesystems" (pipes anônimos e uma primeira visão de sinais). Os exemplos são em C++, mas as chamadas de sistema são as mesmas.
- *The Linux Programming Interface*, Michael Kerrisk — cap. 20–22 (sinais e handlers; seção 21.1.2 sobre
  funções async-signal-safe), cap. 24–26 (`fork`, término, `waitpid`, zumbis e `SIGCHLD`), cap. 27
  (`exec` e close-on-exec), cap. 34 (grupos de processos e sessões), cap. 44 (pipes) e seção 63.5.2 (*self-pipe trick*).
- Páginas de manual: `waitpid(2)`, `wait(2)` (macros `W*`), `pipe(2)`/`pipe2`, `open(2)` (`O_CLOEXEC`),
  `fopen(3)` (modo `e`), `signal-safety(7)`, `sigaction(2)`, `setpgid(2)`, `kill(2)`, `prctl(2)`
  (`PR_SET_CHILD_SUBREAPER`), `poll(2)`.

## Próximo desafio

**desafio-22 — Mensagens pela metade** (trilha network): de processos que conversam por pipes para
programas que conversam por TCP — framing com prefixo de tamanho, leituras parciais, timeouts e backpressure.
