#!/usr/bin/env bash
# Validates a challenge branch.
#
#   ./scripts/validate-branch.sh [--contract-only] [dir]
#
# 1. Contract: challenge.yaml schema, required files, README sections,
#    executable scripts, pinned base images, no committed secrets.
# 2. Behaviour (skipped with --contract-only): runs ./test.sh.
#    - If the editable files still match starter_checksum, the tests MUST fail:
#      a starter that passes means the tests do not detect the problem.
#    - Otherwise (a participant's solution) the tests MUST pass.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$HERE/lib/common.sh"
PY=$(gq_python)

contract_only=0
if [[ "${1:-}" == "--contract-only" ]]; then contract_only=1; shift; fi
dir=$(cd "${1:-.}" && pwd)
branch=${GITQUEST_BRANCH:-$(git -C "$dir" symbolic-ref --quiet --short HEAD 2>/dev/null || true)}

gq_title "Contrato do desafio ($dir)"
"$PY" "$HERE/lib/gq.py" validate "$dir" "$branch" || gq_die "contrato inválido"
gq_ok "challenge.yaml, arquivos obrigatórios e README válidos"

if command -v shellcheck >/dev/null 2>&1; then
  # Every challenge script (the vendored scripts/ are linted on main).
  mapfile -t sh_files < <(cd "$dir" && git ls-files '*.sh' | grep -v '^scripts/')
  (cd "$dir" && shellcheck -x -e SC1091 "${sh_files[@]}") && gq_ok "shellcheck sem alertas (${#sh_files[@]} scripts)"
else
  gq_warn "shellcheck não instalado; análise estática dos scripts pulada"
fi

((contract_only)) && exit 0

expected=$("$PY" "$HERE/lib/gq.py" get "$dir/challenge.yaml" starter_checksum)
actual=$("$PY" "$HERE/lib/gq.py" checksum "$dir")
timeout_s=$("$PY" "$HERE/lib/gq.py" get "$dir/challenge.yaml" validation.timeout_seconds)

run_tests() {
  local rc=0
  if command -v timeout >/dev/null 2>&1; then
    (cd "$dir" && timeout --kill-after=15 "$((timeout_s + 60))" ./test.sh) || rc=$?
  else
    (cd "$dir" && ./test.sh) || rc=$?
  fi
  return "$rc"
}

gq_title "Preparando o ambiente (./start.sh)"
(cd "$dir" && ./start.sh) || gq_die "start.sh falhou — o ambiente do desafio não pôde ser preparado"

if [[ -n "$expected" && "$expected" == "$actual" ]]; then
  gq_title "Código inicial intocado: os testes DEVEM falhar"
  if run_tests; then
    gq_die "os testes passaram com o código inicial — eles não detectam o problema do desafio"
  fi
  gq_ok "os testes reprovam o código inicial, como esperado"
else
  [[ -z "$expected" ]] && gq_warn "starter_checksum ausente; tratando o código como solução"
  gq_title "Código modificado: os testes DEVEM passar"
  run_tests || gq_die "os testes falharam"
  gq_ok "todos os testes passaram"
fi
