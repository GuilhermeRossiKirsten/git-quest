#include "supervisor.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "eventlog.h"

#define MAX_BACKOFF_MS 5000
#define POLL_MAX_MS 1000

long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(long ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

void sup_init(struct supervisor *sup, const struct config *cfg, int grace_ms, int backoff_ms)
{
    memset(sup, 0, sizeof *sup);
    sup->count = cfg->count;
    sup->grace_ms = grace_ms;
    sup->backoff_ms = backoff_ms;
    for (int i = 0; i < cfg->count; i++) {
        sup->svc[i].spec = &cfg->services[i];
        sup->svc[i].state = SVC_WAITING;
        sup->svc[i].out_fd = -1;
        sup->svc[i].pipe_wr = -1;
    }
}

static void describe(const struct service *s, char *buf, size_t len)
{
    if (s->by_signal) {
        const char *abbr = sigabbrev_np(s->code);
        snprintf(buf, len, "morto pelo sinal %d (SIG%s)", s->code, abbr ? abbr : "?");
    } else {
        snprintf(buf, len, "saiu com código %d", s->code);
    }
}

static void close_output(struct service *s)
{
    if (s->out_fd < 0)
        return;
    relay_flush(&s->line, s->spec->name);
    close(s->out_fd);
    s->out_fd = -1;
}

static void spawn(struct service *s)
{
    /* Pipe da execução anterior: o que tinha para ler já foi lido. */
    close_output(s);
    if (s->pipe_wr >= 0) {
        close(s->pipe_wr);
        s->pipe_wr = -1;
    }

    int fds[2];
    if (pipe(fds) < 0) {
        eventlog("%s: pipe: %s", s->spec->name, strerror(errno));
        goto failed;
    }
    pid_t pid = fork();
    if (pid < 0) {
        eventlog("%s: fork: %s", s->spec->name, strerror(errno));
        close(fds[0]);
        close(fds[1]);
        goto failed;
    }
    if (pid == 0) {
        /* Filho: stdout e stderr vão para o pipe do supervisor. */
        if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0)
            _exit(127);
        close(fds[1]);
        execvp(s->spec->argv[0], s->spec->argv);
        dprintf(STDERR_FILENO, "minisup: não foi possível executar '%s': %s\n",
                s->spec->argv[0], strerror(errno));
        _exit(127);
    }

    s->pid = pid;
    s->out_fd = fds[0];
    s->pipe_wr = fds[1];
    s->line.len = 0;
    s->state = SVC_RUNNING;
    s->has_result = false;
    eventlog("%s: iniciado pid=%d (execução %d)", s->spec->name, (int)pid, s->restarts + 1);
    return;

failed:
    s->state = SVC_DONE;
    s->has_result = true;
    s->by_signal = false;
    s->code = 127;
    s->clean = false;
}

void sup_start_due(struct supervisor *sup, long long now)
{
    if (sup->shutting_down)
        return;
    for (int i = 0; i < sup->count; i++) {
        struct service *s = &sup->svc[i];
        if (s->state == SVC_WAITING && now >= s->restart_at)
            spawn(s);
    }
}

static void on_exit_status(struct supervisor *sup, struct service *s, int status, long long now)
{
    s->pid = 0;
    s->has_result = true;
    s->code = WEXITSTATUS(status);
    s->clean = s->code == 0;

    char how[64];
    describe(s, how, sizeof how);
    if (!s->clean && !sup->shutting_down && s->restarts < s->spec->max_restarts) {
        long long delay = (long long)sup->backoff_ms << s->restarts;
        if (delay > MAX_BACKOFF_MS)
            delay = MAX_BACKOFF_MS;
        s->restarts++;
        s->state = SVC_WAITING;
        s->restart_at = now + delay;
        eventlog("%s: %s; reinício %d/%d em %lld ms", s->spec->name, how, s->restarts,
                 s->spec->max_restarts, delay);
    } else {
        s->state = SVC_DONE;
        eventlog("%s: %s; não será reiniciado", s->spec->name, how);
    }
}

void sup_reap(struct supervisor *sup, long long now)
{
    for (int i = 0; i < sup->count; i++) {
        struct service *s = &sup->svc[i];
        if (s->state != SVC_RUNNING)
            continue;
        int status;
        if (waitpid(s->pid, &status, WNOHANG) == s->pid)
            on_exit_status(sup, s, status, now);
    }
}

void sup_read(struct service *s)
{
    char buf[4096];
    ssize_t n = read(s->out_fd, buf, sizeof buf);
    if (n > 0) {
        relay_feed(&s->line, s->spec->name, buf, (size_t)n);
    } else if (n == 0) {
        close_output(s);
    } else if (errno != EINTR && errno != EAGAIN) {
        eventlog("%s: read: %s", s->spec->name, strerror(errno));
        close_output(s);
    }
}

void sup_stop_all(struct supervisor *sup)
{
    sup->shutting_down = true;
    eventlog("desligando (carência de %d ms)", sup->grace_ms);
    for (int i = 0; i < sup->count; i++) {
        struct service *s = &sup->svc[i];
        if (s->state == SVC_RUNNING)
            kill(s->pid, SIGTERM);
        else if (s->state == SVC_WAITING)
            s->state = SVC_DONE; /* reinício pendente cancelado */
    }
    sleep_ms(sup->grace_ms);
    for (int i = 0; i < sup->count; i++) {
        struct service *s = &sup->svc[i];
        if (s->state != SVC_RUNNING)
            continue;
        int status;
        if (waitpid(s->pid, &status, 0) == s->pid)
            on_exit_status(sup, s, status, now_ms());
    }
}

bool sup_finished(const struct supervisor *sup)
{
    for (int i = 0; i < sup->count; i++)
        if (sup->svc[i].state != SVC_DONE || sup->svc[i].out_fd >= 0)
            return false;
    return true;
}

int sup_poll_timeout(const struct supervisor *sup, long long now)
{
    /* Acorda pelo menos uma vez por segundo para conferir os filhos. */
    long long wait = POLL_MAX_MS;
    for (int i = 0; i < sup->count; i++) {
        const struct service *s = &sup->svc[i];
        if (s->state == SVC_WAITING && s->restart_at - now < wait)
            wait = s->restart_at - now;
    }
    return wait < 0 ? 0 : (int)wait;
}

int sup_report(const struct supervisor *sup)
{
    int exit_code = 0;
    for (int i = 0; i < sup->count; i++) {
        const struct service *s = &sup->svc[i];
        if (!s->has_result) {
            relay_printf("minisup: %s: não chegou a executar\n", s->spec->name);
            continue;
        }
        char how[64];
        describe(s, how, sizeof how);
        relay_printf("minisup: %s: %s, reinícios: %d\n", s->spec->name, how, s->restarts);
        if (!s->clean && exit_code == 0)
            exit_code = s->by_signal ? 128 + s->code : s->code;
    }
    return exit_code;
}

void sup_close(struct supervisor *sup)
{
    for (int i = 0; i < sup->count; i++) {
        close_output(&sup->svc[i]);
        if (sup->svc[i].pipe_wr >= 0)
            close(sup->svc[i].pipe_wr);
        sup->svc[i].pipe_wr = -1;
    }
}
