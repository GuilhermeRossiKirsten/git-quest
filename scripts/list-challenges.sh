#!/usr/bin/env bash
# Lists the catalog and checks it against the challenge branches that exist.
#   ./scripts/list-challenges.sh            # table
#   ./scripts/list-challenges.sh --check    # also fails if an "available"
#                                           # challenge has no branch, or a
#                                           # branch is missing from the catalog
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$HERE/lib/common.sh"
PY=$(gq_python)
CATALOG="$HERE/../config/challenge-catalog.yaml"

[[ -f "$CATALOG" ]] || gq_die "execute a partir da branch main ($CATALOG não existe)"
"$PY" "$HERE/lib/gq.py" validate-catalog "$CATALOG" || gq_die "catálogo inválido"

"$HERE/gitquest" list

[[ "${1:-}" == "--check" ]] || exit 0

branches=$(git for-each-ref --format='%(refname:lstrip=-1)' 'refs/heads/desafio-*' 'refs/remotes/origin/desafio-*' | sort -u)
fail=0
while IFS=$'\t' read -r id _ _ _ _ status _; do
  if [[ "$status" == available ]] && ! grep -qx "$id" <<<"$branches"; then
    gq_error "$id está como 'available' mas a branch não existe"; fail=1
  fi
done < <("$PY" "$HERE/lib/gq.py" catalog "$CATALOG")
for b in $branches; do
  "$PY" "$HERE/lib/gq.py" catalog "$CATALOG" "$b" | grep -q . || { gq_error "branch $b não está no catálogo"; fail=1; }
done
((fail)) && exit 1
gq_ok "catálogo e branches consistentes"
