# Dicas — desafio-21

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelos sintomas, um de cada vez, com `./start.sh` (a demonstração mostra `ps` e
`/proc/<pid>/fd`) e `GQ_VERBOSE=1 ./test.sh`.

- Um zumbi é um processo que já terminou mas cujo pai ainda não "perguntou" como ele terminou.
  **Quem** é o pai dos zumbis que você vê? Que filhos o `minisup` pergunta e quais ele nunca pergunta?
- Um leitor de pipe só recebe EOF quando **todas** as cópias da ponta de escrita estão fechadas,
  em **todos** os processos. Quantas cópias existem depois de um `fork`?
- Um descritor aberto sobrevive ao `exec`, a menos que alguém diga o contrário.
- Quanto vale `WEXITSTATUS(status)` quando o processo morreu por um sinal?
- O que o `minisup` faz **dentro** do handler de `SIGTERM`? Enquanto o handler roda, quem lê os pipes?

## Nível 2

Onde olhar:

- `sup_reap` em `src/supervisor.c`: o `minisup` é *subreaper* (`prctl` em `main`), então os netos órfãos
  também viram filhos dele. E um único `SIGCHLD` pode representar vários filhos (sinais não se acumulam).
- `spawn` em `src/supervisor.c`: siga `fds[0]` e `fds[1]` no pai e no filho. Quem guarda cada ponta, e por quanto tempo?
- `pipe` vs. `pipe2`, e o modo do `fopen` em `src/eventlog.c` (`man 3 fopen`, procure por `'e'`).
- `on_exit_status`: as macros `WIFEXITED`, `WIFSIGNALED`, `WTERMSIG` (`man 2 waitpid`).
- `on_stop` em `src/minisup.c` e `sup_stop_all`: compare com a lista de `man 7 signal-safety`. Note que
  `waitpid(pid, ..., 0)` sem prazo espera para sempre por quem ignora `SIGTERM`.
- `kill(s->pid, SIGTERM)` atinge um processo. Quem mais está no grupo de processos do serviço? (`man 2 setpgid`, `man 2 kill`)

## Nível 3

Abordagem (sem código pronto):

1. **Colheita**: troque o laço "um `waitpid` por serviço" por um laço `waitpid(-1, &st, WNOHANG)` que só
   para quando não houver mais nada a colher (retorno 0 ou `ECHILD`; repita em `EINTR`). Para cada PID
   devolvido, ache o serviço dono; PIDs desconhecidos são órfãos adotados — basta colhê-los.
2. **Pipes**: crie com `pipe2(fds, O_CLOEXEC)`. No pai, feche a ponta de escrita logo depois do `fork`.
   No filho, `dup2` para 1 e 2 (o `dup2` cria cópias **sem** close-on-exec; as originais somem no `exec`).
   Abra o log com `fopen(path, "ae")`.
3. **Status**: decodifique com `WIFEXITED`/`WEXITSTATUS` e `WIFSIGNALED`/`WTERMSIG`; guarde se foi sinal.
   O resumo e o código final (`128 + sinal`) já sabem usar esse campo.
4. **Sinais**: o handler só anota o sinal numa variável `volatile sig_atomic_t` e escreve 1 byte num
   *self-pipe* não bloqueante (preserve `errno`). O laço principal coloca a ponta de leitura no `poll`,
   e, ao acordar, colhe filhos (`SIGCHLD`) ou inicia o desligamento (`SIGTERM`/`SIGINT`). `poll` pode
   devolver `EINTR`: apenas volte ao laço.
5. **Desligamento como estado do laço**: ao iniciar, mande `SIGTERM` para o **grupo** de cada serviço
   (`kill(-pgid, SIGTERM)`; o filho chama `setpgid(0, 0)` antes do `exec` e o pai repete `setpgid(pid, pid)`)
   e marque um prazo `agora + carência`. Continue lendo os pipes e colhendo filhos normalmente. Vencido o
   prazo, `SIGKILL` para os grupos que ainda tiverem alguém (`kill(-pgid, 0)` diz se o grupo existe). Saia
   quando todos tiverem sido colhidos e os pipes chegarem a EOF (com um prazo curto para a drenagem).
