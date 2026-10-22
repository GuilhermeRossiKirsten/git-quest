/* minisup — um supervisor de processos pequeno.
 *
 *   minisup [-g carencia_ms] [-b backoff_ms] [-l arquivo_de_log] arquivo.conf
 *
 * Inicia cada serviço do arquivo de configuração, repassa a saída deles com o
 * nome como prefixo, reinicia quem termina com falha e, ao receber SIGTERM ou
 * SIGINT, encerra todos de forma ordenada. Veja README.md para o contrato.
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "config.h"
#include "eventlog.h"
#include "supervisor.h"

static struct config cfg;
static struct supervisor sup;
static volatile sig_atomic_t child_exited;

static void on_sigchld(int signo)
{
    (void)signo;
    child_exited = 1;
}

/* SIGTERM/SIGINT: desliga tudo e sai. */
static void on_stop(int signo)
{
    eventlog("sinal %d recebido", signo);
    sup_stop_all(&sup);
    int code = sup_report(&sup);
    eventlog("minisup terminando com código %d", code);
    sup_close(&sup);
    eventlog_close();
    config_free(&cfg);
    exit(code);
}

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sa.sa_handler = on_sigchld;
    if (sigaction(SIGCHLD, &sa, NULL) < 0)
        return -1;
    sa.sa_handler = on_stop;
    if (sigaction(SIGTERM, &sa, NULL) < 0 || sigaction(SIGINT, &sa, NULL) < 0)
        return -1;
    return 0;
}

static int parse_ms(const char *s, const char *what)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < 0 || v > 600000) {
        fprintf(stderr, "minisup: %s inválido: '%s' (0 a 600000 ms)\n", what, s);
        exit(2);
    }
    return (int)v;
}

static void usage(void)
{
    fprintf(stderr, "uso: minisup [-g carencia_ms] [-b backoff_ms] [-l arquivo_de_log] arquivo.conf\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int grace_ms = 2000;
    int backoff_ms = 100;
    const char *log_path = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "g:b:l:")) != -1) {
        switch (opt) {
        case 'g': grace_ms = parse_ms(optarg, "carência"); break;
        case 'b': backoff_ms = parse_ms(optarg, "backoff"); break;
        case 'l': log_path = optarg; break;
        default: usage();
        }
    }
    if (optind != argc - 1)
        usage();

    char err[256];
    if (config_load(argv[optind], &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "minisup: %s\n", err);
        return 2;
    }
    if (eventlog_open(log_path) != 0) {
        fprintf(stderr, "minisup: %s: %s\n", log_path, strerror(errno));
        config_free(&cfg);
        return 2;
    }
    /* Órfãos dos serviços (netos cujo pai morreu) passam a ser nossos filhos,
     * e não do init: assim nenhum processo escapa da supervisão. */
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
        eventlog("prctl(PR_SET_CHILD_SUBREAPER): %s", strerror(errno));

    sup_init(&sup, &cfg, grace_ms, backoff_ms);
    if (setup_signals() != 0) {
        fprintf(stderr, "minisup: sinais: %s\n", strerror(errno));
        return 2;
    }
    eventlog("minisup pid=%d supervisionando %d serviço(s)", (int)getpid(), sup.count);

    for (;;) {
        long long now = now_ms();
        if (child_exited) {
            child_exited = 0;
            sup_reap(&sup, now);
        }
        sup_start_due(&sup, now);
        if (sup_finished(&sup))
            break;

        struct pollfd pfd[MAX_SERVICES];
        struct service *owner[MAX_SERVICES];
        nfds_t n = 0;
        for (int i = 0; i < sup.count; i++) {
            if (sup.svc[i].out_fd >= 0) {
                pfd[n].fd = sup.svc[i].out_fd;
                pfd[n].events = POLLIN;
                owner[n++] = &sup.svc[i];
            }
        }
        int ready = poll(pfd, n, sup_poll_timeout(&sup, now));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            eventlog("poll: %s", strerror(errno));
            break;
        }
        for (nfds_t i = 0; i < n; i++)
            if (pfd[i].revents & (POLLIN | POLLHUP | POLLERR))
                sup_read(owner[i]);
    }

    int code = sup_report(&sup);
    eventlog("minisup terminando com código %d", code);
    sup_close(&sup);
    eventlog_close();
    config_free(&cfg);
    return code;
}
