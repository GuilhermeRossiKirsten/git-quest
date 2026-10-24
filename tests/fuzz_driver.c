/* Driver de fuzzing do avaliador: chama tlv_process() no mesmo processo para
 * cada pacote de um arquivo de corpus, como um harness estilo libFuzzer.
 *
 *   fuzz-driver <corpus>
 *
 * Corpus: sequência de [uint32 little-endian tamanho][bytes do pacote].
 * Para cada pacote i, escreve em stdout:
 *   "<i> <rc> <bytes em out> <bytes em err>\n" seguido dos bytes de out.
 * Antes de processar o pacote i, escreve "caso <i>\n" em stderr, para que o
 * avaliador saiba qual entrada estava em curso se o processo abortar.
 * Cada pacote é copiado para um bloco do tamanho exato (o ASan enxerga
 * qualquer leitura além do fim). */
#include "../src/tlv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_exact(FILE *f, void *dst, size_t n)
{
    return fread(dst, 1, n, f) == n;
}

int main(int argc, char **argv)
{
    FILE *corpus;
    unsigned long idx = 0;

    if (argc != 2) {
        fprintf(stderr, "uso: %s <corpus>\n", argv[0]);
        return 64;
    }
    corpus = fopen(argv[1], "rb");
    if (corpus == NULL) {
        perror(argv[1]);
        return 64;
    }
    for (;;) {
        unsigned char hdr[4];
        size_t len;
        unsigned char *pkt;
        char *obuf = NULL, *ebuf = NULL;
        size_t olen = 0, elen = 0;
        FILE *out, *err;
        int rc;

        if (!read_exact(corpus, hdr, sizeof hdr)) {
            break;
        }
        len = (size_t)hdr[0] | (size_t)hdr[1] << 8 | (size_t)hdr[2] << 16 | (size_t)hdr[3] << 24;
        pkt = malloc(len > 0 ? len : 1);
        if (pkt == NULL || !read_exact(corpus, pkt, len)) {
            fprintf(stderr, "corpus corrompido no caso %lu\n", idx);
            return 64;
        }
        out = open_memstream(&obuf, &olen);
        err = open_memstream(&ebuf, &elen);
        if (out == NULL || err == NULL) {
            perror("open_memstream");
            return 64;
        }
        fprintf(stderr, "caso %lu\n", idx);
        fflush(stderr);
        rc = tlv_process(pkt, len, out, err);
        fclose(out);
        fclose(err);
        printf("%lu %d %zu %zu\n", idx, rc, olen, elen);
        fwrite(obuf, 1, olen, stdout);
        free(obuf);
        free(ebuf);
        free(pkt);
        idx++;
    }
    fclose(corpus);
    fflush(stdout);
    fprintf(stderr, "fim %lu\n", idx);
    return 0;
}
