/* tlvparse — decodificação dos registros e formatação do relatório.
 *
 * Código "de campo": roda há anos no coletor que recebe os pacotes dos
 * sensores e "nunca deu problema" com os pacotes que os sensores mandam.
 */
#include "tlv.h"

#include <string.h>
#include <stdlib.h>

#define T_NAME    0x01u
#define T_READING 0x02u
#define T_LABEL   0x03u
#define T_SERIES  0x04u

#define HEADER_LEN    7u
#define REC_HDR_LEN   3u    /* type (1) + length (2) */
#define NAME_CAP      32u   /* 31 caracteres + terminador */
#define LABEL_CAP     64u   /* 63 caracteres + terminador */
#define MAX_READINGS  1024u
#define MAX_LABELS    64u
#define MAX_SERIES    64u

struct series {
    int16_t *data;
    size_t   n;
};

struct report {
    char         *name;                 /* alocado com strdup */
    char         *labels[MAX_LABELS];   /* blocos de LABEL_CAP bytes */
    size_t        nlabel;
    int32_t       readings[MAX_READINGS];
    size_t        nread;
    int32_t       sum;
    struct series series[MAX_SERIES];
    size_t        nseries;
};

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int is_printable(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] < 0x20u || p[i] > 0x7eu) {
            return 0;
        }
    }
    return 1;
}

static int reject(FILE *err, const char *msg)
{
    fprintf(err, "tlvparse: %s\n", msg);
    return TLV_REJECT;
}

static void report_free(struct report *r)
{
    free(r->name);
    for (size_t i = 0; i < r->nlabel; i++) {
        free(r->labels[i]);
    }
    for (size_t i = 0; i < r->nseries; i++) {
        free(r->series[i].data);
    }
}

static int do_name(struct report *r, const unsigned char *val, size_t vlen,
                   FILE *err)
{
    char buf[NAME_CAP];

    if (r->name != NULL) {
        return reject(err, "mais de um registro NAME");
    }
    if (vlen < 1u) {
        return reject(err, "NAME vazio");
    }
    if (!is_printable(val, vlen)) {
        return reject(err, "NAME nao imprimivel");
    }
    /* os sensores mandam nomes curtos */
    memcpy(buf, val, vlen);
    buf[vlen] = '\0';
    r->name = strdup(buf);
    if (r->name == NULL) {
        return reject(err, "sem memoria");
    }
    return TLV_OK;
}

static int label_push(struct report *r, char *text)
{
    if (r->nlabel >= MAX_LABELS) {
        return -1;
    }
    r->labels[r->nlabel++] = text;
    return 0;
}

static int do_label(struct report *r, const unsigned char *val, size_t vlen,
                    FILE *err)
{
    char *text;

    if (vlen < 1u || vlen > LABEL_CAP) {
        return reject(err, "LABEL com tamanho invalido");
    }
    if (!is_printable(val, vlen)) {
        return reject(err, "LABEL nao imprimivel");
    }
    text = malloc(LABEL_CAP);
    if (text == NULL) {
        return reject(err, "sem memoria");
    }
    memcpy(text, val, vlen);
    text[vlen] = '\0';
    label_push(r, text);
    return TLV_OK;
}

static int do_reading(struct report *r, const unsigned char *val, size_t vlen,
                      FILE *err)
{
    uint32_t u;
    int32_t v;

    if (vlen != 4u) {
        return reject(err, "READING precisa de 4 bytes");
    }
    if (r->nread >= MAX_READINGS) {
        return reject(err, "leituras demais");
    }
    u = be32(val);
    memcpy(&v, &u, sizeof v);
    r->readings[r->nread++] = v;
    r->sum += v;
    return TLV_OK;
}

static int do_series(struct report *r, const unsigned char *val, size_t vlen,
                     FILE *err)
{
    uint32_t count;
    uint32_t bytes;
    int16_t *data;

    if (r->nseries >= MAX_SERIES) {
        return reject(err, "series demais");
    }
    if (vlen < 4u) {
        return reject(err, "SERIES sem cabecalho de contagem");
    }
    count = be32(val);
    bytes = count * 2u;                 /* 2 bytes por amostra */
    if (vlen != 4u + bytes) {
        return reject(err, "SERIES com contagem inconsistente");
    }
    data = malloc(bytes);
    if (data == NULL) {
        return reject(err, "sem memoria");
    }
    for (uint32_t i = 0; i < count; i++) {
        uint16_t u = be16(val + 4u + (size_t)i * 2u);
        int16_t  s;
        memcpy(&s, &u, sizeof s);
        data[i] = s;
    }
    r->series[r->nseries].data = data;
    r->series[r->nseries].n = count;
    r->nseries++;
    return TLV_OK;
}

static void print_report(const struct report *r, FILE *out)
{
    fprintf(out, "sensor %s\n", r->name);
    for (size_t i = 0; i < r->nlabel; i++) {
        fprintf(out, "label %s\n", r->labels[i]);
    }
    for (size_t i = 0; i < r->nread; i++) {
        fprintf(out, "reading %d\n", (int)r->readings[i]);
    }
    if (r->nread > 0u) {
        fprintf(out, "avg %d\n", (int)(r->sum / (int32_t)r->nread));
    }
    for (size_t i = 0; i < r->nseries; i++) {
        fputs("series", out);
        for (size_t j = 0; j < r->series[i].n; j++) {
            fprintf(out, " %d", (int)r->series[i].data[j]);
        }
        fputc('\n', out);
    }
}

int tlv_process(const unsigned char *in, size_t len, FILE *out, FILE *err)
{
    struct report r;
    size_t off;
    uint16_t rec_count;

    memset(&r, 0, sizeof r);

    if (len < HEADER_LEN) {
        return reject(err, "pacote menor que o cabecalho");
    }
    if (memcmp(in, "SNS1", 4) != 0) {
        return reject(err, "magic invalido");
    }
    if (in[4] != 1u) {
        return reject(err, "versao nao suportada");
    }
    rec_count = be16(in + 5);

    off = HEADER_LEN;
    for (uint16_t rec = 0; rec < rec_count; rec++) {
        unsigned type;
        size_t vlen;
        const unsigned char *val;
        int rc;

        if (off + REC_HDR_LEN > len) {
            report_free(&r);
            return reject(err, "registro truncado");
        }
        type = in[off];
        vlen = be16(in + off + 1u);
        val = in + off + REC_HDR_LEN;

        switch (type) {
        case T_NAME:    rc = do_name(&r, val, vlen, err);    break;
        case T_READING: rc = do_reading(&r, val, vlen, err); break;
        case T_LABEL:   rc = do_label(&r, val, vlen, err);   break;
        case T_SERIES:  rc = do_series(&r, val, vlen, err);  break;
        default:
            report_free(&r);
            return reject(err, "tipo de registro desconhecido");
        }
        if (rc != TLV_OK) {
            report_free(&r);
            return rc;
        }
        off += REC_HDR_LEN + vlen;
    }

    if (off != len) {
        report_free(&r);
        return reject(err, "bytes sobrando apos os registros");
    }
    if (r.name == NULL) {
        report_free(&r);
        return reject(err, "pacote sem NAME");
    }

    print_report(&r, out);
    report_free(&r);
    return TLV_OK;
}
