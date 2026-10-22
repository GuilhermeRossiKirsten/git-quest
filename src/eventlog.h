/* Registro de eventos do supervisor (inícios, términos, reinícios, desligamento).
 * Vai para o arquivo passado em -l ou, sem -l, para a saída de erro. */
#ifndef MINISUP_EVENTLOG_H
#define MINISUP_EVENTLOG_H

int eventlog_open(const char *path);
void eventlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void eventlog_close(void);

#endif
