#include "relay.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static void emit(const char *name, const char *data, size_t len)
{
    char out[MAX_NAME + 3 + LINE_MAX_LEN + 1];
    size_t nlen = strlen(name);
    memcpy(out, name, nlen);
    memcpy(out + nlen, " | ", 3);
    memcpy(out + nlen + 3, data, len);
    out[nlen + 3 + len] = '\n';
    (void)write_all(STDOUT_FILENO, out, nlen + 3 + len + 1);
}

void relay_feed(struct linebuf *lb, const char *name, const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            emit(name, lb->data, lb->len);
            lb->len = 0;
            continue;
        }
        if (lb->len == sizeof lb->data) { /* linha longa demais: quebra em pedaços */
            emit(name, lb->data, lb->len);
            lb->len = 0;
        }
        lb->data[lb->len++] = buf[i];
    }
}

void relay_flush(struct linebuf *lb, const char *name)
{
    if (lb->len > 0)
        emit(name, lb->data, lb->len);
    lb->len = 0;
}

void relay_printf(const char *fmt, ...)
{
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, sizeof out, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof out)
        n = (int)sizeof out - 1;
    (void)write_all(STDOUT_FILENO, out, (size_t)n);
}
