#!/usr/bin/env python3
"""Demonstração do desafio-32: o tlvparse diante de três pacotes de campo.

Chamado por `make demo` (e por ./start.sh). Mostra a saída, o código de saída
e o começo de qualquer relatório do ASan/UBSan.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)

import packets  # noqa: E402

TLVPARSE = os.path.join(os.path.dirname(HERE), "build", "tlvparse")

SHOWCASE = [
    ("pacote normal do sensor estacao-07", packets.DEMO),
    ("sensor com firmware novo: dois picos de leitura", packets.EXTREME["dois-int32-max"]),
    ("pacote cortado no meio do envio", packets.LENGTHS["rotulo-declara-60-tem-10"]),
]


def main():
    env = dict(os.environ, ASAN_OPTIONS="color=never", UBSAN_OPTIONS="color=never:print_stacktrace=1")
    for title, data in SHOWCASE:
        print(f"--- {title} ({len(data)} bytes)")
        p = subprocess.run([TLVPARSE], input=data, capture_output=True, env=env, timeout=20)
        for line in p.stdout.decode("utf-8", "replace").splitlines()[:8]:
            print(f"    | {line}")
        err = p.stderr.decode("utf-8", "replace").splitlines()
        for line in err[:6]:
            print(f"    ! {line[:150]}")
        if len(err) > 6:
            print(f"    ! ... ({len(err) - 6} linhas a mais em stderr)")
        print(f"    (código de saída: {p.returncode})")
    print("Os casos dos testes podem ser gerados com: python3 tests/packets.py list | write <caso> <arquivo>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
