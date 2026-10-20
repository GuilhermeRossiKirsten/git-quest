#!/usr/bin/env python3
"""Scan every layer and the config of an image produced by `docker save`.

    docker save IMAGE | python3 tests/inspect_image.py SECRET MARKER

Reads the tar stream from stdin (classic and OCI layouts, plain or gzip layers)
and reports, without extracting anything to disk:
  secret  files (or image config/history) containing SECRET
  git     any path with a `.git` component
  source  files containing MARKER (a comment present only in the service source)

Every layer counts, not only the final filesystem: a file deleted in a later
layer is still shipped to whoever pulls the image.
Exit status: 0 = clean, 1 = findings, 2 = usage/format error.
"""
import sys
import tarfile

CHUNK = 1 << 20


def contains(fobj, needles):
    """Return the subset of needles found in fobj (streamed, chunk overlap-safe)."""
    found, tail = set(), b""
    keep = max(len(n) for n in needles) - 1
    while True:
        block = fobj.read(CHUNK)
        if not block:
            return found
        data = tail + block
        for n in needles:
            if n in data:
                found.add(n)
        tail = data[-keep:] if keep > 0 else b""


class Prefixed:
    """File-like object that replays already-read bytes before the rest of fobj."""

    def __init__(self, head, fobj):
        self.head, self.fobj = head, fobj

    def read(self, size=-1):
        if self.head:
            if size is None or size < 0:
                out, self.head = self.head + self.fobj.read(), b""
                return out
            out, self.head = self.head[:size], self.head[size:]
            if len(out) < size:
                out += self.fobj.read(size - len(out))
            return out
        return self.fobj.read(size)


def is_git_path(name):
    return ".git" in name.split("/")


def scan_layer(fobj, secret, marker, findings):
    try:
        inner = tarfile.open(fileobj=fobj, mode="r|*")
    except tarfile.TarError:
        return False
    with inner:
        for m in inner:
            path = m.name.lstrip("./")
            if is_git_path(m.name):
                findings.append(("git", f"/{path}"))
            if not m.isfile():
                continue
            f = inner.extractfile(m)
            if f is None:
                continue
            for n in contains(f, [secret, marker]):
                findings.append(("secret" if n == secret else "source", f"/{path}"))
    return True


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    secret, marker = argv[1].encode(), argv[2].encode()
    findings = []
    try:
        outer = tarfile.open(fileobj=sys.stdin.buffer, mode="r|")
        for m in outer:
            if not m.isfile():
                continue
            f = outer.extractfile(m)
            if f is None:
                continue
            head = f.read(512)
            if head.lstrip()[:1] in (b"{", b"["):
                # JSON (manifest/config): carries ENV, LABEL and the build history (RUN/ARG lines).
                if secret in head + f.read():
                    findings.append(("secret", f"metadados da imagem ({m.name})"))
                continue
            scan_layer(Prefixed(head, f), secret, marker, findings)
    except tarfile.TarError as exc:
        print(f"erro lendo a saída de docker save: {exc}", file=sys.stderr)
        return 2

    seen = set()
    for kind, where in findings:
        if (kind, where) not in seen:
            seen.add((kind, where))
            print(f"{kind}\t{where}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
