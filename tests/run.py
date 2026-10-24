#!/usr/bin/env python3
"""Avaliador do desafio-32: o tlvparse diante de pacotes válidos, malformados e mutados.

Chamado por `make test` (dentro do container gcc:14.2.0 ou com GITQUEST_LOCAL=1).

- Cada pacote fixo (tests/packets.py) roda num processo novo do build/tlvparse,
  compilado com ASan + UBSan (+ LSan), -fno-sanitize-recover, e é comparado com
  o modelo do contrato (tests/model.py).
- O fuzzing aplica mutações determinísticas (semente fixa) a pacotes válidos e
  chama tlv_process() no mesmo processo (build/fuzz-driver), sob ASan + UBSan.
- A build endurecida (-O2, _FORTIFY_SOURCE=3, -Wconversion, -Wsign-conversion,
  -Werror) precisa compilar e produzir a mesma saída nos pacotes válidos.

Todo pacote que reprova é gravado em build/failures/ para você reproduzir:
    ./build/tlvparse < build/failures/<caso>.bin
"""
import os
import random
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)

import model    # noqa: E402
import packets  # noqa: E402

ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
TLVPARSE = os.path.join(BUILD, "tlvparse")
HARDENED = os.path.join(BUILD, "tlvparse-hardened")
DRIVER = os.path.join(BUILD, "fuzz-driver")
FAILURES = os.path.join(BUILD, "failures")

FUZZ_SEED = 32032
FUZZ_CASES = 4000
CASE_TIMEOUT = 10
FUZZ_TIMEOUT = 150

SANITIZER_MARKS = ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:",
                   "SUMMARY: UndefinedBehaviorSanitizer", "buffer overflow detected",
                   "stack smashing detected")

ENV = dict(os.environ)
ENV["ASAN_OPTIONS"] = ("detect_leaks=1:halt_on_error=1:abort_on_error=0:color=never:"
                       "max_allocation_size_mb=64:allocator_may_return_null=0")
ENV["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1:color=never"
ENV["LSAN_OPTIONS"] = "color=never"


class Failed(Exception):
    pass


def sanitizer_line(stderr):
    """Resumo de uma linha do primeiro relatório de sanitizer em `stderr`, ou None."""
    lines = stderr.splitlines()
    for n, line in enumerate(lines):
        if not any(mark in line for mark in SANITIZER_MARKS):
            continue
        m = re.search(r"(AddressSanitizer|LeakSanitizer): (detected memory leaks|[\w-]+)", line)
        if not m:
            return line.strip()[:200]
        summary = f"{m.group(1)}: {m.group(2)}"
        access = next((x.strip() for x in lines[n + 1:n + 3] if re.match(r"\s*(READ|WRITE) of size", x)), "")
        frame = next((f for f in (re.search(r"#\d+ 0x[0-9a-f]+ in (\S+) (\S*src/\S+)", x) for x in lines[n:])
                      if f), None)
        if access:
            summary += f" ({access.split(' at ')[0]})"
        if frame:
            summary += f" em {frame.group(1)} {frame.group(2)}"
        return summary[:200]
    return None


def save_failure(key, data):
    os.makedirs(FAILURES, exist_ok=True)
    path = os.path.join(FAILURES, key.replace("/", "-") + ".bin")
    with open(path, "wb") as fh:
        fh.write(data)
    return os.path.relpath(path, ROOT)


def run_binary(binary, data):
    try:
        p = subprocess.run([binary], input=data, capture_output=True, env=ENV, timeout=CASE_TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, b"", f"travou: passou de {CASE_TIMEOUT} s"
    return p.returncode, p.stdout, p.stderr.decode("utf-8", "replace")


def check_case(binary, key, data):
    """None se o tlvparse se comportou como o contrato manda; senão, o motivo."""
    want_rc, want = model.evaluate(data)
    rc, out, err = run_binary(binary, data)
    if rc is None:
        return err
    san = sanitizer_line(err)
    if san:
        return f"relatório de sanitizer: {san}"
    if rc < 0:
        return f"morreu pelo sinal {-rc}"
    if want_rc == 0:
        if rc != 0:
            first = err.strip().splitlines()[0] if err.strip() else "(stderr vazio)"
            return f"pacote válido rejeitado (código {rc}): {first[:120]}"
        got = out.decode("utf-8", "replace")
        if got != want:
            return describe_diff(want, got)
        return None
    if rc != 2:
        return f"pacote inválido ({want}) terminou com código {rc}; esperado 2"
    if out:
        return f"pacote inválido ({want}) rejeitado, mas algo foi escrito em stdout"
    if not re.search(r"^tlvparse: \S", err, re.M):
        return f"pacote inválido ({want}) rejeitado sem a mensagem 'tlvparse: <motivo>' em stderr"
    return None


def describe_diff(want, got):
    w, g = want.splitlines(), got.splitlines()
    for i in range(max(len(w), len(g))):
        a = w[i] if i < len(w) else "(nada)"
        b = g[i] if i < len(g) else "(nada)"
        if a != b:
            return f"saída difere na linha {i + 1}: esperado {a[:70]!r}, obtido {b[:70]!r}"
    return "saída difere (fim de linha ou linha final)"


def check_group(binary, cases, label):
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda kv: (kv[0], kv[1], check_case(binary, kv[0], kv[1])), cases.items()))
    bad = [(k, d, why) for k, d, why in results if why]
    if bad:
        lines = []
        for k, d, why in bad[:4]:
            path = save_failure(f"{label}/{k}", d)
            lines.append(f"[{k}] {why} (pacote: {path})")
        more = f" … e mais {len(bad) - 4}" if len(bad) > 4 else ""
        raise Failed(f"{len(bad)} de {len(cases)} casos falharam:\n        " + "\n        ".join(lines) + more)


# ---------------------------------------------------------------- fuzzing

INTERESTING16 = [0, 1, 2, 3, 4, 31, 32, 63, 64, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF]
INTERESTING32 = [0, 1, 2, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x3FFFFFFF, 0x40000000,
                 0x7FFFFFFF, 0x80000000, 0x80000001, 0x80000002, 0xFFFFFFFE, 0xFFFFFFFF]


def mutate(rng, data):
    buf = bytearray(data)
    for _ in range(rng.randint(1, 4)):
        op = rng.randrange(9)
        if len(buf) <= 7:
            buf = bytearray(packets.packet([packets.name("x")]))
        # quase sempre preserva o cabeçalho, para a mutação chegar aos registros
        pos = rng.randrange(len(buf)) if rng.random() < 0.04 else rng.randrange(7, len(buf))
        if op == 0:                                   # inverte um bit
            buf[pos] ^= 1 << rng.randrange(8)
        elif op == 1:                                 # byte "interessante"
            buf[pos] = rng.choice([0x00, 0x01, 0x20, 0x7E, 0x7F, 0x80, 0xFF])
        elif op == 2 and len(buf) >= 2:               # campo de 16 bits
            pos = rng.randrange(len(buf) - 1)
            buf[pos:pos + 2] = struct.pack(">H", rng.choice(INTERESTING16))
        elif op == 3 and len(buf) >= 4:               # campo de 32 bits
            pos = rng.randrange(len(buf) - 3)
            buf[pos:pos + 4] = struct.pack(">I", rng.choice(INTERESTING32))
        elif op == 4:                                 # trunca
            del buf[rng.randrange(len(buf)):]
        elif op == 5:                                 # remove um trecho
            del buf[pos:pos + rng.randint(1, 8)]
        elif op == 6:                                 # duplica um trecho
            n = rng.randint(1, 16)
            buf[pos:pos] = buf[pos:pos + n]
        elif op == 7:                                 # insere bytes
            buf[pos:pos] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 8)))
        else:                                         # enxerta outro registro
            buf[pos:pos] = rng.choice([packets.name("N" * rng.choice([1, 31, 32, 40])),
                                       packets.label("L" * rng.choice([1, 63, 64, 90])),
                                       packets.reading(rng.choice([packets.I32_MAX, packets.I32_MIN, 0])),
                                       packets.series([1, 2, 3], count=rng.choice([3, 4, 0x80000003]))])
    return bytes(buf)


def random_valid(rng):
    """Um pacote válido sorteado, com valores extremos de vez em quando."""
    recs = [packets.name("n" * rng.choice([1, 5, 31]))]
    for _ in range(rng.randint(0, 12)):
        kind = rng.randrange(3)
        if kind == 0:
            recs.append(packets.label(chr(rng.randrange(0x20, 0x7F)) * rng.choice([1, 9, 63])))
        elif kind == 1:
            recs.append(packets.reading(rng.choice([packets.I32_MAX, packets.I32_MIN, -1, 0,
                                                    rng.randrange(packets.I32_MIN, packets.I32_MAX)])))
        else:
            recs.append(packets.series([rng.choice([-32768, 32767, rng.randrange(-32768, 32768)])
                                        for _ in range(rng.choice([0, 1, 7, 40]))]))
    rng.shuffle(recs)
    return packets.packet(recs)


def fuzz_corpus():
    rng = random.Random(FUZZ_SEED)
    corpus = []
    for _ in range(FUZZ_CASES):
        if rng.random() < 0.2:
            corpus.append(random_valid(rng))
        else:
            corpus.append(mutate(rng, rng.choice(packets.FUZZ_SEEDS)))
    return corpus


def t_fuzz():
    corpus = fuzz_corpus()
    os.makedirs(os.path.join(BUILD, "fuzz"), exist_ok=True)
    path = os.path.join(BUILD, "fuzz", "corpus.bin")
    with open(path, "wb") as fh:
        for data in corpus:
            fh.write(struct.pack("<I", len(data)) + data)
    try:
        p = subprocess.run([DRIVER, path], capture_output=True, env=ENV, timeout=FUZZ_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise Failed(f"o fuzzing passou de {FUZZ_TIMEOUT} s (laço longo demais ou travado?)")
    err = p.stderr.decode("utf-8", "replace")
    san = sanitizer_line(err)
    started = re.findall(r"^caso (\d+)$", err, re.M)
    finished = re.search(r"^fim \d+$", err, re.M)
    if (san or p.returncode != 0) and not finished and started:
        i = int(started[-1])
        saved = save_failure(f"fuzz/{i:04d}", corpus[i])
        raise Failed(f"o caso {i} de {FUZZ_CASES} derrubou o parser: "
                     f"{san or f'código {p.returncode}'} (pacote: {saved})")
    if san:
        raise Failed(f"relatório de sanitizer ao fim do fuzzing: {san} "
                     "(vazamento? rode os pacotes fixos com 'limites' para achar o caminho)")
    if p.returncode != 0:
        raise Failed(f"o driver de fuzzing terminou com código {p.returncode}")

    out, pos, problems, idx = p.stdout, 0, [], 0
    while pos < len(out):
        nl = out.index(b"\n", pos)
        i, rc, olen, _elen = (int(x) for x in out[pos:nl].split())
        text = out[nl + 1:nl + 1 + olen].decode("utf-8", "replace")
        pos = nl + 1 + olen
        want_rc, want = model.evaluate(corpus[i])
        if rc != want_rc:
            problems.append((i, f"tlv_process devolveu {rc}, o contrato manda {want_rc}"
                                + (f" ({want})" if want_rc else "")))
        elif rc == 0 and text != want:
            problems.append((i, describe_diff(want, text)))
        elif rc != 0 and text:
            problems.append((i, "pacote rejeitado, mas algo foi escrito no relatório"))
        idx += 1
    if idx != FUZZ_CASES:
        raise Failed(f"o driver processou {idx} de {FUZZ_CASES} casos")
    if problems:
        lines = [f"[caso {i}] {why} (pacote: {save_failure(f'fuzz/{i:04d}', corpus[i])})"
                 for i, why in problems[:3]]
        raise Failed(f"{len(problems)} de {FUZZ_CASES} mutações divergem do contrato:\n        "
                     + "\n        ".join(lines))


# ---------------------------------------------------------------- critérios

def make(*targets):
    return subprocess.run(["make", "--no-print-directory", "-C", ROOT, *targets],
                          capture_output=True, text=True)


def compile_errors(result, logname):
    log = result.stdout + result.stderr
    with open(os.path.join(BUILD, logname), "w") as fh:
        fh.write(log)
    errs = [line.strip()[:180] for line in log.splitlines() if "error:" in line]
    return "\n        ".join(errs[:4]) or "veja o log"


def t_valid():
    check_group(TLVPARSE, packets.VALID, "valido")


def t_extreme():
    check_group(TLVPARSE, packets.EXTREME, "extremo")


def t_lengths():
    check_group(TLVPARSE, packets.LENGTHS, "comprimentos")


def t_series():
    check_group(TLVPARSE, packets.SERIES_COUNTS, "contagens-de-serie")


def t_text():
    check_group(TLVPARSE, packets.TEXT, "textos")


def t_limits():
    check_group(TLVPARSE, packets.LIMITS, "limites")


def t_hardened():
    r = make("harden")
    if r.returncode != 0:
        raise Failed("a build endurecida não compila (log: build/harden.log):\n        "
                     + compile_errors(r, "harden.log"))
    check_group(HARDENED, packets.VALID, "endurecida")


CRITERIA = [
    ("pacotes válidos, inclusive nos limites máximos, geram exatamente o relatório de referência", t_valid),
    ("leituras extremas (soma além de 32 bits) dão a média exata, sem comportamento indefinido", t_extreme),
    ("comprimentos declarados maiores que o pacote e pacotes truncados são rejeitados (código 2)", t_lengths),
    ("contagens de SERIES enormes ou inconsistentes são rejeitadas (código 2)", t_series),
    ("NAME e LABEL fora do tamanho ou com bytes proibidos são rejeitados (código 2)", t_text),
    ("limites de quantidade e erros no meio do pacote: rejeição sem vazamento de memória", t_limits),
    (f"fuzzing determinístico: {FUZZ_CASES} mutações (semente {FUZZ_SEED}) sem crash e conforme o contrato", t_fuzz),
    ("build endurecida (-O2 -D_FORTIFY_SOURCE=3 -Wconversion -Wsign-conversion -Werror) compila e "
     "gera o mesmo relatório", t_hardened),
]


def main():
    color = sys.stdout.isatty() and not os.environ.get("NO_COLOR")
    green, red, reset = ("\033[32m", "\033[31m", "\033[0m") if color else ("", "", "")

    def ok(msg):
        print(f"  {green}✔{reset} {msg}", flush=True)

    def bad(msg, why):
        print(f"  {red}✘{reset} {msg}\n      motivo: {why}", flush=True)

    os.makedirs(BUILD, exist_ok=True)
    if os.path.isdir(FAILURES):
        for f in os.listdir(FAILURES):
            os.remove(os.path.join(FAILURES, f))

    gcc = subprocess.run(["gcc", "-dumpfullversion"], capture_output=True, text=True).stdout.strip()
    print(f"==> Compilando src/ (gcc {gcc}, -std=c17 -Wall -Wextra -Wpedantic -Werror, ASan + UBSan)")
    r = make("build/tlvparse", "build/fuzz-driver")
    if r.returncode != 0:
        bad("o tlvparse compila em C17 sem avisos, com ASan + UBSan",
            "erros de compilação (log: build/compile.log):\n        " + compile_errors(r, "compile.log"))
        return 1
    ok("o tlvparse compila em C17 sem avisos, com ASan + UBSan")

    print("==> Pacotes fixos (um processo por pacote), fuzzing e build endurecida")
    passed, failed = 1, 0
    for desc, fn in CRITERIA:
        try:
            fn()
        except Failed as e:
            bad(desc, str(e))
            failed += 1
        else:
            ok(desc)
            passed += 1

    print()
    total = passed + failed
    if failed:
        print(f"{passed} de {total} critérios satisfeitos. Reproduza um caso com "
              "'./build/tlvparse < build/failures/<caso>.bin' (dentro do container) "
              "e liste os pacotes com 'python3 tests/packets.py list'.")
        return 1
    print(f"Todos os {total} critérios satisfeitos: o coletor rejeita o que não presta e não cai mais.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
