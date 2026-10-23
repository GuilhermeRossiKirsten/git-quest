# shellcheck shell=bash
# Shared helpers for GitQuest scripts. Source it; do not execute it.
#
#   source "$(dirname "$0")/scripts/lib/common.sh"
#
# Every function is prefixed with gq_ to avoid clashing with challenge code.

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  GQ_RED=$'\033[31m' GQ_GREEN=$'\033[32m' GQ_YELLOW=$'\033[33m'
  GQ_BLUE=$'\033[34m' GQ_BOLD=$'\033[1m' GQ_RESET=$'\033[0m'
else
  GQ_RED='' GQ_GREEN='' GQ_YELLOW='' GQ_BLUE='' GQ_BOLD='' GQ_RESET=''
fi

gq_info()  { printf '%s==>%s %s\n' "$GQ_BLUE" "$GQ_RESET" "$*"; }
gq_ok()    { printf '%s ok%s %s\n' "$GQ_GREEN" "$GQ_RESET" "$*"; }
gq_warn()  { printf '%saviso:%s %s\n' "$GQ_YELLOW" "$GQ_RESET" "$*" >&2; }
gq_error() { printf '%serro:%s %s\n' "$GQ_RED" "$GQ_RESET" "$*" >&2; }
gq_die()   { gq_error "$*"; exit 1; }
gq_title() { printf '\n%s%s%s\n' "$GQ_BOLD" "$*" "$GQ_RESET"; }

gq_os() {
  case "$(uname -s)" in
    Linux*)  if grep -qi microsoft /proc/version 2>/dev/null; then echo wsl; else echo linux; fi ;;
    Darwin*) echo macos ;;
    MINGW*|MSYS*|CYGWIN*) echo windows ;;
    *) echo unknown ;;
  esac
}

gq_python() {
  local py
  for py in python3 python; do
    if command -v "$py" >/dev/null 2>&1 && "$py" -c 'import sys; sys.exit(sys.version_info < (3, 8))' 2>/dev/null; then
      echo "$py"; return 0
    fi
  done
  gq_die "Python >= 3.8 não encontrado. Instale python3 (ex.: sudo apt install python3)."
}

# gq_require_cmd <command> <how to install>
gq_require_cmd() {
  command -v "$1" >/dev/null 2>&1 || gq_die "'$1' não encontrado. $2"
}

# gq_version_ge <have> <want> — true when have >= want (dotted numeric versions)
gq_version_ge() {
  local -a a b
  IFS=. read -ra a <<<"$1"
  IFS=. read -ra b <<<"$2"
  local i
  for ((i = 0; i < ${#b[@]}; i++)); do
    local x=${a[i]:-0} y=${b[i]:-0}
    x=${x%%[!0-9]*} y=${y%%[!0-9]*}
    ((10#${x:-0} > 10#${y:-0})) && return 0
    ((10#${x:-0} < 10#${y:-0})) && return 1
  done
  return 0
}

gq_require_docker() {
  gq_require_cmd docker "Instale Docker Engine (Linux) ou Docker Desktop com WSL2 (Windows/macOS): https://docs.docker.com/get-docker/"
  if ! docker info >/dev/null 2>&1; then
    gq_die "O daemon do Docker não está respondendo. Inicie o Docker Desktop ou rode 'sudo systemctl start docker'."
  fi
  local v
  v=$(docker version --format '{{.Server.Version}}' 2>/dev/null || echo 0)
  gq_version_ge "$v" "20.10" || gq_die "Docker $v é antigo demais; é necessário >= 20.10."
}

# Path of the current directory as the Docker daemon understands it.
gq_host_pwd() {
  if [[ "$(gq_os)" == windows ]]; then pwd -W; else pwd; fi
}

# gq_seccomp_tsan — value for `docker run --security-opt`: Docker's default seccomp
# profile (moby v28.3.3) plus ONE extra rule, personality(PER_LINUX|ADDR_NO_RANDOMIZE),
# which `setarch -R` needs so ThreadSanitizer can start on kernels with high
# vm.mmap_rnd_bits. Docker Desktop runs unconfined by default; this keeps Linux
# engines (and CI) on the default profile instead of seccomp=unconfined.
gq_seccomp_tsan() {
  local dir
  dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && if [[ "$(gq_os)" == windows ]]; then pwd -W; else pwd; fi)"
  printf 'seccomp=%s/seccomp-tsan.json' "$dir"
}

# gq_docker_build <image-tag> <context-dir|-> [dockerfile]
# Context "-" builds from the Dockerfile alone (toolchain images need no files).
# Idempotent: Docker's layer cache makes re-runs cheap and the tag is overwritten.
gq_docker_build() {
  local tag=$1 ctx=$2 file=${3:-$2/Dockerfile}
  gq_info "Construindo imagem $tag"
  local log rc=0
  if [[ "$ctx" == - ]]; then
    log=$(docker build -t "$tag" - <"$file" 2>&1) || rc=$?
  else
    log=$(docker build -t "$tag" -f "$file" "$ctx" 2>&1) || rc=$?
  fi
  if ((rc != 0)); then
    printf '%s\n' "$log" | tail -25 >&2
    gq_die "Falha ao construir $tag (veja o log acima). Verifique sua conexão e o Docker."
  fi
  gq_ok "imagem $tag pronta"
}

# gq_toolchain_image <challenge-id> <dockerfile>
# Prints a tag derived from the Dockerfile content, so editing the Dockerfile
# produces a new image instead of silently reusing a stale one.
gq_toolchain_image() {
  echo "gitquest/$1:$(git hash-object "$2" | cut -c1-12)"
}

# gq_docker_ensure_image <image-tag> <dockerfile> — build only when missing.
gq_docker_ensure_image() {
  docker image inspect "$1" >/dev/null 2>&1 || gq_docker_build "$1" - "$2"
}

# gq_docker_run <image> <timeout-seconds> <command...>
# Runs the command in a throwaway container with the current directory mounted
# at /work, as a non-root user, without network and with CPU/memory/PID limits.
# The timeout is enforced inside the container so it behaves the same on Linux,
# macOS and Windows. Extra `docker run` flags (e.g. a cache volume) can be
# passed through the GQ_DOCKER_EXTRA array.
gq_docker_run() {
  local image=$1 timeout=$2
  shift 2
  local user=1000:1000
  if [[ "$(gq_os)" == linux || "$(gq_os)" == wsl ]]; then
    user="$(id -u):$(id -g)"   # files written to /work stay owned by you
  fi
  local -a tty=() extra=()
  [[ -t 1 ]] && tty=(-t)
  if declare -p GQ_DOCKER_EXTRA >/dev/null 2>&1; then extra=("${GQ_DOCKER_EXTRA[@]}"); fi
  MSYS_NO_PATHCONV=1 docker run --rm "${tty[@]}" --user "$user" "${extra[@]}" \
    --network "${GQ_DOCKER_NETWORK:-none}" \
    --cpus "${GQ_CPUS:-2}" --memory "${GQ_MEMORY:-2g}" --pids-limit 512 \
    --cap-add SYS_PTRACE \
    -e HOME=/tmp -e NO_COLOR="${NO_COLOR:-}" \
    -v "$(gq_host_pwd)":/work -w /work \
    "$image" timeout --kill-after=10 "$timeout" "$@"
}

gq_banner_next_steps() {
  # gq_banner_next_steps <test-command> [extra lines...]
  local test_cmd=$1
  shift
  gq_title "Ambiente pronto."
  echo "  1. Leia o enunciado:         README.md"
  echo "  2. Edite apenas os arquivos listados em 'editable' no challenge.yaml"
  echo "  3. Rode os testes:           $test_cmd"
  echo "  4. Precisa de ajuda?         ./scripts/gitquest hints 1"
  local line
  for line in "$@"; do echo "  $line"; done
}
