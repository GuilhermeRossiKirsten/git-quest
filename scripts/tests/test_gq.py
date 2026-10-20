"""Unit tests for scripts/lib/gq.py (run: python3 -m unittest discover -s scripts/tests)."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "lib"))
import gq  # noqa: E402

VALID = """
id: desafio-7
title: "Título: com dois-pontos"
summary: "Resumo"
track: go
language: go
difficulty: intermediate
learning_objectives:
  - "Objetivo"
environment:
  type: docker
  requires: [docker, go]
  start_command: "./start.sh"
  test_command: "./test.sh"
validation:
  type: automated
  timeout_seconds: 300
editable:
  - src
estimated_minutes: 60
points: 200
tags: [go, concurrency]
prerequisites: []
next: [desafio-8]
"""

README = "# x\n\n" + "".join(f"## {s}\n\ntexto\n\n" for s in gq.README_SECTIONS)
HINTS = "# Dicas\n\n## Nível 1\n\na\n\n## Nível 2\n\nb\n"
SCRIPT = "#!/usr/bin/env bash\nset -euo pipefail\necho ok\n"


class ParserTest(unittest.TestCase):
    def test_parses_nested_structures(self):
        data = gq.parse_yaml(VALID)
        self.assertEqual(data["title"], "Título: com dois-pontos")
        self.assertEqual(data["environment"]["requires"], ["docker", "go"])
        self.assertEqual(data["validation"]["timeout_seconds"], 300)
        self.assertEqual(data["prerequisites"], [])

    def test_list_of_maps(self):
        data = gq.parse_yaml("items:\n  - id: a\n    n: 1\n  - id: b\n    tags: [x]\n")
        self.assertEqual(data["items"], [{"id": "a", "n": 1}, {"id": "b", "tags": ["x"]}])

    def test_rejects_unsupported_yaml(self):
        for bad in ("a: &anchor 1", "a: [1, [2]]", "a: 1\na: 2", "a:\n\tb: 1", "a: {b: 1}", "  - x\nb: 1"):
            with self.subTest(bad=bad), self.assertRaises(gq.YamlError):
                gq.parse_yaml(bad)


class MetadataTest(unittest.TestCase):
    def test_valid_metadata(self):
        self.assertEqual(gq.validate_metadata(gq.parse_yaml(VALID), "desafio-7"), [])

    def test_branch_mismatch(self):
        errors = gq.validate_metadata(gq.parse_yaml(VALID), "desafio-8")
        self.assertTrue(any("não corresponde" in e for e in errors))

    def test_cpp_requires_standard_and_toolchain(self):
        data = gq.parse_yaml(VALID.replace("language: go", "language: cpp"))
        errors = gq.validate_metadata(data)
        self.assertTrue(any("standard" in e for e in errors))
        self.assertTrue(any("toolchain" in e for e in errors))

    def test_unknown_and_missing_fields(self):
        data = gq.parse_yaml(VALID.replace("points: 200", "pontos: 200"))
        errors = gq.validate_metadata(data)
        self.assertIn("campo obrigatório ausente: points", errors)
        self.assertIn("campo desconhecido: pontos", errors)

    def test_bad_values(self):
        for old, new in (("track: go", "track: rust"), ("desafio-7", "desafio-07"),
                         ("estimated_minutes: 60", "estimated_minutes: 1"),
                         ("next: [desafio-8]", "next: [desafio-7]")):
            with self.subTest(change=new):
                self.assertNotEqual(gq.validate_metadata(gq.parse_yaml(VALID.replace(old, new))), [])


class CatalogTest(unittest.TestCase):
    def test_repository_catalog_is_valid(self):
        path = os.path.join(os.path.dirname(__file__), "..", "..", "config", "challenge-catalog.yaml")
        self.assertEqual(gq.validate_catalog(gq.load_yaml(path)), [])

    def test_docs_catalog_lists_every_challenge(self):
        root = os.path.join(os.path.dirname(__file__), "..", "..")
        data = gq.load_yaml(os.path.join(root, "config", "challenge-catalog.yaml"))
        with open(os.path.join(root, "docs", "challenge-catalog.md"), encoding="utf-8") as fh:
            doc = fh.read()
        missing = [c["id"] for c in data["challenges"] if f"`{c['id']}`" not in doc]
        self.assertEqual(missing, [], "ids ausentes em docs/challenge-catalog.md")

    def test_detects_cycles_and_unknown_prereqs(self):
        base = ("challenges:\n"
                "  - id: desafio-1\n    title: a\n    track: go\n    difficulty: beginner\n    points: 10\n"
                "    estimated_minutes: 5\n    status: available\n    prerequisites: [desafio-2]\n    summary: s\n"
                "  - id: desafio-2\n    title: b\n    track: go\n    difficulty: beginner\n    points: 10\n"
                "    estimated_minutes: 5\n    status: planned\n    prerequisites: [desafio-1]\n    summary: s\n")
        errors = gq.validate_catalog(gq.parse_yaml(base))
        self.assertTrue(any("ciclo" in e for e in errors))
        errors = gq.validate_catalog(gq.parse_yaml(base.replace("[desafio-1]", "[desafio-9]")))
        self.assertTrue(any("desconhecido" in e for e in errors))


class ProgressTest(unittest.TestCase):
    CATALOG = {"challenges": [
        {"id": "desafio-1", "track": "cpp", "points": 100, "status": "available", "prerequisites": []},
        {"id": "desafio-2", "track": "cpp", "points": 150, "status": "available", "prerequisites": ["desafio-1"]},
        {"id": "desafio-3", "track": "go", "points": 200, "status": "available", "prerequisites": ["desafio-2"]},
        {"id": "desafio-4", "track": "go", "points": 999, "status": "planned", "prerequisites": []},
    ]}

    def test_report_counts_points_and_suggests_unlocked_challenge(self):
        done = {"desafio-1": {"at": "2026-10-20T10:00:00Z", "track": "cpp", "points": "100"}}
        text = gq.progress_report(self.CATALOG, done)
        self.assertIn("1/3 desafios, 100/450 pontos", text)   # planned ones do not count
        self.assertRegex(text, r"cpp .* 1/2 .* próximo: desafio-2")
        self.assertRegex(text, r"go .* 0/1 .* próximo: -")     # desafio-3 still locked

    def test_markdown_and_tsv_loading(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "progress.tsv")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("desafio-1\t2026-10-20T10:00:00Z\tcpp\t100\tabc123\nlixo\n")
            done = gq.load_progress(path)
        self.assertEqual(list(done), ["desafio-1"])
        md = gq.progress_report(self.CATALOG, done, markdown=True)
        self.assertIn("| cpp | 1/2 | 100 | desafio-2 |", md)
        self.assertIn("- `desafio-1` — 2026-10-20T10:00:00Z (+100)", md)


class CliOutputTest(unittest.TestCase):
    def test_output_uses_lf_on_every_platform(self):
        # Bash reads this output with mapfile; a trailing \r breaks path matching on Windows.
        catalog = os.path.join(os.path.dirname(__file__), "..", "..", "config", "challenge-catalog.yaml")
        script = os.path.join(os.path.dirname(__file__), "..", "lib", "gq.py")
        out = subprocess.run([sys.executable, script, "catalog", catalog], capture_output=True, check=True).stdout
        self.assertNotIn(b"\r", out)
        self.assertIn("não".encode("utf-8"), out)


class TreeTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.write("challenge.yaml", VALID)
        self.write("README.md", README)
        self.write("docs/hints.md", HINTS)
        self.write("docs/learning-objectives.md", "# Objetivos\n")
        self.write("start.sh", SCRIPT)
        self.write("test.sh", SCRIPT)
        self.write("src/main.go", "package main\n")
        self.write("Dockerfile", "FROM golang:1.25.3-bookworm\n")
        subprocess.run(["git", "init", "-q", self.dir], check=True)
        subprocess.run(["git", "-C", self.dir, "config", "core.autocrlf", "false"], check=True)
        subprocess.run(["git", "-C", self.dir, "add", "-A"], check=True)
        subprocess.run(["git", "-C", self.dir, "update-index", "--chmod=+x", "start.sh", "test.sh"], check=True)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, rel, text):
        path = os.path.join(self.dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)

    def errors(self):
        subprocess.run(["git", "-C", self.dir, "add", "-A"], check=True)
        return gq.validate_tree(self.dir, "desafio-7")[0]

    def test_valid_tree(self):
        self.assertEqual(self.errors(), [])

    def test_unpinned_base_image(self):
        self.write("Dockerfile", "FROM golang AS build\nFROM alpine:latest\n")
        self.assertEqual(len([e for e in self.errors() if "sem versão fixada" in e]), 2)

    def test_missing_readme_section_and_hints(self):
        self.write("README.md", "# x\n\n## Contexto\n")
        self.write("docs/hints.md", "# Dicas\n\n## Nível 2\n")
        errors = self.errors()
        self.assertTrue(any("Critérios de conclusão" in e for e in errors))
        self.assertTrue(any("sequenciais" in e for e in errors))

    def test_secret_and_editable_tests(self):
        self.write("src/key.pem", "-----BEGIN RSA PRIVATE KEY-----\nabc\n")
        self.write("challenge.yaml", VALID.replace("  - src\n", "  - src\n  - tests\n"))
        self.write("tests/a_test.go", "package a\n")
        errors = self.errors()
        self.assertTrue(any("segredo" in e for e in errors))
        self.assertTrue(any("não pode ser editável" in e for e in errors))

    def test_crlf_script(self):
        self.write("start.sh", SCRIPT.replace("\n", "\r\n"))
        with open(os.path.join(self.dir, ".gitattributes"), "w") as fh:
            fh.write("* -text\n")
        self.assertTrue(any("CRLF" in e for e in self.errors()))

    def test_checksum_tracks_editable_files_only(self):
        before = gq.checksum(self.dir, ["src"])
        self.write("README.md", README + "\nmais\n")
        subprocess.run(["git", "-C", self.dir, "add", "-A"], check=True)
        self.assertEqual(before, gq.checksum(self.dir, ["src"]))
        self.write("src/main.go", "package main\n// solução\n")
        self.assertNotEqual(before, gq.checksum(self.dir, ["src"]))


if __name__ == "__main__":
    unittest.main()
