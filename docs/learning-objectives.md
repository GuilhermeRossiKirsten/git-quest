# Objetivos de aprendizagem — desafio-21

Ao concluir este desafio você será capaz de:

1. **Colher processos corretamente**: usar `waitpid(-1, ..., WNOHANG)` em laço depois de `SIGCHLD`,
   sabendo que sinais iguais não se acumulam e que um *subreaper* (`PR_SET_CHILD_SUBREAPER`) também
   herda os órfãos dos seus descendentes — e por isso precisa colhê-los.
2. **Raciocinar sobre o ciclo de vida de pipes**: saber que o EOF só chega quando todas as cópias da ponta
   de escrita estão fechadas, em todos os processos, e fechar cada ponta no lugar certo depois do `fork`.
3. **Controlar a herança de descritores** através do `exec` com `O_CLOEXEC` (`pipe2`, `open`, `fopen` com `"e"`)
   e entender por que `dup2` produz um descritor herdável.
4. **Decodificar o status de término** com `WIFEXITED`/`WEXITSTATUS` e `WIFSIGNALED`/`WTERMSIG` e
   transformá-lo na convenção de código de saída do shell (`128 + sinal`).
5. **Tratar sinais com segurança**: handlers mínimos e async-signal-safe (self-pipe + `sig_atomic_t`),
   trabalho real no laço de eventos, `EINTR` tratado em `poll`/`read`/`waitpid`.
6. **Desligar uma árvore de processos**: grupos de processos (`setpgid`, `kill(-pgid, ...)`), carência
   com prazo, escalonamento para `SIGKILL` e drenagem da saída antes de sair.

## Como os testes verificam cada objetivo

O `minisup` é compilado com `-std=c17 -Wall -Wextra -Wpedantic -Werror` e ASan + UBSan; cada cenário
sobe um `minisup` novo, com serviços de mentira (`tests/helper.c`), marcados por uma variável de
ambiente que permite encontrá-los em `/proc` mesmo depois que o `minisup` sai.

| Objetivo | Verificação |
|---|---|
| 1. Colher processos | Cenário `zumbis`: um serviço cria um neto e sai; o neto informa quem o adotou (tem de ser o `minisup`) e termina; 1,2 s depois não pode haver processo em estado `Z` com `PPID` = `minisup`. Cenário `rajada`: 30 serviços saem no mesmo milissegundo; o `minisup` tem de colher todos e sair sozinho com código 0. |
| 2. Ciclo de vida de pipes | Cenário `saida`: um serviço escreve 20 000 linhas e um resto sem `\n`, outro sai na hora; as linhas chegam completas, em ordem e prefixadas, e o `minisup` sai sozinho com 0 (se alguma ponta de escrita ficar aberta, o EOF nunca chega e ele não sai). |
| 3. Herança de descritores | Cenário `fds`: um serviço iniciado entre dois outros lista `/proc/self/fd`; só podem existir 0, 1 e 2, com 1 e 2 no mesmo pipe. O motivo da falha mostra para onde aponta cada descritor vazado. |
| 4. Status de término | Cenários `status-codigo` (sai com 3, um reinício: roda 2 vezes, resumo "saiu com código 3", `minisup` sai com 3) e `status-sinal` (morre por `SIGKILL`, dois reinícios: roda 3 vezes, resumo "morto pelo sinal 9 (SIGKILL)", `minisup` sai com 137). |
| 5. Sinais com segurança | Cenários `desligamento` e `sigint`: depois de `SIGTERM`/`SIGINT`, as linhas que um serviço escreve ao se despedir precisam aparecer, o serviço morto pelo `SIGTERM` aparece como "morto pelo sinal 15" e o código é 0. Todos os cenários rodam com ASan/UBSan e falham se houver relatório. |
| 6. Desligar a árvore | Cenário `prazo`: um serviço ignora `SIGTERM`; com `-g 400`, o `minisup` precisa sair depois de 0,35 s e antes de 8 s, com o serviço "morto pelo sinal 9" e código 137. Cenário `netos`: um serviço cria um neto que larga a saída; 2 s depois que o `minisup` sai, nenhum processo do cenário pode estar vivo. |
