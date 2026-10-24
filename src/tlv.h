/* tlvparse — decodificador dos pacotes TLV que os sensores de campo enviam
 * ao coletor de telemetria.
 *
 * Formato do pacote (inteiros em big-endian), lido de stdin:
 *
 *   cabeçalho (7 bytes):
 *     magic   4 bytes  "SNS1"
 *     version 1 byte   (== 1)
 *     count   2 bytes  quantidade de registros TLV que vêm a seguir
 *   em seguida, exatamente `count` registros, e nada depois deles:
 *     type    1 byte
 *     length  2 bytes  tamanho do campo value, em bytes
 *     value   length bytes
 *
 * Tipos de registro:
 *   0x01 NAME     nome do sensor: ASCII imprimível (0x20..0x7e), 1..31 bytes;
 *                 exatamente um por pacote
 *   0x02 READING  leitura: exatamente 4 bytes, int32; no máximo 1024
 *   0x03 LABEL    rótulo: ASCII imprimível, 1..63 bytes; no máximo 64
 *   0x04 SERIES   série: uint32 `n` seguido de exatamente n amostras int16
 *                 (length == 4 + 2n); no máximo 64 séries
 *
 * Relatório (stdout), nesta ordem:
 *   sensor <nome>
 *   label <rótulo>            um por LABEL, na ordem do pacote
 *   reading <valor>           um por READING, na ordem do pacote
 *   avg <média>               só se houver leituras: média exata, truncada
 *                             em direção a zero
 *   series <a1> <a2> ...      uma linha por SERIES ("series" se n == 0)
 *
 * Qualquer violação do formato rejeita o pacote inteiro: nada em stdout,
 * uma linha "tlvparse: <motivo>" em stderr e código de saída 2.
 */
#ifndef TLV_H
#define TLV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TLV_OK      0
#define TLV_REJECT  2

/* Valida o pacote `in` (exatamente `len` bytes) e, se ele for aceito,
 * escreve o relatório em `out`. Mensagens de erro vão para `err`.
 * Retorna TLV_OK ou TLV_REJECT. Pode ser chamada várias vezes no mesmo
 * processo (o avaliador faz isso): não guarde estado entre chamadas. */
int tlv_process(const unsigned char *in, size_t len, FILE *out, FILE *err);

#endif /* TLV_H */
