"""Modelo executável do contrato do tlvparse (veja src/tlv.h e o README).

evaluate(pacote) -> (0, relatório) se o pacote deve ser aceito,
                    (2, motivo)    se deve ser rejeitado.

O avaliador compara o tlvparse com este modelo: código de saída e, para
pacotes aceitos, a saída byte a byte.
"""
import struct

MAX_READINGS, MAX_LABELS, MAX_SERIES = 1024, 64, 64


class Reject(Exception):
    pass


def _text(value, maximum, what):
    if not 1 <= len(value) <= maximum:
        raise Reject(f"{what} com {len(value)} bytes (permitido: 1..{maximum})")
    if any(b < 0x20 or b > 0x7E for b in value):
        raise Reject(f"{what} com byte não imprimível")
    return value.decode("ascii")


def parse(data):
    if len(data) < 7:
        raise Reject("pacote menor que o cabeçalho")
    if data[:4] != b"SNS1":
        raise Reject("magic inválido")
    if data[4] != 1:
        raise Reject("versão não suportada")
    (count,) = struct.unpack(">H", data[5:7])
    off = 7
    name, labels, readings, series = None, [], [], []
    for _ in range(count):
        if len(data) - off < 3:
            raise Reject("registro truncado (cabeçalho)")
        rtype = data[off]
        (vlen,) = struct.unpack(">H", data[off + 1:off + 3])
        if vlen > len(data) - off - 3:
            raise Reject("registro truncado (value)")
        val = data[off + 3:off + 3 + vlen]
        off += 3 + vlen
        if rtype == 0x01:
            if name is not None:
                raise Reject("mais de um NAME")
            name = _text(val, 31, "NAME")
        elif rtype == 0x02:
            if vlen != 4:
                raise Reject("READING sem 4 bytes")
            if len(readings) >= MAX_READINGS:
                raise Reject("leituras demais")
            readings.append(struct.unpack(">i", val)[0])
        elif rtype == 0x03:
            labels.append(_text(val, 63, "LABEL"))
            if len(labels) > MAX_LABELS:
                raise Reject("rótulos demais")
        elif rtype == 0x04:
            if len(series) >= MAX_SERIES:
                raise Reject("séries demais")
            if vlen < 4:
                raise Reject("SERIES sem contagem")
            (n,) = struct.unpack(">I", val[:4])
            if vlen != 4 + 2 * n:
                raise Reject("SERIES com contagem inconsistente")
            series.append(list(struct.unpack(f">{n}h", val[4:])))
        else:
            raise Reject(f"tipo desconhecido 0x{rtype:02x}")
    if off != len(data):
        raise Reject("bytes sobrando")
    if name is None:
        raise Reject("pacote sem NAME")
    return name, labels, readings, series


def report(name, labels, readings, series):
    lines = [f"sensor {name}"]
    lines += [f"label {t}" for t in labels]
    lines += [f"reading {v}" for v in readings]
    if readings:
        total, n = sum(readings), len(readings)
        avg = abs(total) // n * (1 if total >= 0 else -1)   # trunca em direção a zero
        lines.append(f"avg {avg}")
    lines += ["series" + "".join(f" {s}" for s in samples) for samples in series]
    return "".join(line + "\n" for line in lines)


def evaluate(data):
    try:
        return 0, report(*parse(data))
    except Reject as e:
        return 2, str(e)
