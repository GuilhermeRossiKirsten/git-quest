# shellcheck shell=bash
# Helpers de desafio-24 compartilhados por start.sh e test.sh (use com source,
# depois de scripts/lib/common.sh).

GQ24_PROJECT=gitquest-desafio-24
GQ24_NETWORK=gitquest-desafio-24-net
IMAGE=$(gq_toolchain_image desafio-24 Dockerfile)
# Cache de build do Go num volume nomeado; container ligado só à rede interna do compose.
# shellcheck disable=SC2034 # lido por gq_docker_run (scripts/lib/common.sh)
GQ_DOCKER_EXTRA=(-v gitquest-desafio-24-gocache:/gocache -e REDIS_ADDR=redis:6379)
export GQ_DOCKER_NETWORK=$GQ24_NETWORK

compose() { docker compose -p "$GQ24_PROJECT" -f docker-compose.yml "$@"; }

# gq24_require: Docker + Docker Compose v2 com suporte a --wait.
gq24_require() {
  gq_require_docker
  local v
  v=$(docker compose version --short 2>/dev/null) ||
    gq_die "Docker Compose v2 não encontrado ('docker compose'). Atualize o Docker Desktop ou instale o plugin docker-compose-plugin."
  v=${v#v}
  gq_version_ge "$v" "2.20" || gq_die "Docker Compose $v é antigo demais; é necessário >= 2.20."
  gq_ok "Docker Compose $v"
}

# gq24_up: sobe o Redis (idempotente) e espera o healthcheck.
gq24_up() {
  gq_info "Subindo o Redis ($GQ24_PROJECT)"
  local log
  if ! log=$(compose up -d --wait 2>&1); then
    printf '%s\n' "$log" | tail -20 >&2
    gq_die "o Redis não ficou saudável — veja 'docker compose -p $GQ24_PROJECT logs redis'"
  fi
  gq_ok "Redis 7.4 saudável na rede $GQ24_NETWORK (sem porta exposta no host)"
}

# run <cmd...>: executa no container de toolchain, na rede do compose.
run() { gq_docker_run "$IMAGE" 300 "$@"; }
