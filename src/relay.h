/* Repasse da saída dos serviços para a saída padrão do minisup.
 *
 * Cada linha recebida de um serviço vira "<nome> | <linha>" na saída do
 * minisup, escrita com um único write() para não se misturar com as linhas
 * dos outros serviços.
 */
#ifndef MINISUP_RELAY_H
#define MINISUP_RELAY_H

#include <stddef.h>
#include <sys/types.h>

#define LINE_MAX_LEN 4096

struct linebuf {
    size_t len;
    char data[LINE_MAX_LEN];
};

/* Acrescenta `n` bytes lidos do pipe do serviço; emite cada linha completa. */
void relay_feed(struct linebuf *lb, const char *name, const char *buf, size_t n);
/* Emite o que sobrou sem '\n' no fim (chamada quando o pipe chega ao fim). */
void relay_flush(struct linebuf *lb, const char *name);
/* printf para a saída padrão, sem o buffer do stdio. */
void relay_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* write() que insiste até escrever tudo (escritas parciais e EINTR). */
int write_all(int fd, const void *buf, size_t n);

#endif
