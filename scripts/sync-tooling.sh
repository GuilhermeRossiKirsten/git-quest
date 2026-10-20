#!/usr/bin/env bash
# Copies the shared tooling listed in config/shared-files.txt from main into
# every local desafio-* branch and commits the change there.
#
#   ./scripts/sync-tooling.sh [--dry-run] [desafio-N ...]
#
# Uses temporary worktrees, so your current checkout is never touched.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$HERE/lib/common.sh"
ROOT="$(cd "$HERE/.." && pwd)"

dry=0
if [[ "${1:-}" == "--dry-run" ]]; then dry=1; shift; fi

[[ "$(git -C "$ROOT" symbolic-ref --quiet --short HEAD)" == main ]] || gq_die "execute a partir da branch main"
[[ -z "$(git -C "$ROOT" status --porcelain -- $(grep -v '^#' "$ROOT/config/shared-files.txt"))" ]] \
  || gq_die "há alterações não commitadas nas ferramentas compartilhadas; faça commit no main antes"

mapfile -t shared < <(grep -Ev '^\s*(#|$)' "$ROOT/config/shared-files.txt")
if (($#)); then
  branches=("$@")
else
  mapfile -t branches < <(git -C "$ROOT" for-each-ref --format='%(refname:short)' 'refs/heads/desafio-*')
fi
((${#branches[@]})) || gq_die "nenhuma branch desafio-* local encontrada"

tmp=$(mktemp -d)
trap 'git -C "$ROOT" worktree prune; rm -rf "$tmp"' EXIT
main_sha=$(git -C "$ROOT" rev-parse --short HEAD)

for br in "${branches[@]}"; do
  [[ "$br" =~ ^desafio-[1-9][0-9]*$ ]] || gq_die "nome de branch inválido: $br"
  wt="$tmp/$br"
  git -C "$ROOT" worktree add --quiet "$wt" "$br"
  for f in "${shared[@]}"; do
    mkdir -p "$wt/$(dirname "$f")"
    git -C "$ROOT" show "main:$f" >"$wt/$f"
    git -C "$wt" add -- "$f"
    if [[ "$f" == scripts/gitquest || "$f" == *.sh ]]; then
      git -C "$wt" update-index --chmod=+x -- "$f"
    fi
  done
  if git -C "$wt" diff --cached --quiet; then
    gq_ok "$br já está sincronizada"
  elif ((dry)); then
    gq_info "$br seria atualizada:"
    git -C "$wt" diff --cached --stat | sed 's/^/    /'
  else
    git -C "$wt" commit --quiet -m "chore: sincroniza ferramentas compartilhadas (main@$main_sha)"
    gq_ok "$br atualizada"
  fi
  git -C "$ROOT" worktree remove --force "$wt"
done
((dry)) || echo "Publique com: git push origin ${branches[*]}"
