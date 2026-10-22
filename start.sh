#!/usr/bin/env bash
# Prepara o ambiente do desafio-25: ferramentas fixadas, cluster kind "gitquest"
# (compartilhado com os outros desafios da trilha), imagem da loja carregada no
# cluster e o namespace desafio-25.
# Idempotente: reutiliza o que já existe.
#
#   ./start.sh            cria ou reutiliza o cluster
#   ./start.sh --reset    apaga e recria SOMENTE o cluster kind "gitquest"
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/cluster.sh
source env/cluster.sh

reset=0
case "${1:-}" in
  "") ;;
  --reset) reset=1 ;;
  -h | --help)
    sed -n '2,8p' "$0"
    exit 0
    ;;
  *) gq_die "opção desconhecida: $1 (use --reset ou --help)" ;;
esac

gq_title "GitQuest · desafio-25 — Quem pode o quê"

gq_info "Verificando pré-requisitos"
gq_require_docker
gq_ok "Docker $(docker version --format '{{.Server.Version}}')"
gq_ensure_tools

# kind muda o current-context do kubeconfig ao criar/exportar o cluster;
# guardamos o contexto que você estava usando para restaurá-lo depois.
previous_context=$(kubectl config current-context 2>/dev/null | tr -d '\r' || true)
restore_context() {
  if [[ -n "$previous_context" && "$previous_context" != "$GQ_CONTEXT" ]]; then
    kubectl config use-context "$previous_context" >/dev/null
    gq_info "contexto atual do kubeconfig mantido em '$previous_context' (use --context $GQ_CONTEXT)"
  fi
}

if ((reset)) && [[ "$(gq_cluster_state)" != absent ]]; then
  gq_warn "--reset: apagando o cluster kind '$GQ_CLUSTER' (nenhum outro cluster é tocado)"
  kind delete cluster --name "$GQ_CLUSTER"
fi

case "$(gq_cluster_state)" in
  absent)
    gq_info "Criando o cluster kind '$GQ_CLUSTER' ($GQ_NODE_IMAGE) — pode levar alguns minutos"
    kind create cluster --name "$GQ_CLUSTER" --image "$GQ_NODE_IMAGE" --wait 180s ||
      gq_die "falha ao criar o cluster. Verifique se o Docker tem ao menos 4 GB de memória e tente ./start.sh --reset"
    restore_context
    ;;
  stopped)
    gq_info "Religando o contêiner $GQ_NODE_CONTAINER"
    docker start "$GQ_NODE_CONTAINER" >/dev/null
    ;;
esac

if ! kubectl config get-contexts "$GQ_CONTEXT" >/dev/null 2>&1; then
  gq_info "Exportando o contexto $GQ_CONTEXT para o kubeconfig"
  kind export kubeconfig --name "$GQ_CLUSTER"
  restore_context
fi

gq_info "Aguardando a API do cluster"
deadline=$((SECONDS + 120))
until MSYS_NO_PATHCONV=1 kubectl --context "$GQ_CONTEXT" --request-timeout=5s get --raw /readyz >/dev/null 2>&1; do
  ((SECONDS < deadline)) || gq_die "a API do cluster não respondeu em 120s. Tente ./start.sh --reset"
  sleep 2
done
k wait --for=condition=Ready node --all --timeout=120s >/dev/null ||
  gq_die "o nó do cluster não ficou Ready em 120s. Tente ./start.sh --reset"
k -n kube-system rollout status deployment/coredns --timeout=120s >/dev/null ||
  gq_die "o DNS do cluster (coredns) não ficou pronto em 120s"
gq_ok "cluster '$GQ_CLUSTER' pronto (contexto $GQ_CONTEXT)"

gq_build_app
gq_info "Carregando $GQ_APP_IMAGE no cluster"
kind load docker-image "$GQ_APP_IMAGE" --name "$GQ_CLUSTER" >/dev/null ||
  gq_die "falha ao carregar a imagem no cluster (kind load docker-image)"
gq_ok "imagem disponível nos nós do cluster"

k create namespace "$GQ_NS" --dry-run=client -o yaml | k apply -f - >/dev/null
gq_ok "namespace $GQ_NS pronto"

extra=()
if [[ ":$PATH:" == *":$GQ_TOOLS_BIN:"* ]] && [[ -n "$(ls -A "$GQ_TOOLS_BIN" 2>/dev/null)" ]]; then
  extra+=("Ferramentas baixadas em .tools/bin — no seu terminal: export PATH=\"\$PWD/.tools/bin:\$PATH\"")
fi
gq_banner_next_steps "./test.sh" \
  "5. Aplique e investigue:     kubectl --context $GQ_CONTEXT -n $GQ_NS apply -f k8s/" \
  "                             kubectl --context $GQ_CONTEXT -n $GQ_NS get pods,sa,role,rolebinding,netpol" \
  "   Quem pode o quê?          kubectl --context $GQ_CONTEXT -n $GQ_NS auth can-i --list --as=system:serviceaccount:$GQ_NS:default" \
  "   Teste a rede por dentro:  kubectl --context $GQ_CONTEXT -n $GQ_NS exec deploy/frontend -- /app connect db:5432" \
  "6. Para apagar o cluster:    kind delete cluster --name $GQ_CLUSTER (afeta todos os desafios da trilha)" \
  ${extra[@]+"${extra[@]}"}
