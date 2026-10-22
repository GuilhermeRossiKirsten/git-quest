/* Estado e ciclo de vida dos serviços supervisionados. */
#ifndef MINISUP_SUPERVISOR_H
#define MINISUP_SUPERVISOR_H

#include <stdbool.h>
#include <sys/types.h>

#include "config.h"
#include "relay.h"

enum svc_state {
    SVC_WAITING, /* aguardando o (re)início em restart_at */
    SVC_RUNNING, /* processo vivo */
    SVC_DONE,    /* terminou e não será reiniciado */
};

struct service {
    const struct service_spec *spec;
    enum svc_state state;
    pid_t pid;            /* processo atual; 0 quando não há */
    int out_fd;           /* extremidade de leitura do pipe stdout+stderr; -1 = fechado */
    int pipe_wr;          /* extremidade de escrita (fechada no próximo reinício) */
    struct linebuf line;  /* linha parcial ainda sem '\n' */
    int restarts;
    long long restart_at; /* ms (relógio monotônico) */
    /* Como a última execução terminou. */
    bool has_result;
    bool by_signal;
    int code;             /* código de saída ou número do sinal */
    bool clean;
};

struct supervisor {
    struct service svc[MAX_SERVICES];
    int count;
    int grace_ms;
    int backoff_ms;
    bool shutting_down;
};

long long now_ms(void);

void sup_init(struct supervisor *sup, const struct config *cfg, int grace_ms, int backoff_ms);
/* Inicia os serviços cujo (re)início venceu. */
void sup_start_due(struct supervisor *sup, long long now);
/* Colhe os serviços que terminaram (chamada depois de SIGCHLD). */
void sup_reap(struct supervisor *sup, long long now);
/* Lê uma vez do pipe do serviço e repassa as linhas completas. */
void sup_read(struct service *s);
/* Desligamento: SIGTERM para todos, carência e espera cada um terminar. */
void sup_stop_all(struct supervisor *sup);
bool sup_finished(const struct supervisor *sup);
/* Quanto tempo o laço principal pode dormir em poll(). */
int sup_poll_timeout(const struct supervisor *sup, long long now);
/* Imprime o resumo final e devolve o código de saída do minisup. */
int sup_report(const struct supervisor *sup);
void sup_close(struct supervisor *sup);

#endif
