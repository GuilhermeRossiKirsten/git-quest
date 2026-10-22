#!/usr/bin/env python3
"""Avaliador do desafio-21: roda o minisup em cenários reais e inspeciona /proc.

Chamado por `make test` (dentro do container gcc:14.2.0 ou com GITQUEST_LOCAL=1).
Cada cenário sobe o build/minisup (compilado com ASan + UBSan) com serviços de
mentira (build/gq-helper), observa a saída, os processos e o código de saída, e
no fim mata tudo o que sobrou. Todo processo de um cenário carrega a variável de
ambiente GQ_TEST_TAG=<etiqueta>, o que permite achar netos e sobreviventes.

GQ_VERBOSE=1 mostra a saída completa do minisup nos cenários que falharem.
"""
import os
import re
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MINISUP = os.path.join(ROOT, "build", "minisup")
HELPER = os.path.join(ROOT, "build", "gq-helper")
VERBOSE = os.environ.get("GQ_VERBOSE") == "1"

SANITIZER_MARKS = ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:")
sanitizer_reports = []  # (cenário, primeira linha do relatório)


# ---------------------------------------------------------------- /proc

def proc_stat(pid):
    """(estado, ppid, comm) de um processo, ou None se ele já sumiu."""
    try:
        with open(f"/proc/{pid}/stat") as fh:
            s = fh.read()
    except OSError:
        return None
    lp, rp = s.find("("), s.rfind(")")
    fields = s[rp + 2:].split()
    return fields[0], int(fields[1]), s[lp + 1:rp]


def all_pids():
    return [int(d) for d in os.listdir("/proc") if d.isdigit()]


def zombies_of(ppid):
    out = []
    for pid in all_pids():
        st = proc_stat(pid)
        if st and st[0] == "Z" and st[1] == ppid:
            out.append((pid, st[2]))
    return out


def tagged_alive(tag):
    """Processos vivos (não zumbis) que carregam GQ_TEST_TAG=tag no ambiente."""
    needle = f"GQ_TEST_TAG={tag}".encode()
    out = []
    for pid in all_pids():
        try:
            with open(f"/proc/{pid}/environ", "rb") as fh:
                env = fh.read().split(b"\0")
        except OSError:
            continue
        if needle in env:
            st = proc_stat(pid)
            if st and st[0] != "Z":
                out.append((pid, st[2]))
    return out


def describe_procs(procs):
    return ", ".join(f"pid {pid} ({comm})" for pid, comm in procs)


# ---------------------------------------------------------------- minisup

def _no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class Minisup:
    """Um minisup rodando com uma configuração gerada para o cenário."""

    def __init__(self, scenario, services, grace_ms=2000, backoff_ms=20):
        self.scenario = scenario
        self.tag = f"gq21-{scenario}-{uuid.uuid4().hex[:8]}"
        self.dir = tempfile.mkdtemp(prefix=f"gq21-{scenario}-")
        self.log = os.path.join(self.dir, "minisup.log")
        conf = os.path.join(self.dir, "minisup.conf")
        with open(conf, "w") as fh:
            for name, restarts, args in services:
                fh.write(f"{name} {restarts} {HELPER} {args}\n")
        env = dict(os.environ)
        env["GQ_TEST_TAG"] = self.tag
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=0:color=never"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1:color=never"
        self.out, self.err = [], []
        self.cond = threading.Condition()
        self.started = time.monotonic()
        self.proc = subprocess.Popen(
            [MINISUP, "-g", str(grace_ms), "-b", str(backoff_ms), "-l", self.log, conf],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, close_fds=True, start_new_session=True, preexec_fn=_no_core)
        self.pid = self.proc.pid
        self.threads = [threading.Thread(target=self._pump, args=(self.proc.stdout, self.out), daemon=True),
                        threading.Thread(target=self._pump, args=(self.proc.stderr, self.err), daemon=True)]
        for t in self.threads:
            t.start()

    def _pump(self, stream, sink):
        for raw in iter(stream.readline, b""):
            with self.cond:
                sink.append(raw.decode("utf-8", "replace").rstrip("\n"))
                self.cond.notify_all()
        with self.cond:
            self.cond.notify_all()

    def wait_line(self, pattern, timeout=5.0):
        """Primeira linha da saída que casa com `pattern` (regex), ou None."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        seen = 0
        with self.cond:
            while True:
                for line in self.out[seen:]:
                    m = rx.search(line)
                    if m:
                        return m
                seen = len(self.out)
                left = deadline - time.monotonic()
                if left <= 0 or (self.proc.poll() is not None and not self.threads[0].is_alive()):
                    return None
                self.cond.wait(min(left, 0.1))

    def lines(self):
        with self.cond:
            return list(self.out)

    def count(self, pattern):
        rx = re.compile(pattern)
        return sum(1 for line in self.lines() if rx.search(line))

    def summary(self, service):
        prefix = f"minisup: {service}: "
        for line in self.lines():
            if line.startswith(prefix):
                return line
        return None

    def send(self, sig):
        if self.proc.poll() is None:
            os.kill(self.pid, sig)

    def wait_exit(self, timeout):
        """Código de saída, ou None se o minisup ainda estiver vivo no prazo."""
        try:
            rc = self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return None
        for t in self.threads:
            t.join(timeout=2)
        return rc

    def alive(self):
        return self.proc.poll() is None

    def cleanup(self, failed):
        if self.alive():
            self.proc.kill()
            self.proc.wait()
        for _ in range(3):
            for pid, _comm in tagged_alive(self.tag):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
            time.sleep(0.05)
        for t in self.threads:
            t.join(timeout=2)
        err = "\n".join(self.err)
        for mark in SANITIZER_MARKS:
            if mark in err:
                first = next(line for line in self.err if mark in line)
                sanitizer_reports.append((self.scenario, first.strip()))
                break
        if failed and VERBOSE:
            print(f"        --- saída do minisup ({self.scenario}) ---")
            for line in self.lines()[-40:]:
                print(f"        | {line}")
            for line in self.err[-20:]:
                print(f"        ! {line}")
            if os.path.exists(self.log):
                print(f"        --- {self.log} ---")
                with open(self.log, errors="replace") as fh:
                    for line in fh.read().splitlines()[-25:]:
                        print(f"        # {line}")
        shutil.rmtree(self.dir, ignore_errors=True)


def exit_reason(sup, rc):
    """Explica um término inesperado do próprio minisup."""
    if rc is not None and rc < 0:
        return f"o próprio minisup morreu pelo sinal {-rc} ({signal.Signals(-rc).name})"
    if any(m in "\n".join(sup.err) for m in SANITIZER_MARKS):
        return "o minisup abortou com um relatório do ASan/UBSan (GQ_VERBOSE=1 ./test.sh para ver)"
    return None


# ---------------------------------------------------------------- critérios

class Failed(Exception):
    pass


def require(cond, reason):
    if not cond:
        raise Failed(reason)


def scenario(name, services, **kw):
    """Context manager: sobe o minisup e garante a limpeza no fim."""
    class _Ctx:
        def __enter__(self):
            self.sup = Minisup(name, services, **kw)
            return self.sup

        def __exit__(self, et, ev, tb):
            self.sup.cleanup(failed=et is not None)
            return False
    return _Ctx()


def wait_natural_end(sup, timeout, what):
    rc = sup.wait_exit(timeout)
    if rc is None:
        left = [p for p in tagged_alive(sup.tag) if p[0] != sup.pid]
        zombies = zombies_of(sup.pid)
        if not left:
            detail = "todos os serviços já terminaram, mas o minisup continua rodando"
            if zombies:
                detail += f"; ele tem {len(zombies)} filho(s) zumbi(s) nunca colhido(s)"
            raise Failed(f"{what}: o minisup não terminou sozinho em {timeout:g} s — {detail}")
        raise Failed(f"{what}: o minisup não terminou em {timeout:g} s; ainda vivos: {describe_procs(left)}")
    why = exit_reason(sup, rc)
    require(why is None, f"{what}: {why}")
    return rc


def check_summary(sup, service, expected_rx, explain):
    line = sup.summary(service)
    require(line is not None, f"faltou a linha de resumo 'minisup: {service}: ...' na saída")
    require(re.search(expected_rx, line), f"{explain}; o resumo diz: '{line}'")


def t_relay():
    with scenario("saida", [("muito", 0, "lines 20000 resto-sem-quebra"),
                            ("curto", 0, "exit 0")]) as sup:
        rc = wait_natural_end(sup, 15, "dois serviços que saem com 0")
        got = [line for line in sup.lines() if line.startswith("muito | linha ")]
        expected = [f"muito | linha {i:06d}" for i in range(1, 20001)]
        if got != expected:
            for i, (a, b) in enumerate(zip(got, expected)):
                if a != b:
                    raise Failed(f"linha {i + 1} do serviço 'muito' chegou como '{a}' (esperado '{b}')")
            raise Failed(f"o serviço 'muito' escreveu 20000 linhas; chegaram {len(got)}")
        require("muito | resto-sem-quebra" in sup.lines(),
                "o trecho final sem '\\n' do serviço 'muito' não foi repassado")
        require(sup.count(r"^curto \| start pid=") == 1, "a linha do serviço 'curto' não foi repassada")
        check_summary(sup, "muito", r"saiu com código 0, reinícios: 0", "o resumo de 'muito' está errado")
        require(rc == 0, f"o minisup saiu com código {rc}; esperado 0 (todos os serviços saíram com 0)")


def t_zombies():
    with scenario("zumbis", [("forker", 0, "forker 300"), ("vigia", 0, "sleep")]) as sup:
        m = sup.wait_line(r"^forker \| neto pid=(\d+) adotado por (\d+)", 8)
        require(sup.wait_line(r"^vigia \| ready", 1) is not None and m is not None,
                f"os serviços não chegaram a rodar (minisup vivo: {sup.alive()})")
        adopter = int(m.group(2))
        require(adopter == sup.pid,
                f"o neto órfão foi adotado pelo PID {adopter}, não pelo minisup (PID {sup.pid}); "
                "o minisup precisa continuar sendo subreaper (PR_SET_CHILD_SUBREAPER)")
        time.sleep(1.5)
        why = exit_reason(sup, sup.proc.poll())
        require(sup.alive(), why or f"o minisup terminou antes da hora (código {sup.proc.poll()})")
        z = zombies_of(sup.pid)
        require(not z, f"{len(z)} zumbi(s) com PPID = minisup 1,2 s depois de terminar(em): {describe_procs(z)}")
        sup.send(signal.SIGTERM)
        sup.wait_exit(6)

    deadline_ms = int(time.time() * 1000) + 1000
    burst = [(f"r{i:02d}", 0, f"exit-at {deadline_ms} 0") for i in range(30)]
    with scenario("rajada", burst) as sup:
        rc = wait_natural_end(sup, 10, "30 serviços que saem ao mesmo tempo")
        done = sum(1 for i in range(30) if (sup.summary(f"r{i:02d}") or "").endswith("saiu com código 0, reinícios: 0"))
        require(done == 30, f"só {done} de 30 serviços aparecem no resumo como 'saiu com código 0'")
        require(rc == 0, f"o minisup saiu com código {rc}; esperado 0")


def t_fds():
    with scenario("fds", [("antes", 0, "sleep"), ("fds", 0, "fds"), ("depois", 0, "sleep")]) as sup:
        require(sup.wait_line(r"^fds \| fds: fim", 8) is not None,
                f"o serviço 'fds' não chegou a listar seus descritores (minisup vivo: {sup.alive()})")
        listed = {}
        for line in sup.lines():
            m = re.match(r"^fds \| fd (\d+) -> (.*)$", line)
            if m:
                listed[int(m.group(1))] = m.group(2)
        require(1 in listed and 2 in listed and listed[1] == listed[2] and listed[1].startswith("pipe:"),
                f"stdout e stderr do serviço deveriam ser o mesmo pipe; ele viu {listed}")
        extra = sorted(fd for fd in listed if fd > 2)
        require(not extra, "o serviço herdou descritores além de 0, 1 e 2: "
                + ", ".join(f"fd {fd} -> {listed[fd]}" for fd in extra))
        sup.send(signal.SIGTERM)
        sup.wait_exit(6)


def t_status():
    with scenario("status-codigo", [("falha", 1, "exit 3"), ("ok", 3, "exit 0")]) as sup:
        rc = wait_natural_end(sup, 10, "um serviço que sai com 3 (1 reinício) e outro com 0")
        runs = sup.count(r"^falha \| start pid=")
        require(runs == 2, f"o serviço 'falha' (sai com 3, máximo 1 reinício) rodou {runs} vez(es); esperado 2")
        runs = sup.count(r"^ok \| start pid=")
        require(runs == 1, f"o serviço 'ok' (sai com 0) rodou {runs} vez(es); esperado 1 (saída 0 não é falha)")
        check_summary(sup, "falha", r"saiu com código 3, reinícios: 1$", "o resumo de 'falha' está errado")
        require(rc == 3, f"o minisup saiu com código {rc}; esperado 3 (o código do serviço 'falha')")

    with scenario("status-sinal", [("crash", 2, "crash 9")]) as sup:
        rc = wait_natural_end(sup, 10, "um serviço que morre por SIGKILL (2 reinícios)")
        runs = sup.count(r"^crash \| start pid=")
        check_summary(sup, "crash", r"morto pelo sinal 9 \(SIGKILL\), reinícios: 2$",
                      "o serviço 'crash' morreu pelo sinal 9 (SIGKILL) em todas as execuções")
        require(runs == 3, f"o serviço 'crash' rodou {runs} vez(es); esperado 3 (morrer por sinal é falha)")
        require(rc == 137, f"o minisup saiu com código {rc}; esperado 137 (128 + 9, morte por SIGKILL)")


def t_shutdown():
    with scenario("desligamento", [("educado", 0, "polite"), ("dorminhoco", 0, "sleep")]) as sup:
        require(sup.wait_line(r"^educado \| ready", 8) and sup.wait_line(r"^dorminhoco \| ready", 2),
                f"os serviços não chegaram a rodar (minisup vivo: {sup.alive()})")
        sup.send(signal.SIGTERM)
        rc = sup.wait_exit(6)
        require(rc is not None, "o minisup ainda está vivo 6 s depois do SIGTERM (carência configurada: 2 s)")
        why = exit_reason(sup, rc)
        require(why is None, why)
        require("educado | tchau 3" in sup.lines(),
                "o que o serviço 'educado' escreveu ao receber SIGTERM ('tchau 1..3') não apareceu na saída")
        check_summary(sup, "educado", r"saiu com código 0", "o serviço 'educado' saiu com 0 ao receber SIGTERM")
        check_summary(sup, "dorminhoco", r"morto pelo sinal 15 \(SIGTERM\)",
                      "o serviço 'dorminhoco' morreu pelo SIGTERM do desligamento")
        require(rc == 0, f"o minisup saiu com código {rc}; esperado 0 (desligamento limpo)")

    with scenario("sigint", [("dorminhoco", 0, "sleep")]) as sup:
        require(sup.wait_line(r"^dorminhoco \| ready", 8), "o serviço não chegou a rodar")
        sup.send(signal.SIGINT)
        rc = sup.wait_exit(6)
        require(rc is not None, "o minisup ainda está vivo 6 s depois do SIGINT (Ctrl-C)")
        require(rc == 0, exit_reason(sup, rc) or f"SIGINT: o minisup saiu com código {rc}; esperado 0")


def t_grace():
    with scenario("prazo", [("teimoso", 0, "stubborn")], grace_ms=400) as sup:
        require(sup.wait_line(r"^teimoso \| ready", 8), "o serviço não chegou a rodar")
        t0 = time.monotonic()
        sup.send(signal.SIGTERM)
        rc = sup.wait_exit(8)
        elapsed = time.monotonic() - t0
        require(rc is not None,
                "o minisup ainda está vivo 8 s depois do SIGTERM; o serviço 'teimoso' ignora SIGTERM e a carência era 0,4 s")
        why = exit_reason(sup, rc)
        require(why is None, why)
        require(elapsed >= 0.35, f"o minisup terminou {elapsed:.2f} s depois do SIGTERM, antes da carência de 0,4 s")
        check_summary(sup, "teimoso", r"morto pelo sinal 9 \(SIGKILL\)",
                      "o serviço 'teimoso' só pode ter terminado por SIGKILL")
        require(rc == 137, f"o minisup saiu com código {rc}; esperado 137 (um serviço precisou de SIGKILL)")


def t_grandchildren():
    with scenario("netos", [("familia", 0, "family")]) as sup:
        m = sup.wait_line(r"^familia \| ready neto=(\d+)", 8)
        require(m is not None, "o serviço não chegou a rodar")
        sup.send(signal.SIGTERM)
        rc = sup.wait_exit(6)
        require(rc is not None, "o minisup ainda está vivo 6 s depois do SIGTERM")
        why = exit_reason(sup, rc)
        require(why is None, why)
        left = tagged_alive(sup.tag)
        for _ in range(20):
            if not left:
                break
            time.sleep(0.1)
            left = tagged_alive(sup.tag)
        require(not left, f"processos do serviço continuam vivos 2 s depois que o minisup saiu: {describe_procs(left)}")
        require(rc == 0, f"o minisup saiu com código {rc}; esperado 0")


CRITERIA = [
    ("saída repassada linha a linha e fim natural quando todos os serviços saem", t_relay),
    ("nenhum zumbi: filhos e órfãos adotados são colhidos, inclusive em rajada", t_zombies),
    ("serviços não herdam descritores do supervisor (só 0, 1 e 2)", t_fds),
    ("status decodificado: código de saída x sinal, reinícios e código final", t_status),
    ("desligamento ordenado com SIGTERM/SIGINT, sem perder a saída final", t_shutdown),
    ("carência respeitada e SIGKILL para quem ignora SIGTERM", t_grace),
    ("nada do grupo do serviço sobrevive ao desligamento (netos)", t_grandchildren),
]


def main():
    green = "" if os.environ.get("NO_COLOR") else "\033[32m"
    red = "" if os.environ.get("NO_COLOR") else "\033[31m"
    reset = "" if os.environ.get("NO_COLOR") else "\033[0m"
    if not sys.stdout.isatty():
        green = red = reset = ""

    def ok(msg):
        print(f"  {green}✔{reset} {msg}", flush=True)

    def bad(msg, why):
        print(f"  {red}✘{reset} {msg}\n      motivo: {why}", flush=True)

    gcc = subprocess.run(["gcc", "-dumpfullversion"], capture_output=True, text=True).stdout.strip()
    print(f"==> Compilando src/ (gcc {gcc}, -std=c17 -Wall -Wextra -Wpedantic -Werror, ASan + UBSan)")
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    build = subprocess.run(["make", "--no-print-directory", "-C", ROOT, "build/minisup"],
                           capture_output=True, text=True)
    with open(os.path.join(ROOT, "build", "compile.log"), "w") as fh:
        fh.write(build.stdout + build.stderr)
    if build.returncode != 0:
        bad("o minisup compila sem avisos em C17", "erros de compilação (log completo: build/compile.log)")
        for line in (build.stdout + build.stderr).splitlines():
            if "error:" in line:
                print(f"        {line[:200]}")
        return 1
    ok("o minisup compila sem avisos em C17 (-Werror) com ASan + UBSan")

    print("==> Executando os cenários (cada um com um minisup novo; GQ_VERBOSE=1 mostra a saída)")
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
    if sanitizer_reports:
        bad("nenhum relatório do ASan/UBSan/LSan no minisup",
            "; ".join(f"[{sc}] {line}" for sc, line in sanitizer_reports[:3]))
        failed += 1
    else:
        ok("nenhum relatório do ASan/UBSan/LSan no minisup")
        passed += 1

    print()
    total = passed + failed
    if failed:
        print(f"{passed} de {total} critérios satisfeitos. Para investigar: ./start.sh (demonstração) "
              "e GQ_VERBOSE=1 ./test.sh")
        return 1
    print(f"Todos os {total} critérios satisfeitos: nenhum zumbi, nenhum órfão, nenhum descritor vazado.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
