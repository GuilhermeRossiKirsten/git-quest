#!/usr/bin/env python3
"""Gerador determinístico dos pacotes usados pelo avaliador do desafio-32.

Uso (dentro do container ou com python3 local):

    python3 tests/packets.py list                 # nomes de todos os casos
    python3 tests/packets.py write <caso> <arq>   # grava o pacote em <arq>
    python3 tests/packets.py expect <caso>        # saída esperada (contrato)

Nada aqui é aleatório: os mesmos casos são gerados em toda execução.
"""
import os
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model  # noqa: E402

NAME, READING, LABEL, SERIES = 0x01, 0x02, 0x03, 0x04
I32_MAX, I32_MIN = 2**31 - 1, -2**31


# ---------------------------------------------------------------- construtores

def rec(rtype, value, length=None):
    """Um registro TLV; `length` permite declarar um tamanho mentiroso."""
    if length is None:
        length = len(value)
    return struct.pack(">BH", rtype, length) + value


def name(text):
    return rec(NAME, text.encode("ascii") if isinstance(text, str) else text)


def label(text):
    return rec(LABEL, text.encode("ascii") if isinstance(text, str) else text)


def reading(v):
    return rec(READING, struct.pack(">i", v))


def series(samples, count=None):
    n = len(samples) if count is None else count
    return rec(SERIES, struct.pack(">I", n) + b"".join(struct.pack(">h", s) for s in samples))


def packet(records, count=None, magic=b"SNS1", version=1, tail=b""):
    """Cabeçalho + registros; `count` permite declarar uma contagem errada."""
    n = len(records) if count is None else count
    return magic + bytes([version]) + struct.pack(">H", n) + b"".join(records) + tail


def ramp(n, start=-500, step=7):
    """Amostras int16 previsíveis (sem aleatoriedade)."""
    return [((start + i * step + 32768) % 65536) - 32768 for i in range(n)]


# ---------------------------------------------------------------- pacotes válidos

DEMO = packet([name("estacao-07"), label("telhado norte"), reading(215), reading(-38),
               reading(1002), series([12, -3, 40, 7])])

VALID = {
    "demo": DEMO,
    "minimo-so-nome": packet([name("a")]),
    "nome-31-bytes": packet([name("N" * 31), reading(1)]),
    "rotulo-63-bytes": packet([name("s"), label("L" * 63)]),
    "rotulo-com-simbolos": packet([name("s"), label("100% ok {x} \\ ~"), label(" ")]),
    "64-rotulos": packet([name("muitos-rotulos")] + [label(f"r{i:02d}") for i in range(64)]),
    "1024-leituras": packet([name("muitas-leituras")] + [reading(i * 37 - 9000) for i in range(1024)]),
    "64-series": packet([name("muitas-series")] + [series(ramp(i % 9, start=i)) for i in range(64)]),
    "serie-vazia": packet([name("vazia"), series([])]),
    "serie-maxima-32765": packet([name("serie-grande"), series(ramp(32765))]),
    "ordem-misturada": packet([series([1, 2]), reading(-7), label("depois"), name("fim"),
                               reading(-8), label("antes")]),
    "media-negativa-trunca": packet([name("neg"), reading(-7), reading(-2)]),
    "todos-os-limites": packet([name("Z" * 31)] + [label("x" * 63) for _ in range(64)]
                               + [reading(i - 512) for i in range(1024)]
                               + [series(ramp(200, start=i)) for i in range(64)]),
}

# Valores extremos: o pacote é válido, a soma é que não cabe em 32 bits.
EXTREME = {
    "dois-int32-max": packet([name("pico"), reading(I32_MAX), reading(I32_MAX)]),
    "max-mais-um": packet([name("pico"), reading(I32_MAX), reading(1)]),
    "tres-negativos-extremos": packet([name("vale"), reading(I32_MIN), reading(I32_MIN), reading(-1)]),
    "1024-int32-max": packet([name("saturado")] + [reading(I32_MAX)] * 1024),
    "1024-int32-min": packet([name("saturado")] + [reading(I32_MIN)] * 1024),
    "min-e-max": packet([name("faixa"), reading(I32_MIN), reading(I32_MAX)]),
}


# ---------------------------------------------------------------- pacotes malformados

def _hdr(n):
    return b"SNS1\x01" + struct.pack(">H", n)


LENGTHS = {
    "vazio": b"",
    "so-magic": b"SNS1",
    "cabecalho-incompleto": b"SNS1\x01\x00",
    "magic-errado": b"SNS2\x01\x00\x01" + name("x"),
    "versao-2": packet([name("x")], version=2),
    "contagem-sem-registros": _hdr(1),
    "cabecalho-de-registro-pela-metade": _hdr(2) + name("x") + b"\x03\x00",
    "contagem-maior-que-registros": packet([name("x"), reading(1)], count=5),
    "bytes-sobrando": packet([name("x")], tail=b"\x00\x00"),
    "nome-declara-20-tem-5": _hdr(1) + rec(NAME, b"curto", length=20),
    "rotulo-declara-60-tem-10": _hdr(2) + name("x") + rec(LABEL, b"0123456789", length=60),
    "leitura-declara-4-tem-2": _hdr(2) + name("x") + rec(READING, b"\x00\x01", length=4),
    "serie-declara-mais-que-o-pacote": _hdr(2) + name("x") + rec(SERIES, struct.pack(">I", 3) + b"\x00\x01", length=10),
    "length-ffff-no-fim": _hdr(2) + name("x") + rec(LABEL, b"abc", length=0xFFFF),
    "rotulo-length-63-tem-1": _hdr(2) + name("x") + rec(LABEL, b"z", length=63),
}

SERIES_COUNTS = {
    "serie-n-2-31-sem-amostras": packet([name("x"), rec(SERIES, struct.pack(">I", 0x80000000))]),
    "serie-n-2-31-mais-2": packet([name("x"), rec(SERIES, struct.pack(">I", 0x80000002) + b"\x00\x01\x00\x02")]),
    "serie-n-ffffffff": packet([name("x"), rec(SERIES, struct.pack(">I", 0xFFFFFFFF) + b"\x00" * 2)]),
    "serie-n-maior-que-o-corpo": packet([name("x"), series([1, 2, 3], count=5)]),
    "serie-n-menor-que-o-corpo": packet([name("x"), series([1, 2, 3], count=2)]),
    "serie-corpo-impar": packet([name("x"), rec(SERIES, struct.pack(">I", 1) + b"\x00\x01\x02")]),
    "serie-sem-contagem": packet([name("x"), rec(SERIES, b"\x00\x00\x00")]),
}

TEXT = {
    "nome-32-bytes": packet([name("N" * 32)]),
    "nome-64-bytes": packet([name("N" * 64)]),
    "nome-300-bytes": packet([name("sensor-" * 42 + "fim")]),
    "nome-vazio": packet([name("")]),
    "nome-com-quebra-de-linha": packet([name("a\nb")]),
    "nome-com-byte-zero": packet([name(b"ab\x00cd")]),
    "dois-nomes": packet([name("a"), name("b")]),
    "sem-nome": packet([reading(1), label("x")]),
    "rotulo-64-bytes": packet([name("x"), label("L" * 64)]),
    "rotulo-200-bytes": packet([name("x"), label("L" * 200)]),
    "rotulo-vazio": packet([name("x"), label("")]),
    "rotulo-com-del": packet([name("x"), label(b"abc\x7f")]),
}

LIMITS = {
    "65-rotulos": packet([name("x")] + [label(f"r{i}") for i in range(65)]),
    "65-rotulos-e-depois-leitura": packet([name("x")] + [label("r")] * 65 + [reading(3)]),
    "1025-leituras": packet([name("x")] + [reading(i) for i in range(1025)]),
    "65-series": packet([name("x")] + [series([i, i]) for i in range(65)]),
    "tipo-desconhecido-apos-alocar": packet([name("x"), label("a"), series([1, 2]), rec(0x09, b"??")]),
    "truncado-apos-alocar": _hdr(4) + name("x") + label("a") + series([1, 2]) + b"\x02\x00",
    "leitura-de-3-bytes-apos-alocar": packet([name("x"), label("a"), series([5]), rec(READING, b"\x00\x00\x01")]),
    "nome-repetido-apos-alocar": packet([name("x"), label("a"), series([5]), name("y")]),
}

MALFORMED_GROUPS = [
    ("comprimentos", LENGTHS),
    ("contagens-de-serie", SERIES_COUNTS),
    ("textos", TEXT),
    ("limites", LIMITS),
]

# Sementes do fuzzing: pacotes válidos pequenos, que exercitam todos os tipos.
FUZZ_SEEDS = [
    DEMO,
    VALID["ordem-misturada"],
    packet([name("f"), label("L" * 63), series(ramp(6)), reading(I32_MAX), reading(5)]),
    packet([name("N" * 31), series([]), series([1]), reading(I32_MIN), reading(-1)]),
    packet([label("a"), name("b"), label("c")]),
]


def all_cases():
    cases = {}
    for prefix, group in [("valido", VALID), ("extremo", EXTREME)] + MALFORMED_GROUPS:
        for key, data in group.items():
            cases[f"{prefix}/{key}"] = data
    return cases


def main(argv):
    cases = all_cases()
    if len(argv) == 2 and argv[1] == "list":
        for key, data in cases.items():
            print(f"{key:55} {len(data):7d} bytes")
        return 0
    if len(argv) == 4 and argv[1] == "write" and argv[2] in cases:
        with open(argv[3], "wb") as fh:
            fh.write(cases[argv[2]])
        return 0
    if len(argv) == 3 and argv[1] == "expect" and argv[2] in cases:
        rc, out = model.evaluate(cases[argv[2]])
        sys.stdout.write(out if rc == 0 else f"(rejeitado: {out})\n")
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
