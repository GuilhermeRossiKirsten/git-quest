#!/usr/bin/env bash
# Reports which tools are available and which challenge tracks they unlock.
# Never installs anything. Exit code 0 when the minimum (git, bash, python3)
# is present, 1 otherwise.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$HERE/lib/common.sh"

missing_core=0

check() {
  # check <name> <min-version> <version-command...>
  local name=$1 min=$2
  shift 2
  if ! command -v "$name" >/dev/null 2>&1; then
    printf '  %-10s %sausente%s\n' "$name" "$GQ_YELLOW" "$GQ_RESET"
    return 1
  fi
  local v
  v=$("$@" 2>/dev/null | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 || true)
  if [[ -n "$min" ]] && ! gq_version_ge "${v:-0}" "$min"; then
    printf '  %-10s %s%s (mínimo %s)%s\n' "$name" "$GQ_YELLOW" "${v:-?}" "$min" "$GQ_RESET"
    return 1
  fi
  printf '  %-10s %s%s%s\n' "$name" "$GQ_GREEN" "${v:-ok}" "$GQ_RESET"
}

gq_title "Sistema: $(gq_os) ($(uname -m))"
[[ "$(gq_os)" == windows ]] && gq_warn "Git Bash funciona para a maioria dos desafios; WSL2 é o ambiente recomendado (docs/getting-started.md)."

gq_title "Essenciais"
check git 2.23 git --version || missing_core=1
check bash 4.0 bash --version || missing_core=1
if ! check python3 3.8 python3 --version; then
  check python 3.8 python --version || missing_core=1
fi

gq_title "Por trilha"
docker_ok=0
if check docker 20.10 docker version --format '{{.Client.Version}}'; then
  if docker info >/dev/null 2>&1; then docker_ok=1; else gq_warn "o daemon do Docker não está rodando"; fi
fi
check go 1.22 go version || true
check g++ 11 g++ -dumpfullversion || true
check make 3.81 make --version || true
check kubectl 1.29 kubectl version --client || true
check kind 0.20 kind version || true
check shellcheck 0.7 shellcheck --version || true

gq_title "Resumo"
if ((docker_ok)); then
  echo "  C++, estruturas de dados, Go e Docker: prontos via Docker (nenhuma instalação local exigida)."
  echo "  Kubernetes: o start.sh do desafio baixa kind/kubectl fixados para ./.tools se faltarem."
else
  echo "  Sem Docker: apenas desafios com 'environment.type: local' (ex.: Go com Go instalado)."
fi
((missing_core)) && gq_die "faltam ferramentas essenciais (veja acima)"
gq_ok "ambiente mínimo presente"
