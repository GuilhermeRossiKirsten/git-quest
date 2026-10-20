#!/usr/bin/env python3
"""GitQuest helper: YAML-subset parser, challenge.yaml validator, catalog and checksum.

Only the Python standard library is used (Python >= 3.8), so the CLI works on a
fresh machine. The YAML subset is documented in docs/challenge-format.md:
block maps, block lists, flow lists of scalars ([a, b]), quoted/bare scalars,
integers, booleans and full-line comments. Anything else is rejected loudly
instead of being misread.
"""
import fnmatch
import hashlib
import os
import re
import subprocess
import sys

# --------------------------------------------------------------------------
# YAML subset parser
# --------------------------------------------------------------------------


class YamlError(Exception):
    pass


def _scalar(raw, lineno):
    raw = raw.strip()
    if raw == "":
        return None
    if raw[0] in "\"'":
        q = raw[0]
        if len(raw) < 2 or raw[-1] != q:
            raise YamlError(f"linha {lineno}: string sem aspas de fechamento: {raw}")
        body = raw[1:-1]
        if q == '"':
            body = body.replace('\\"', '"').replace("\\\\", "\\")
        return body
    if raw.startswith("[") or raw.startswith("{"):
        if raw == "[]":
            return []
        if raw.startswith("[") and raw.endswith("]"):
            return [_scalar(p, lineno) for p in _split_flow(raw[1:-1], lineno)]
        raise YamlError(f"linha {lineno}: coleções flow só são aceitas como [a, b]: {raw}")
    if " #" in raw:
        raw = raw.split(" #", 1)[0].rstrip()
    if raw in ("true", "false"):
        return raw == "true"
    if raw in ("null", "~"):
        return None
    if re.fullmatch(r"-?\d+", raw):
        return int(raw)
    if raw[0] in "&*!|>%@`":
        raise YamlError(f"linha {lineno}: recurso YAML não suportado: {raw}")
    return raw


def _split_flow(body, lineno):
    parts, cur, quote = [], "", None
    for ch in body:
        if quote:
            cur += ch
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
            cur += ch
        elif ch == ",":
            parts.append(cur)
            cur = ""
        elif ch in "[]{}":
            raise YamlError(f"linha {lineno}: listas aninhadas não são suportadas")
        else:
            cur += ch
    if quote:
        raise YamlError(f"linha {lineno}: aspas não fechadas")
    if cur.strip():
        parts.append(cur)
    return [p for p in parts if p.strip()]


_KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_-]*):(?:\s+(.*))?$")


def parse_yaml(text):
    lines = []
    for n, line in enumerate(text.splitlines(), 1):
        if "\t" in line[: len(line) - len(line.lstrip())]:
            raise YamlError(f"linha {n}: use espaços, não tabs, para indentar")
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped == "---":
            continue
        lines.append((len(line) - len(line.lstrip(" ")), stripped, n))
    value, pos = _parse_block(lines, 0, 0)
    if pos != len(lines):
        raise YamlError(f"linha {lines[pos][2]}: indentação inesperada")
    return value if value is not None else {}


def _parse_block(lines, pos, indent):
    if pos >= len(lines):
        return None, pos
    if lines[pos][1].startswith("- ") or lines[pos][1] == "-":
        return _parse_list(lines, pos, lines[pos][0])
    return _parse_map(lines, pos, lines[pos][0])


def _parse_map(lines, pos, indent):
    result = {}
    while pos < len(lines):
        ind, text, n = lines[pos]
        if ind < indent:
            break
        if ind > indent:
            raise YamlError(f"linha {n}: indentação inesperada")
        if text.startswith("- "):
            break
        m = _KEY_RE.match(text)
        if not m:
            raise YamlError(f"linha {n}: esperado 'chave: valor', encontrado: {text}")
        key, raw = m.group(1), m.group(2)
        if key in result:
            raise YamlError(f"linha {n}: chave duplicada '{key}'")
        pos += 1
        if raw is None or raw.strip() == "" or raw.strip().startswith("#"):
            if pos < len(lines) and (lines[pos][0] > ind or
                                     (lines[pos][0] == ind and lines[pos][1].startswith("- "))):
                child, pos = _parse_block(lines, pos, lines[pos][0])
                result[key] = child
            else:
                result[key] = None
        else:
            result[key] = _scalar(raw, n)
    return result, pos


def _parse_list(lines, pos, indent):
    result = []
    while pos < len(lines):
        ind, text, n = lines[pos]
        if ind < indent or not (text.startswith("- ") or text == "-"):
            if ind > indent:
                raise YamlError(f"linha {n}: indentação inesperada")
            break
        if ind > indent:
            raise YamlError(f"linha {n}: indentação inesperada")
        item = text[1:].strip()
        if _KEY_RE.match(item):
            # A map inside a list: rewrite "- key: v" as a map line at indent+2.
            child_indent = ind + 2
            sub = [(child_indent, item, n)]
            pos += 1
            while pos < len(lines) and lines[pos][0] >= child_indent:
                sub.append(lines[pos])
                pos += 1
            value, used = _parse_map(sub, 0, child_indent)
            if used != len(sub):
                raise YamlError(f"linha {sub[used][2]}: indentação inesperada")
            result.append(value)
        else:
            result.append(_scalar(item, n))
            pos += 1
    return result, pos


def load_yaml(path):
    with open(path, encoding="utf-8") as fh:
        return parse_yaml(fh.read())


# --------------------------------------------------------------------------
# challenge.yaml schema
# --------------------------------------------------------------------------

TRACKS = ["cpp", "dsa", "go", "docker", "kubernetes", "linux", "network", "database"]
DIFFICULTIES = ["beginner", "intermediate", "advanced", "expert"]
STANDARDS = ["cpp11", "cpp14", "cpp17", "cpp20", "cpp23", "cpp26"]
ENV_TYPES = ["docker", "local", "kind"]
ID_RE = re.compile(r"^desafio-[1-9][0-9]*$")

# field -> (type, required)
SCHEMA = {
    "id": (str, True),
    "title": (str, True),
    "summary": (str, True),
    "track": (str, True),
    "language": (str, True),
    "standard": (str, False),
    "toolchain": (str, False),
    "difficulty": (str, True),
    "learning_objectives": (list, True),
    "environment": (dict, True),
    "validation": (dict, True),
    "editable": (list, True),
    "estimated_minutes": (int, True),
    "points": (int, True),
    "tags": (list, True),
    "prerequisites": (list, True),
    "next": (list, False),
    "achievements": (list, False),
    "starter_checksum": (str, False),
}
ENV_SCHEMA = {
    "type": (str, True),
    "requires": (list, True),
    "start_command": (str, True),
    "test_command": (str, True),
}
VALIDATION_SCHEMA = {
    "type": (str, True),
    "timeout_seconds": (int, True),
    "hidden_tests": (bool, False),
}

REQUIRED_FILES = ["README.md", "challenge.yaml", "start.sh", "test.sh",
                  "docs/learning-objectives.md", "docs/hints.md"]

# Headings every challenge README must contain (case-insensitive prefix match).
README_SECTIONS = [
    "Contexto", "Problema", "Objetivos", "Conhecimentos necessários",
    "Ambiente", "Como começar", "Arquivos que você pode modificar",
    "Como testar", "Critérios de conclusão", "Dicas", "Referências",
    "Próximo desafio",
]

SECRET_PATTERNS = [
    (re.compile(r"AKIA[0-9A-Z]{16}"), "chave de acesso AWS"),
    (re.compile(r"-----BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"), "chave privada"),
    (re.compile(r"ghp_[A-Za-z0-9]{36}"), "token do GitHub"),
    (re.compile(r"xox[baprs]-[A-Za-z0-9-]{10,}"), "token do Slack"),
]


def _check_fields(data, schema, prefix, errors):
    for key, (typ, required) in schema.items():
        if key not in data or data[key] is None:
            if required:
                errors.append(f"campo obrigatório ausente: {prefix}{key}")
            continue
        val = data[key]
        if typ is int and isinstance(val, bool):
            errors.append(f"{prefix}{key}: esperado inteiro")
        elif not isinstance(val, typ):
            errors.append(f"{prefix}{key}: esperado {typ.__name__}, encontrado {type(val).__name__}")
    for key in data:
        if key not in schema:
            errors.append(f"campo desconhecido: {prefix}{key}")


def validate_metadata(data, branch=None):
    errors = []
    if not isinstance(data, dict):
        return ["challenge.yaml deve ser um mapa no nível superior"]
    _check_fields(data, SCHEMA, "", errors)
    if errors:
        return errors
    if not ID_RE.match(data["id"]):
        errors.append(f"id inválido '{data['id']}': use desafio-N (N >= 1, sem zeros à esquerda)")
    if branch and ID_RE.match(branch) and branch != data["id"]:
        errors.append(f"id '{data['id']}' não corresponde à branch '{branch}'")
    if data["track"] not in TRACKS:
        errors.append(f"track '{data['track']}' inválida; opções: {', '.join(TRACKS)}")
    if data["difficulty"] not in DIFFICULTIES:
        errors.append(f"difficulty inválida; opções: {', '.join(DIFFICULTIES)}")
    if data["language"] == "cpp":
        if data.get("standard") not in STANDARDS:
            errors.append(f"desafios C++ exigem 'standard' em: {', '.join(STANDARDS)}")
        if not data.get("toolchain"):
            errors.append("desafios C++ exigem 'toolchain' (ex.: gcc:14.2.0)")
    elif data.get("standard") is not None and data["language"] != "cpp":
        errors.append("'standard' só se aplica a language: cpp")
    for key in ("learning_objectives", "editable", "tags"):
        if not data[key]:
            errors.append(f"{key} não pode ser vazio")
        elif not all(isinstance(x, str) and x for x in data[key]):
            errors.append(f"{key} deve conter apenas strings não vazias")
    for key in ("prerequisites", "next"):
        for ref in data.get(key) or []:
            if not isinstance(ref, str) or not ID_RE.match(ref):
                errors.append(f"{key}: referência inválida '{ref}'")
            elif ref == data["id"]:
                errors.append(f"{key}: um desafio não pode referenciar a si mesmo")
    if not 5 <= data["estimated_minutes"] <= 600:
        errors.append("estimated_minutes deve estar entre 5 e 600")
    if not 10 <= data["points"] <= 1000:
        errors.append("points deve estar entre 10 e 1000")
    env = data["environment"]
    _check_fields(env, ENV_SCHEMA, "environment.", errors)
    if isinstance(env.get("type"), str) and env["type"] not in ENV_TYPES:
        errors.append(f"environment.type inválido; opções: {', '.join(ENV_TYPES)}")
    val = data["validation"]
    _check_fields(val, VALIDATION_SCHEMA, "validation.", errors)
    if val.get("type") not in (None, "automated"):
        errors.append("validation.type suportado: automated")
    if isinstance(val.get("timeout_seconds"), int) and not 10 <= val["timeout_seconds"] <= 3600:
        errors.append("validation.timeout_seconds deve estar entre 10 e 3600")
    cs = data.get("starter_checksum")
    if cs is not None and not re.fullmatch(r"sha256:[0-9a-f]{64}", cs):
        errors.append("starter_checksum deve ter o formato sha256:<64 hex>")
    return errors


def git_ls_files(root):
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True)
        return [p for p in out.stdout.decode("utf-8").split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for base, dirs, names in os.walk(root):
            dirs[:] = [d for d in dirs if d not in (".git", "build", ".tools")]
            for name in names:
                files.append(os.path.relpath(os.path.join(base, name), root).replace(os.sep, "/"))
        return files


def git_file_mode(root, path):
    out = subprocess.run(["git", "-C", root, "ls-files", "-s", "--", path],
                         capture_output=True, text=True)
    return out.stdout.split(" ", 1)[0] if out.stdout else None


def match_editable(patterns, files):
    picked = set()
    for pat in patterns:
        pat = pat.rstrip("/")
        for f in files:
            if f == pat or f.startswith(pat + "/") or fnmatch.fnmatch(f, pat):
                picked.add(f)
    return sorted(picked)


def checksum(root, patterns):
    """sha256 over the editable files (LF-normalised) — identifies the pristine starter."""
    h = hashlib.sha256()
    for rel in match_editable(patterns, git_ls_files(root)):
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as fh:
            body = fh.read().replace(b"\r\n", b"\n")
        h.update(rel.encode() + b"\0" + body + b"\0")
    return "sha256:" + h.hexdigest()


def validate_tree(root, branch=None):
    """Validate the whole challenge contract. Returns (errors, warnings)."""
    errors, warnings = [], []
    meta_path = os.path.join(root, "challenge.yaml")
    if not os.path.isfile(meta_path):
        return ["challenge.yaml não encontrado — esta branch não parece ser um desafio"], []
    try:
        data = load_yaml(meta_path)
    except YamlError as exc:
        return [f"challenge.yaml: {exc}"], []
    errors += validate_metadata(data, branch)
    files = git_ls_files(root)
    for req in REQUIRED_FILES:
        if not os.path.isfile(os.path.join(root, req)):
            errors.append(f"arquivo obrigatório ausente: {req}")
    for script in ("start.sh", "test.sh"):
        p = os.path.join(root, script)
        if not os.path.isfile(p):
            continue
        with open(p, "rb") as fh:
            body = fh.read()
        if b"\r\n" in body:
            errors.append(f"{script} contém CRLF — o Bash não executa scripts com CRLF")
        if not body.startswith(b"#!/usr/bin/env bash"):
            errors.append(f"{script} deve começar com '#!/usr/bin/env bash'")
        if b"set -euo pipefail" not in body:
            errors.append(f"{script} deve usar 'set -euo pipefail'")
        mode = git_file_mode(root, script)
        if mode and mode != "100755":
            errors.append(f"{script} não é executável no Git (rode: git update-index --chmod=+x {script})")
    readme = os.path.join(root, "README.md")
    if os.path.isfile(readme):
        with open(readme, encoding="utf-8") as fh:
            headings = [l.lstrip("#").strip().lower() for l in fh if l.startswith("## ")]
        for sec in README_SECTIONS:
            if not any(h.startswith(sec.lower()) for h in headings):
                errors.append(f"README.md: seção obrigatória ausente: '## {sec}'")
    hints = os.path.join(root, "docs", "hints.md")
    if os.path.isfile(hints):
        with open(hints, encoding="utf-8") as fh:
            levels = re.findall(r"^## Nível (\d+)", fh.read(), re.M)
        if not levels:
            errors.append("docs/hints.md: nenhuma seção '## Nível N' encontrada")
        elif levels != [str(i) for i in range(1, len(levels) + 1)]:
            errors.append("docs/hints.md: níveis devem ser sequenciais a partir de 1")
    if isinstance(data, dict) and isinstance(data.get("editable"), list):
        for pat in data["editable"]:
            if not match_editable([pat], files):
                errors.append(f"editable: '{pat}' não corresponde a nenhum arquivo versionado")
            if pat.rstrip("/") in ("challenge.yaml", "test.sh", "tests") or pat.startswith("tests/"):
                errors.append(f"editable: '{pat}' não pode ser editável (faz parte do avaliador)")
    for rel in files:
        path = os.path.join(root, rel)
        base = os.path.basename(rel)
        if base == "Dockerfile" or base.startswith("Dockerfile."):
            errors += _check_dockerfile(path, rel)
        if os.path.isfile(path) and os.path.getsize(path) < 2_000_000:
            with open(path, "rb") as fh:
                text = fh.read().decode("utf-8", "ignore")
            for rx, what in SECRET_PATTERNS:
                if rx.search(text):
                    errors.append(f"{rel}: possível segredo versionado ({what})")
            if text == "" and base != ".gitkeep":
                warnings.append(f"{rel}: arquivo vazio")
    if isinstance(data, dict) and data.get("environment", {}).get("type") == "docker" \
            and not any(os.path.basename(f) == "Dockerfile" for f in files) \
            and data.get("track") != "docker":
        warnings.append("environment.type=docker mas nenhum Dockerfile foi encontrado")
    return errors, warnings


def _check_dockerfile(path, rel):
    errors = []
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            m = re.match(r"^\s*FROM\s+(?:--platform=\S+\s+)?(\S+)", line, re.I)
            if not m:
                continue
            image = m.group(1)
            if image.lower() == "scratch" or "$" in image:
                continue
            name = image.split("@")[0]
            tag = name.rsplit(":", 1)[1] if ":" in name.split("/")[-1] else None
            if "@sha256:" not in image and (tag is None or tag == "latest"):
                errors.append(f"{rel}:{n}: imagem base sem versão fixada ({image})")
    return errors


# --------------------------------------------------------------------------
# Catalog
# --------------------------------------------------------------------------

CATALOG_FIELDS = ["id", "title", "track", "difficulty", "points", "estimated_minutes",
                  "status", "prerequisites", "summary"]


def validate_catalog(data):
    errors = []
    items = data.get("challenges") if isinstance(data, dict) else None
    if not isinstance(items, list):
        return ["catálogo deve conter a lista 'challenges'"]
    seen = set()
    for i, ch in enumerate(items):
        where = f"challenges[{i}]"
        if not isinstance(ch, dict):
            errors.append(f"{where}: esperado mapa")
            continue
        for f in CATALOG_FIELDS:
            if f not in ch:
                errors.append(f"{where}: campo ausente '{f}'")
        cid = ch.get("id")
        if not isinstance(cid, str) or not ID_RE.match(cid):
            errors.append(f"{where}: id inválido {cid!r}")
            continue
        if cid in seen:
            errors.append(f"{where}: id duplicado {cid}")
        seen.add(cid)
        if ch.get("track") not in TRACKS:
            errors.append(f"{cid}: track inválida")
        if ch.get("difficulty") not in DIFFICULTIES:
            errors.append(f"{cid}: difficulty inválida")
        if ch.get("status") not in ("available", "planned"):
            errors.append(f"{cid}: status deve ser available ou planned")
    for ch in items:
        if isinstance(ch, dict):
            for p in ch.get("prerequisites") or []:
                if p not in seen:
                    errors.append(f"{ch.get('id')}: pré-requisito desconhecido {p}")
    errors += _check_cycles(items)
    return errors


def _check_cycles(items):
    graph = {c["id"]: list(c.get("prerequisites") or []) for c in items
             if isinstance(c, dict) and isinstance(c.get("id"), str)}
    state, errors = {}, []

    def visit(node, stack):
        if state.get(node) == 1:
            errors.append("ciclo de pré-requisitos: " + " -> ".join(stack + [node]))
            return
        if state.get(node) == 2 or node not in graph:
            return
        state[node] = 1
        for nxt in graph[node]:
            visit(nxt, stack + [node])
        state[node] = 2

    for node in graph:
        visit(node, [])
    return errors


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _get(data, dotted):
    cur = data
    for part in dotted.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


def _print_value(val):
    if isinstance(val, list):
        for item in val:
            print(item)
    elif isinstance(val, bool):
        print("true" if val else "false")
    elif val is not None:
        print(val)


def main(argv):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            # LF even on Windows: Bash callers read this output with mapfile/read.
            stream.reconfigure(encoding="utf-8", newline="\n")
    if not argv:
        print("uso: gq.py <validate|validate-catalog|get|catalog|checksum> ...", file=sys.stderr)
        return 2
    cmd, args = argv[0], argv[1:]
    try:
        if cmd == "validate":
            root = args[0] if args else "."
            branch = args[1] if len(args) > 1 else None
            errors, warnings = validate_tree(root, branch)
            for w in warnings:
                print(f"aviso: {w}")
            for e in errors:
                print(f"erro: {e}")
            return 1 if errors else 0
        if cmd == "validate-catalog":
            errors = validate_catalog(load_yaml(args[0]))
            for e in errors:
                print(f"erro: {e}")
            return 1 if errors else 0
        if cmd == "get":
            # get <file> <dotted.key> — prints scalars, or one list item per line
            _print_value(_get(load_yaml(args[0]), args[1]))
            return 0
        if cmd == "catalog":
            # catalog <file> [id] — TSV rows: id track difficulty points minutes status title
            data = load_yaml(args[0])
            for ch in data.get("challenges") or []:
                if len(args) > 1 and ch.get("id") != args[1]:
                    continue
                if len(args) > 1:
                    for key in CATALOG_FIELDS:
                        val = ch.get(key)
                        if isinstance(val, list):
                            val = ", ".join(val) if val else "-"
                        print(f"{key}\t{val}")
                else:
                    print("\t".join(str(ch.get(k, "")) for k in
                                    ("id", "track", "difficulty", "points",
                                     "estimated_minutes", "status", "title")))
            return 0
        if cmd == "checksum":
            root = args[0] if args else "."
            data = load_yaml(os.path.join(root, "challenge.yaml"))
            print(checksum(root, data.get("editable") or []))
            return 0
    except (YamlError, OSError, IndexError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 2
    print(f"comando desconhecido: {cmd}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
