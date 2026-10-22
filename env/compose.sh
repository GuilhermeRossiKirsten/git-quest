# shellcheck shell=bash
# Helpers de desafio-23 compartilhados por start.sh, test.sh e explain.sh (use
# com source, depois de scripts/lib/common.sh).

GQ23_PROJECT=gitquest-desafio-23

compose() { docker compose -p "$GQ23_PROJECT" -f docker-compose.yml "$@"; }

# Executa um comando no container do PostgreSQL. Os caminhos /challenge/...
# são do container: MSYS_NO_PATHCONV impede o Git Bash de convertê-los.
pg_exec() { MSYS_NO_PATHCONV=1 compose exec -T postgres "$@"; }

# gq23_require: Docker + Docker Compose v2 com suporte a --wait.
gq23_require() {
  gq_require_docker
  local v
  v=$(docker compose version --short 2>/dev/null) ||
    gq_die "Docker Compose v2 não encontrado ('docker compose'). Atualize o Docker Desktop ou instale o plugin docker-compose-plugin."
  v=${v#v}
  gq_version_ge "$v" "2.20" || gq_die "Docker Compose $v é antigo demais; é necessário >= 2.20."
  gq_ok "Docker Compose $v"
}

# gq23_up: sobe o PostgreSQL (idempotente) e espera o healthcheck.
gq23_up() {
  gq_info "Subindo o PostgreSQL ($GQ23_PROJECT)"
  local log
  if ! log=$(compose up -d --wait 2>&1); then
    printf '%s\n' "$log" | tail -20 >&2
    gq_die "o PostgreSQL não ficou saudável — veja 'docker compose -p $GQ23_PROJECT logs postgres'"
  fi
  gq_ok "PostgreSQL 16 saudável (sem porta exposta no host)"
}

# gq23_seed: gera o banco-modelo shop_template se ele não existir ou se
# db/schema.sql ou db/seed.sql mudaram (a versão fica no COMMENT do banco).
gq23_seed() {
  local want have
  want=$(cat db/schema.sql db/seed.sql | git hash-object --stdin)
  have=$(pg_exec psql -X -At -U gitquest -d postgres -c \
    "SELECT shobj_description(oid, 'pg_database') FROM pg_database WHERE datname = 'shop_template'")
  if [[ "$have" == "$want" ]]; then
    gq_ok "banco-modelo shop_template pronto (seed $want)"
    return 0
  fi
  gq_info "Gerando o banco-modelo shop_template: 100 mil clientes, 200 mil produtos, 600 mil pedidos (~20 s)"
  pg_exec psql -X -q -U gitquest -d postgres -v ON_ERROR_STOP=1 -v seed_version="$want" \
    -f /challenge/db/seed.sql || gq_die "falha ao gerar o banco-modelo (veja o erro acima)"
  gq_ok "banco-modelo shop_template gerado"
}

# gq23_shop: (re)cria o banco "shop" para você explorar, com os índices de
# sql/indexes.sql e estatísticas atualizadas.
gq23_shop() {
  gq_info "Recriando o banco 'shop' a partir do modelo e aplicando sql/indexes.sql"
  pg_exec psql -X -q -U gitquest -d postgres -v ON_ERROR_STOP=1 \
    -c "ALTER ROLE app RESET ALL" -c "DROP DATABASE IF EXISTS shop WITH (FORCE)" \
    -c "CREATE DATABASE shop TEMPLATE shop_template STRATEGY FILE_COPY" >/dev/null ||
    gq_die "não foi possível criar o banco 'shop'"
  if ! pg_exec psql -X -q -U app -d shop -v ON_ERROR_STOP=1 -f /challenge/sql/indexes.sql; then
    gq_warn "sql/indexes.sql falhou (veja o erro acima); o banco 'shop' ficou só com os índices anteriores ao erro"
  fi
  # Mesmo ANALYZE do avaliador: amostra = tabela inteira, estatísticas determinísticas.
  pg_exec psql -X -q -U gitquest -d shop -c "SET default_statistics_target = 2000" -c "ANALYZE" >/dev/null
  gq_ok "banco 'shop' pronto"
}
