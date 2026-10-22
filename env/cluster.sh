# shellcheck shell=bash disable=SC2034  # as variáveis GQ_* são usadas por start.sh/test.sh
# Funções do cluster kind compartilhadas por start.sh e test.sh (desafio-25).
# Use com "source env/cluster.sh" depois de scripts/lib/common.sh.
#
# Regras de segurança seguidas aqui:
#   - todo comando kubectl usa --context kind-gitquest explicitamente;
#     o contexto atual do seu kubeconfig nunca é usado nem alterado de vez;
#   - nada é apagado fora do namespace desafio-25 (exceto o próprio cluster
#     gitquest, e só com ./start.sh --reset).

GQ_CLUSTER=gitquest
GQ_CONTEXT=kind-gitquest
GQ_NODE_CONTAINER=gitquest-control-plane
GQ_NS=desafio-25
# Imagem única da loja (frontend, api, db, job de manutenção e ferramentas).
GQ_APP_IMAGE=gitquest/desafio-25-shop:1.0.0

# Versões fixadas. A imagem do nó é fixada por digest (índice multi-arquitetura).
GQ_KIND_VERSION=v0.27.0
GQ_KUBECTL_VERSION=v1.32.2
GQ_NODE_IMAGE=kindest/node:v1.32.2@sha256:142f543559cc55d64e1ab9341df08e5ced84bd2e893736da8f51320f26f5950b

GQ_TOOLS_BIN="$PWD/.tools/bin"
PATH="$GQ_TOOLS_BIN:$PATH"

# k <args...> — kubectl sempre no contexto do desafio (saída sem CR do Windows).
k() {
  kubectl --context "$GQ_CONTEXT" "$@" | tr -d '\r'
}

# gq_timeout <segundos> <comando...> — usa timeout(1) quando disponível.
gq_timeout() {
  local s=$1
  shift
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5 "$s" "$@"
  else
    "$@"
  fi
}

gq_platform() {
  local os arch
  case "$(gq_os)" in
    linux | wsl) os=linux ;;
    macos) os=darwin ;;
    windows) os=windows ;;
    *) gq_die "sistema operacional não suportado: $(uname -s)" ;;
  esac
  case "$(uname -m)" in
    x86_64 | amd64) arch=amd64 ;;
    aarch64 | arm64) arch=arm64 ;;
    *) gq_die "arquitetura não suportada: $(uname -m)" ;;
  esac
  echo "$os $arch"
}

gq_fetch() { # gq_fetch <url> <destino|->
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 --connect-timeout 20 --max-time 600 -o "$2" "$1"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -T 60 -O "$2" "$1"
  else
    gq_die "é preciso curl ou wget para baixar $1"
  fi
}

gq_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d' ' -f1
  else
    gq_die "é preciso sha256sum ou shasum para verificar downloads"
  fi
}

# gq_install_tool <nome> <url-binário> <url-checksum>
# Baixa para .tools/bin, confere o sha256 publicado ao lado do binário e só
# então torna o arquivo executável.
gq_install_tool() {
  local name=$1 url=$2 sum_url=$3 exe=""
  [[ "$(gq_os)" == windows ]] && exe=.exe
  local dest="$GQ_TOOLS_BIN/$name$exe" tmp
  mkdir -p "$GQ_TOOLS_BIN"
  tmp="$dest.download"
  gq_info "Baixando $name de $url"
  gq_fetch "$url" "$tmp" || { rm -f "$tmp"; gq_die "falha ao baixar $url"; }
  local expected actual
  expected=$(gq_fetch "$sum_url" - | awk '{print $1}' | tr -d '\r') || { rm -f "$tmp"; gq_die "falha ao baixar $sum_url"; }
  actual=$(gq_sha256 "$tmp")
  if [[ -z "$expected" || "$expected" != "$actual" ]]; then
    rm -f "$tmp"
    gq_die "checksum inválido para $name (esperado ${expected:-?}, obtido $actual) — download descartado"
  fi
  chmod +x "$tmp"
  mv -f "$tmp" "$dest"
  gq_ok "$name verificado (sha256 $actual)"
}

gq_kubectl_version() {
  kubectl version --client -o json 2>/dev/null | tr -d '\r' |
    grep -oE '"gitVersion": *"v[0-9]+\.[0-9]+\.[0-9]+' | grep -oE 'v[0-9.]+' | head -1
}

# kubectl é compatível com o servidor 1.32 dentro de ±1 versão menor.
gq_kubectl_compatible() {
  local v minor
  command -v kubectl >/dev/null 2>&1 || return 1
  v=$(gq_kubectl_version)
  [[ "$v" =~ ^v1\.([0-9]+)\. ]] || return 1
  minor=${BASH_REMATCH[1]}
  ((minor >= 31 && minor <= 33))
}

gq_kind_version() {
  kind version 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -1
}

gq_ensure_tools() {
  local os arch exe=""
  read -r os arch <<<"$(gq_platform)"
  [[ "$os" == windows ]] && exe=.exe
  if gq_kubectl_compatible; then
    gq_ok "kubectl $(gq_kubectl_version) ($(command -v kubectl))"
  else
    gq_warn "kubectl ausente ou fora da faixa 1.31–1.33; usando kubectl $GQ_KUBECTL_VERSION em .tools/bin"
    local base="https://dl.k8s.io/release/$GQ_KUBECTL_VERSION/bin/$os/$arch/kubectl$exe"
    gq_install_tool kubectl "$base" "$base.sha256"
    hash -r
    gq_kubectl_compatible || gq_die "kubectl baixado não pôde ser executado"
  fi
  if [[ "$(gq_kind_version)" == "$GQ_KIND_VERSION" ]]; then
    gq_ok "kind $GQ_KIND_VERSION ($(command -v kind))"
  else
    gq_info "kind $GQ_KIND_VERSION não encontrado no PATH; instalando em .tools/bin"
    local base="https://github.com/kubernetes-sigs/kind/releases/download/$GQ_KIND_VERSION/kind-$os-$arch"
    gq_install_tool kind "$base" "$base.sha256sum"
    hash -r
    [[ "$(gq_kind_version)" == "$GQ_KIND_VERSION" ]] || gq_die "kind baixado não pôde ser executado"
  fi
}

# gq_cluster_state -> absent | stopped | nocontext | unreachable | ready
gq_cluster_state() {
  local running
  running=$(docker inspect -f '{{.State.Running}}' "$GQ_NODE_CONTAINER" 2>/dev/null) || { echo absent; return; }
  [[ "$running" == true ]] || { echo stopped; return; }
  kubectl config get-contexts "$GQ_CONTEXT" >/dev/null 2>&1 || { echo nocontext; return; }
  if MSYS_NO_PATHCONV=1 kubectl --context "$GQ_CONTEXT" --request-timeout=5s get --raw /readyz >/dev/null 2>&1; then
    echo ready
  else
    echo unreachable
  fi
}

# Exige um cluster pronto (usado pelo test.sh): explica o que fazer caso contrário.
gq_require_cluster() {
  command -v kubectl >/dev/null 2>&1 || gq_die "kubectl não encontrado. Rode ./start.sh (ele instala uma versão fixada em .tools/bin)."
  local state
  state=$(gq_cluster_state)
  case "$state" in
    ready) ;;
    absent) gq_die "o cluster kind '$GQ_CLUSTER' não existe. Rode ./start.sh para criá-lo." ;;
    stopped) gq_die "o cluster kind '$GQ_CLUSTER' está parado (contêiner $GQ_NODE_CONTAINER). Rode ./start.sh para religá-lo." ;;
    nocontext) gq_die "o contexto $GQ_CONTEXT não está no seu kubeconfig. Rode ./start.sh para exportá-lo." ;;
    *) gq_die "a API do cluster '$GQ_CLUSTER' não responde. Rode ./start.sh (ou ./start.sh --reset para recriar o cluster)." ;;
  esac
  if ! docker exec "$GQ_NODE_CONTAINER" crictl inspecti "docker.io/$GQ_APP_IMAGE" >/dev/null 2>&1; then
    gq_die "a imagem $GQ_APP_IMAGE não está carregada no cluster. Rode ./start.sh."
  fi
}

# gq_build_app — constrói a imagem da loja a partir de app/.
gq_build_app() {
  local log
  gq_info "Construindo imagem $GQ_APP_IMAGE"
  if ! log=$(docker build -t "$GQ_APP_IMAGE" -f app/Dockerfile app 2>&1); then
    printf '%s\n' "$log" | tail -25 >&2
    gq_die "falha ao construir $GQ_APP_IMAGE (veja o log acima). Verifique sua conexão e o Docker."
  fi
  gq_ok "imagem $GQ_APP_IMAGE pronta"
}
