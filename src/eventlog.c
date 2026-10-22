#include "eventlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static FILE *log_file;
static struct timespec t0;

int eventlog_open(const char *path)
{
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (path == NULL) {
        log_file = stderr;
        return 0;
    }
    log_file = fopen(path, "a");
    if (log_file == NULL)
        return -1;
    setvbuf(log_file, NULL, _IOLBF, 0);
    return 0;
}

void eventlog(const char *fmt, ...)
{
    if (log_file == NULL)
        return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (double)(now.tv_sec - t0.tv_sec) + (double)(now.tv_nsec - t0.tv_nsec) / 1e9;
    fprintf(log_file, "[%8.3f] ", elapsed);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
}

void eventlog_close(void)
{
    if (log_file != NULL && log_file != stderr)
        fclose(log_file);
    log_file = NULL;
}
