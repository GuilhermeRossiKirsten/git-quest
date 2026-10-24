/* Lê um pacote TLV de stdin, valida e imprime o relatório.
 *
 *   tlvparse < pacote.bin
 *
 * Saída: 0 se o pacote foi aceito; 2 se foi rejeitado. */
#include "tlv.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_INPUT ((size_t)1 << 20)   /* 1 MiB: nenhum sensor manda mais que isso */
#define CHUNK     ((size_t)4096)

int main(void)
{
    unsigned char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int rc;

    for (;;) {
        size_t got;

        if (len == cap) {
            unsigned char *bigger;
            if (cap == MAX_INPUT) {
                if (fgetc(stdin) != EOF) {
                    fprintf(stderr, "tlvparse: entrada maior que %zu bytes\n", MAX_INPUT);
                    free(buf);
                    return TLV_REJECT;
                }
                break;
            }
            cap = cap == 0u ? CHUNK : cap * 2u;
            if (cap > MAX_INPUT) {
                cap = MAX_INPUT;
            }
            bigger = realloc(buf, cap);
            if (bigger == NULL) {
                fprintf(stderr, "tlvparse: sem memoria\n");
                free(buf);
                return TLV_REJECT;
            }
            buf = bigger;
        }
        got = fread(buf + len, 1, cap - len, stdin);
        len += got;
        if (got == 0u) {
            if (ferror(stdin)) {
                fprintf(stderr, "tlvparse: erro de leitura\n");
                free(buf);
                return TLV_REJECT;
            }
            break;  /* EOF */
        }
    }

    /* Devolve a sobra: o pacote fica num bloco do tamanho exato. */
    if (len > 0u) {
        unsigned char *exact = realloc(buf, len);
        if (exact != NULL) {
            buf = exact;
        }
    }

    rc = tlv_process(buf, len, stdout, stderr);
    free(buf);
    return rc;
}
