#!/usr/bin/env bash
# Creates the skeleton of a new challenge on its own orphan branch.
#
#   ./scripts/create-challenge.sh 33 --title "Deadlock no worker pool" \
#       --language cpp --standard cpp20 --difficulty advanced [--track cpp] \
#       [--dir ../gitquest-desafio-33] [--force]
#
# The branch is checked out in a separate worktree (default ../gitquest-desafio-N)
# so the main checkout is untouched. Nothing is committed or pushed.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$HERE/lib/common.sh"
ROOT="$(cd "$HERE/.." && pwd)"
PY=$(gq_python)

usage() {
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
}

[[ $# -ge 1 ]] || usage
num=$1
shift
[[ "$num" =~ ^[1-9][0-9]*$ ]] || { gq_error "o número deve ser um inteiro positivo sem zeros à esquerda: '$num'"; usage; }
id="desafio-$num"

title="" language="" standard="" difficulty="" track="" dir="" force=0
while (($#)); do
  case "$1" in
    --title) title=${2:-}; shift 2 ;;
    --language) language=${2:-}; shift 2 ;;
    --standard) standard=${2:-}; shift 2 ;;
    --difficulty) difficulty=${2:-}; shift 2 ;;
    --track) track=${2:-}; shift 2 ;;
    --dir) dir=${2:-}; shift 2 ;;
    --force) force=1; shift ;;
    -h|--help) usage ;;
    *) gq_error "opção desconhecida: $1"; usage ;;
  esac
done

[[ -n "$title" ]] || gq_die "--title é obrigatório"
[[ "$title" != *'"'* ]] || gq_die "--title não pode conter aspas duplas"
case "$language" in
  cpp|go|bash|python|yaml|sql) ;;
  *) gq_die "--language deve ser um de: cpp go bash python yaml sql" ;;
esac
case "$difficulty" in
  beginner|intermediate|advanced|expert) ;;
  *) gq_die "--difficulty deve ser um de: beginner intermediate advanced expert" ;;
esac
if [[ "$language" == cpp ]]; then
  [[ "$standard" =~ ^cpp(11|14|17|20|23|26)$ ]] || gq_die "--standard (cpp11..cpp26) é obrigatório para C++"
elif [[ -n "$standard" ]]; then
  gq_die "--standard só se aplica a --language cpp"
fi
if [[ -z "$track" ]]; then
  case "$language" in cpp) track=cpp ;; go) track=go ;; *) gq_die "informe --track para a linguagem $language" ;; esac
fi
case "$track" in
  cpp|dsa|go|docker|kubernetes|linux|network|database) ;;
  *) gq_die "--track inválida: $track" ;;
esac

case "$language" in
  cpp) base="gcc:14.2.0" ;;
  go) base="golang:1.25.3-bookworm" ;;
  *) base="debian:bookworm-slim" ;;
esac

# Conflicts: existing branches (local or remote) and catalog entries.
if git -C "$ROOT" show-ref --verify --quiet "refs/heads/$id" \
   || git -C "$ROOT" show-ref --verify --quiet "refs/remotes/origin/$id"; then
  gq_die "a branch $id já existe; escolha outro número"
fi
if [[ -f "$ROOT/config/challenge-catalog.yaml" ]] && grep -qE "^\s*- id: $id\s*$" "$ROOT/config/challenge-catalog.yaml"; then
  gq_warn "$id já está no catálogo; o skeleton será criado mesmo assim"
fi

dir=${dir:-$ROOT/../gitquest-$id}
if [[ -e "$dir" && -n "$(ls -A "$dir" 2>/dev/null)" ]]; then
  ((force)) || gq_die "o diretório $dir já existe e não está vazio (use --force para confirmar a sobrescrita)"
  if [[ -t 0 ]]; then
    read -r -p "Sobrescrever arquivos em $dir? Digite '$id' para confirmar: " answer
    [[ "$answer" == "$id" ]] || gq_die "operação cancelada"
  else
    gq_die "--force exige confirmação interativa; nenhum arquivo foi alterado"
  fi
fi

gq_info "Criando worktree $dir com a branch órfã $id"
git -C "$ROOT" worktree add --quiet --detach "$dir" HEAD
git -C "$dir" switch --quiet --orphan "$id"
dir=$(cd "$dir" && pwd)

cp -R "$ROOT/templates/challenge/." "$dir/"
std_line="" tool_line=""
if [[ "$language" == cpp ]]; then
  std_line="standard: $standard"
  tool_line="toolchain: \"$base\""
fi
while IFS= read -r -d '' f; do
  "$PY" - "$f" "$id" "$title" "$track" "$language" "$difficulty" "$std_line" "$tool_line" "$base" <<'PYEOF'
import sys
path, cid, title, track, lang, diff, std, tool, base = sys.argv[1:]
with open(path, encoding="utf-8") as fh:
    text = fh.read()
for k, v in {"__ID__": cid, "__TITLE__": title, "__TRACK__": track, "__LANGUAGE__": lang,
             "__DIFFICULTY__": diff, "__BASE_IMAGE__": base}.items():
    text = text.replace(k, v)
lines = []
for line in text.splitlines(keepends=True):
    if line.startswith("__STANDARD_LINE__"):
        line = std + "\n" if std else ""
    elif line.startswith("__TOOLCHAIN_LINE__"):
        line = tool + "\n" if tool else ""
    lines.append(line)
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("".join(lines))
PYEOF
done < <(find "$dir" -path "$dir/.git" -prune -o -type f -print0)

while IFS= read -r f; do
  [[ "$f" =~ ^\s*(#|$) ]] && continue
  mkdir -p "$dir/$(dirname "$f")"
  cp "$ROOT/$f" "$dir/$f"
done <"$ROOT/config/shared-files.txt"

git -C "$dir" add -A
git -C "$dir" update-index --chmod=+x start.sh test.sh scripts/gitquest scripts/validate-branch.sh scripts/check-environment.sh

gq_ok "skeleton de $id criado em $dir"
cat <<EOF

Próximos passos (docs/contributing-challenges.md):
  cd $dir
  # implemente src/, tests/, README.md, docs/ e ajuste challenge.yaml
  ./scripts/gitquest validate
  python3 scripts/lib/gq.py checksum .   # copie para starter_checksum
  ./scripts/validate-branch.sh           # os testes devem FALHAR no código inicial
  git commit -m "feat($id): $title"
  # adicione a entrada em config/challenge-catalog.yaml na main
EOF
