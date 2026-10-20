# shellcheck shell=bash
# Helpers de desafio-20 compartilhados por start.sh e test.sh (use com source,
# depois de scripts/lib/common.sh).

GQ20_PROJECT=gitquest-desafio-20
GQ20_NETWORK=gitquest-desafio-20-net
IMAGE=$(gq_toolchain_image desafio-20 Dockerfile)
# Cache de build do Go num volume nomeado; container ligado só à rede interna do compose.
# shellcheck disable=SC2034 # lido por gq_docker_run (scripts/lib/common.sh)
GQ_DOCKER_EXTRA=(-v gitquest-desafio-20-gocache:/gocache
  -e "DATABASE_URL=postgres://gitquest:gitquest@postgres:5432/payments?sslmode=disable")
export GQ_DOCKER_NETWORK=$GQ20_NETWORK

compose() { docker compose -p "$GQ20_PROJECT" -f docker-compose.yml "$@"; }

# gq20_require: Docker + Docker Compose v2 com suporte a --wait.
gq20_require() {
  gq_require_docker
  local v
  v=$(docker compose version --short 2>/dev/null) ||
    gq_die "Docker Compose v2 não encontrado ('docker compose'). Atualize o Docker Desktop ou instale o plugin docker-compose-plugin."
  v=${v#v}
  gq_version_ge "$v" "2.20" || gq_die "Docker Compose $v é antigo demais; é necessário >= 2.20."
  gq_ok "Docker Compose $v"
}

# gq20_up: sobe o PostgreSQL (idempotente) e espera o healthcheck.
gq20_up() {
  gq_info "Subindo o PostgreSQL ($GQ20_PROJECT)"
  local log
  if ! log=$(compose up -d --wait 2>&1); then
    printf '%s\n' "$log" | tail -20 >&2
    gq_die "o PostgreSQL não ficou saudável — veja 'docker compose -p $GQ20_PROJECT logs postgres'"
  fi
  gq_ok "PostgreSQL 16 saudável na rede $GQ20_NETWORK (sem porta exposta no host)"
}

# run <cmd...>: executa no container de toolchain, na rede do compose.
run() { gq_docker_run "$IMAGE" 420 "$@"; }
