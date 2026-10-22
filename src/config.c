#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_name(const char *s)
{
    if (*s == '\0' || strlen(s) >= MAX_NAME)
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
            return 0;
    return 1;
}

static int parse_line(char *line, struct service_spec *spec, char *err, size_t errlen)
{
    char *save = NULL;
    char *name = strtok_r(line, " \t\r\n", &save);
    char *restarts = strtok_r(NULL, " \t\r\n", &save);
    if (restarts == NULL) {
        snprintf(err, errlen, "esperado '<nome> <max_reinicios> <comando>...'");
        return -1;
    }
    if (!valid_name(name)) {
        snprintf(err, errlen, "nome de serviço inválido '%s' (use letras, dígitos, '-' e '_')", name);
        return -1;
    }
    char *end = NULL;
    errno = 0;
    long n = strtol(restarts, &end, 10);
    if (errno != 0 || *end != '\0' || n < 0 || n > 1000) {
        snprintf(err, errlen, "max_reinicios inválido '%s' (0 a 1000)", restarts);
        return -1;
    }
    memset(spec, 0, sizeof *spec);
    snprintf(spec->name, sizeof spec->name, "%s", name);
    spec->max_restarts = (int)n;

    int argc = 0;
    for (char *tok; (tok = strtok_r(NULL, " \t\r\n", &save)) != NULL;) {
        if (argc == MAX_ARGS) {
            snprintf(err, errlen, "argumentos demais (máximo %d)", MAX_ARGS);
            return -1;
        }
        spec->argv[argc] = strdup(tok);
        if (spec->argv[argc] == NULL) {
            snprintf(err, errlen, "sem memória");
            return -1;
        }
        argc++;
    }
    if (argc == 0) {
        snprintf(err, errlen, "serviço '%s' sem comando", spec->name);
        return -1;
    }
    return 0;
}

int config_load(const char *path, struct config *cfg, char *err, size_t errlen)
{
    memset(cfg, 0, sizeof *cfg);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        snprintf(err, errlen, "%s: %s", path, strerror(errno));
        return -1;
    }
    char *line = NULL;
    size_t cap = 0;
    int lineno = 0;
    int rc = 0;
    while (getline(&line, &cap, f) != -1) {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        if (cfg->count == MAX_SERVICES) {
            snprintf(err, errlen, "%s:%d: serviços demais (máximo %d)", path, lineno, MAX_SERVICES);
            rc = -1;
            break;
        }
        char msg[160];
        struct service_spec *spec = &cfg->services[cfg->count];
        if (parse_line(p, spec, msg, sizeof msg) != 0) {
            snprintf(err, errlen, "%s:%d: %s", path, lineno, msg);
            cfg->count++; /* libera o que já foi alocado para esta linha */
            rc = -1;
            break;
        }
        for (int i = 0; i < cfg->count; i++) {
            if (strcmp(cfg->services[i].name, spec->name) == 0) {
                snprintf(err, errlen, "%s:%d: serviço '%s' repetido", path, lineno, spec->name);
                rc = -1;
            }
        }
        cfg->count++;
        if (rc != 0)
            break;
    }
    free(line);
    fclose(f);
    if (rc == 0 && cfg->count == 0) {
        snprintf(err, errlen, "%s: nenhum serviço configurado", path);
        rc = -1;
    }
    if (rc != 0)
        config_free(cfg);
    return rc;
}

void config_free(struct config *cfg)
{
    for (int i = 0; i < cfg->count; i++)
        for (int j = 0; cfg->services[i].argv[j] != NULL; j++)
            free(cfg->services[i].argv[j]);
    cfg->count = 0;
}
