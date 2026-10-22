/* Arquivo de configuração do minisup.
 *
 * Uma linha por serviço:   <nome> <max_reinicios> <comando> [argumentos...]
 * Linhas vazias e linhas que começam com '#' são ignoradas. Não há aspas nem
 * escapes: os argumentos são separados por espaços ou tabs.
 */
#ifndef MINISUP_CONFIG_H
#define MINISUP_CONFIG_H

#include <stddef.h>

#define MAX_SERVICES 64
#define MAX_ARGS 32
#define MAX_NAME 32

struct service_spec {
    char name[MAX_NAME];
    int max_restarts;
    char *argv[MAX_ARGS + 1]; /* terminado em NULL, pronto para execvp */
};

struct config {
    struct service_spec services[MAX_SERVICES];
    int count;
};

/* Lê `path`. Em caso de erro devolve -1 e descreve o problema em `err`. */
int config_load(const char *path, struct config *cfg, char *err, size_t errlen);
void config_free(struct config *cfg);

#endif
