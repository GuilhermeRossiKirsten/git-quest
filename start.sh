#!/usr/bin/env bash
# Prepara o ambiente de desafio-9. Idempotente: pode ser executado várias vezes.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

IMAGE=gitquest/desafio-9-app:dev
BASE_IMAGES=(golang:1.25.3-bookworm gcr.io/distroless/static-debian12:nonroot)
export DOCKER_BUILDKIT=1

gq_title "GitQuest · desafio-9 — A imagem de um gigabyte"

gq_require_docker
gq_ok "Docker $(docker version --format '{{.Server.Version}}')"
docker buildx version >/dev/null 2>&1 ||
  gq_die "BuildKit (docker buildx) não encontrado. Atualize o Docker (Desktop >= 4.x ou Engine >= 23) ou instale o plugin buildx."
gq_ok "BuildKit disponível ($(docker buildx version | awk '{print $2}'))"
gq_python >/dev/null
command -v curl >/dev/null 2>&1 || gq_warn "curl não encontrado; os testes usarão Python para as requisições HTTP"

# Imagens base com versão fixada (baixadas só se faltarem).
for img in "${BASE_IMAGES[@]}"; do
  if docker image inspect "$img" >/dev/null 2>&1; then
    gq_ok "imagem base $img já disponível"
  else
    gq_info "Baixando $img"
    docker pull "$img" >/dev/null || gq_die "não foi possível baixar $img — verifique sua conexão"
    gq_ok "imagem base $img baixada"
  fi
done

# Constrói a imagem a partir do Dockerfile atual, para você ver o ponto de partida.
gq_info "Construindo $IMAGE a partir do Dockerfile atual"
if log=$(docker build -t "$IMAGE" . 2>&1); then
  size=$(docker image ls "$IMAGE" --format '{{.Size}}')
  gq_ok "imagem $IMAGE construída — tamanho atual: ${GQ_BOLD}${size}${GQ_RESET} (meta: < 25 MB)"
else
  printf '%s\n' "$log" | tail -n 15 >&2
  gq_warn "o Dockerfile atual não compila (log acima). Corrija-o e rode ./test.sh."
fi

gq_banner_next_steps "./test.sh" \
  "5. Rode o serviço:            docker run --rm -p 8080:8080 -e GREETING=Oi $IMAGE" \
  "   e acesse:                  curl localhost:8080/api/greeting  (também /healthz e /whoami)" \
  "6. Investigue a imagem:       docker history $IMAGE   ·   docker image inspect $IMAGE"
