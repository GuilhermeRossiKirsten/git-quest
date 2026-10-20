#!/usr/bin/env bash
# Avaliação propriamente dita. Roda DENTRO do container de toolchain (via
# ./test.sh) ou na sua máquina com GITQUEST_LOCAL=1 ./test.sh.
set -uo pipefail
cd "$(dirname "$0")/.."
mkdir -p build

failed=0
echo "==> go build ./..."
if go build ./... 2>build/build.log; then
  echo "  ✔ o código compila"
else
  echo "  ✘ o código compila"
  sed 's/^/      /' build/build.log
  exit 1
fi

echo "==> go vet ./..."
if go vet ./... 2>build/vet.log; then
  echo "  ✔ go vet sem alertas"
else
  failed=1
  echo "  ✘ go vet sem alertas"
  grep -v '^#' build/vet.log | sed 's/^/      /'
fi

echo "==> go test -race -count=1 -timeout 60s ./tests/"
go test -race -count=1 -timeout 60s -json ./tests/ >build/test.json 2>&1
go build -o build/report ./tests/report && build/report <build/test.json || failed=1

exit "$failed"
