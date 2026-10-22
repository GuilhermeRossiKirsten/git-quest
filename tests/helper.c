/* gq-helper — serviços de mentira usados pelos testes e pela demonstração.
 *
 *   gq-helper lines N [resto]   imprime N linhas numeradas e, opcionalmente, um
 *                               resto SEM '\n' no fim; sai com 0
 *   gq-helper exit CODIGO [MS]  imprime "start", espera MS ms e sai com CODIGO
 *   gq-helper exit-at EPOCA_MS CODIGO
 *                               imprime "start" e sai no instante EPOCA_MS
 *                               (relógio de parede), junto com os irmãos
 *   gq-helper crash SINAL       imprime "start" e morre pelo SINAL
 *   gq-helper forker MS         cria um neto e sai na hora; o neto espera ser
 *                               adotado, conta quem o adotou, vive MS ms e sai
 *   gq-helper fds               lista os próprios descritores abertos e sai
 *   gq-helper sleep             imprime "ready" e dorme até receber um sinal
 *   gq-helper polite            como sleep, mas no SIGTERM se despede e sai com 0
 *   gq-helper stubborn          ignora SIGTERM e dorme para sempre
 *   gq-helper family            cria um neto (que também dorme) e dorme
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(long ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static long long wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void sleep_forever(void)
{
    for (;;)
        pause();
}

static volatile sig_atomic_t got_term;
static void on_term(int sig)
{
    (void)sig;
    got_term = 1;
}

static int cmd_lines(int argc, char **argv)
{
    long n = argc > 2 ? atol(argv[2]) : 10;
    for (long i = 1; i <= n; i++)
        printf("linha %06ld\n", i);
    if (argc > 3)
        fputs(argv[3], stdout);
    return fflush(stdout) == 0 ? 0 : 1;
}

static int cmd_forker(int argc, char **argv)
{
    long ms = argc > 2 ? atol(argv[2]) : 300;
    pid_t parent = getpid();
    say("start pid=%d", (int)parent);
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        prctl(PR_SET_NAME, "gq-neto");
        /* Espera o pai morrer para saber quem nos adotou. */
        for (int i = 0; i < 400 && getppid() == parent; i++)
            sleep_ms(5);
        say("neto pid=%d adotado por %d", (int)getpid(), (int)getppid());
        sleep_ms(ms);
        _exit(0);
    }
    return 0;
}

static int cmd_fds(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (d == NULL)
        return 1;
    int self = dirfd(d);
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.')
            continue;
        int fd = atoi(e->d_name);
        if (fd == self)
            continue;
        char path[64], target[256];
        snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
        ssize_t n = readlink(path, target, sizeof target - 1);
        target[n < 0 ? 0 : n] = '\0';
        printf("fd %d -> %s\n", fd, target);
    }
    closedir(d);
    say("fds: fim");
    return 0;
}

static int cmd_polite(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    say("ready");
    while (!got_term)
        pause();
    for (int i = 1; i <= 3; i++) {
        say("tchau %d", i);
        sleep_ms(30);
    }
    return 0;
}

static int cmd_family(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        prctl(PR_SET_NAME, "gq-neto");
        /* Como um daemon descuidado: larga a saída, mas fica no mesmo grupo. */
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0);
        dup2(null, 1);
        dup2(null, 2);
        close(null);
        sleep_forever();
    }
    say("ready neto=%d", (int)pid);
    sleep_forever();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: gq-helper <modo> [args]\n");
        return 2;
    }
    const char *mode = argv[1];
    if (strcmp(mode, "lines") == 0)
        return cmd_lines(argc, argv);
    if (strcmp(mode, "exit") == 0) {
        say("start pid=%d", (int)getpid());
        if (argc > 3)
            sleep_ms(atol(argv[3]));
        return argc > 2 ? atoi(argv[2]) : 0;
    }
    if (strcmp(mode, "exit-at") == 0 && argc > 3) {
        say("start pid=%d", (int)getpid());
        long long left = atoll(argv[2]) - wall_ms();
        if (left > 0)
            sleep_ms((long)left);
        return atoi(argv[3]);
    }
    if (strcmp(mode, "crash") == 0 && argc > 2) {
        int sig = atoi(argv[2]);
        say("start pid=%d", (int)getpid());
        signal(sig, SIG_DFL);
        raise(sig);
        return 1;
    }
    if (strcmp(mode, "forker") == 0)
        return cmd_forker(argc, argv);
    if (strcmp(mode, "fds") == 0)
        return cmd_fds();
    if (strcmp(mode, "sleep") == 0) {
        say("ready");
        sleep_forever();
    }
    if (strcmp(mode, "polite") == 0)
        return cmd_polite();
    if (strcmp(mode, "stubborn") == 0) {
        signal(SIGTERM, SIG_IGN);
        say("ready");
        sleep_forever();
    }
    if (strcmp(mode, "family") == 0)
        return cmd_family();
    fprintf(stderr, "gq-helper: modo desconhecido '%s'\n", mode);
    return 2;
}
