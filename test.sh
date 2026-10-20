#!/usr/bin/env bash
# Avaliador do desafio-9: código de saída 0 somente se todos os critérios passarem.
#
# Constrói a imagem a partir do SEU Dockerfile e a examina de fora: tamanho,
# metadados, conteúdo de todas as camadas e o comportamento do container em
# execução. Nada do seu código roda fora de containers (o build roda no BuildKit).
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

IMAGE=gitquest/desafio-9-app:test
PROBE_IMAGE=gitquest/desafio-9-app:cache-probe
MAX_SIZE_BYTES=25000000 # 25 MB
READY_TIMEOUT=40        # segundos até /healthz responder
HEALTH_TIMEOUT=45       # segundos até o Docker marcar o container como healthy
GREETING="Bem-vindo ao desafio 9"
# Comentário presente só nos arquivos-fonte do serviço (app/): se ele aparece
# em alguma camada, o código-fonte foi parar na imagem.
SOURCE_MARKER=gitquest-desafio-9-source
RUN_LIMITS=(--memory 256m --cpus 1 --pids-limit 128)
RUN_NAME="gq-desafio9-run-$$"
RO_NAME="gq-desafio9-ro-$$"
export DOCKER_BUILDKIT=1

PY=$(gq_python)
SECRET=$(sed -n 's/^PAYMENTS_API_TOKEN=//p' .env 2>/dev/null | tr -d '\r')
[[ -n "$SECRET" ]] || gq_die ".env original não encontrado — restaure-o com: git checkout -- .env"

TMP=$(mktemp -d)
cleanup() {
  docker rm -f "$RUN_NAME" "$RO_NAME" >/dev/null 2>&1 || true
  docker image rm "$PROBE_IMAGE" >/dev/null 2>&1 || true
  rm -rf "$TMP"
}
trap cleanup EXIT

failures=0
pass() { printf '  %s✔%s %s\n' "$GQ_GREEN" "$GQ_RESET" "$1"; }
fail() {
  printf '  %s✘%s %s\n' "$GQ_RED" "$GQ_RESET" "$1"
  shift
  local line
  for line in "$@"; do
    [[ -z "$line" ]] || printf '      %s\n' "$line"
  done
  failures=$((failures + 1))
}

# http_get <url> — imprime o corpo; falha em erro de rede ou status >= 400.
http_get() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsS --noproxy '*' --max-time 3 "$1" 2>/dev/null
  else
    "$PY" - "$1" <<'PY' 2>/dev/null
import sys, urllib.request
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
sys.stdout.write(opener.open(sys.argv[1], timeout=3).read().decode())
PY
  fi
}

running() { [[ "$(docker inspect -f '{{.State.Running}}' "$1" 2>/dev/null)" == true ]]; }

host_port() { { docker port "$1" 8080/tcp 2>/dev/null || true; } | head -n1 | sed 's/.*://'; }

# wait_ready <container> — espera /healthz responder pela porta publicada.
wait_ready() {
  local name=$1 port i
  for ((i = 0; i < READY_TIMEOUT; i++)); do
    running "$name" || return 1
    port=$(host_port "$name")
    if [[ -n "$port" ]] && http_get "http://127.0.0.1:$port/healthz" >/dev/null; then
      return 0
    fi
    sleep 1
  done
  return 1
}

last_logs() { docker logs --tail 5 "$1" 2>&1 | sed 's/^/  │ /'; }

gq_title "GitQuest · desafio-9 — avaliando a imagem do seu Dockerfile"
gq_require_docker

# 1. Build ---------------------------------------------------------------------
if docker build --progress=plain -t "$IMAGE" . >"$TMP/build.log" 2>&1; then
  pass "1. a imagem é construída a partir do Dockerfile"
else
  fail "1. o build falhou — últimas linhas:" "$(tail -n 15 "$TMP/build.log")"
  gq_error "sem imagem não há o que avaliar; corrija o build (docker build -t teste .)"
  exit 1
fi

# 2. Tamanho --------------------------------------------------------------------
size=$(docker image inspect -f '{{.Size}}' "$IMAGE")
size_mb=$(((size + 500000) / 1000000))
if ((size < MAX_SIZE_BYTES)); then
  pass "2. tamanho da imagem: ${size_mb} MB (limite: < 25 MB)"
else
  fail "2. a imagem tem ${size_mb} MB; o limite é < 25 MB" \
    "o que mais pesa? docker history $IMAGE"
fi

# Containers usados nos critérios 3, 4, 6 e 7 (sobem juntos para poupar tempo).
docker run -d --name "$RUN_NAME" "${RUN_LIMITS[@]}" -p 127.0.0.1::8080 \
  -e GREETING="$GREETING" "$IMAGE" >/dev/null 2>"$TMP/run.err" || true
docker run -d --name "$RO_NAME" "${RUN_LIMITS[@]}" -p 127.0.0.1::8080 \
  --read-only --cap-drop ALL --security-opt no-new-privileges \
  "$IMAGE" >/dev/null 2>"$TMP/ro.err" || true

ready=0
wait_ready "$RUN_NAME" && ready=1
port=$(host_port "$RUN_NAME")
not_ready_reason=("o container não respondeu em /healthz (porta 8080) em ${READY_TIMEOUT}s" "$(cat "$TMP/run.err")" "$(last_logs "$RUN_NAME")")

# 3. Usuário não-root --------------------------------------------------------------
config_user=$(docker image inspect -f '{{.Config.User}}' "$IMAGE")
user_part=${config_user%%:*}
if [[ -z "$user_part" || "$user_part" == 0 || "$user_part" == root ]]; then
  fail "3. a imagem roda como root (USER na configuração: '${config_user:-<vazio>}')" \
    "confira: docker image inspect -f '{{.Config.User}}' $IMAGE"
elif ((!ready)); then
  fail "3. não foi possível confirmar o usuário do processo" "${not_ready_reason[@]}"
else
  whoami=$(http_get "http://127.0.0.1:$port/whoami" || true)
  uid=$(printf '%s' "$whoami" | sed -n 's/.*"uid":\([0-9][0-9]*\).*/\1/p')
  if [[ -n "$uid" && "$uid" != 0 ]]; then
    pass "3. o processo roda sem privilégios (USER $config_user, uid $uid)"
  else
    fail "3. o processo dentro do container roda com uid '${uid:-?}'" "resposta de /whoami: $whoami"
  fi
fi

# 4. HEALTHCHECK ------------------------------------------------------------------
hc=$(docker image inspect -f '{{if .Config.Healthcheck}}{{join .Config.Healthcheck.Test " "}}{{end}}' "$IMAGE")
if [[ -z "$hc" || "$hc" == NONE ]]; then
  fail "4. a imagem não declara HEALTHCHECK" "o Docker não tem como saber se o serviço está saudável"
else
  status=""
  for ((i = 0; i < HEALTH_TIMEOUT; i++)); do
    status=$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{end}}' "$RUN_NAME" 2>/dev/null || true)
    [[ "$status" == healthy || "$status" == unhealthy ]] && break
    running "$RUN_NAME" || break
    sleep 1
  done
  if [[ "$status" == healthy ]]; then
    pass "4. HEALTHCHECK definido e o container ficou healthy ($hc)"
  else
    probe_out=$(docker inspect -f '{{if .State.Health}}{{range .State.Health.Log}}{{.Output}}{{"\n"}}{{end}}{{end}}' "$RUN_NAME" 2>/dev/null | grep -v '^[[:space:]]*$' | tail -n 1 || true)
    fail "4. o container não ficou healthy em ${HEALTH_TIMEOUT}s (estado: ${status:-desconhecido})" \
      "HEALTHCHECK: $hc" "saída da última verificação: ${probe_out:-<nenhuma>}"
  fi
fi

# 6. A aplicação responde (avaliado antes do 5 porque usa o container em execução)
app_ok=0
if ((ready)); then
  body=$(http_get "http://127.0.0.1:$port/api/greeting" || true)
  if [[ "$body" == *"\"message\":\"$GREETING\""* ]]; then
    app_ok=1
  fi
fi

# 7. Somente leitura e sem capabilities ---------------------------------------------
if wait_ready "$RO_NAME"; then
  ro_ok=1
else
  ro_ok=0
fi

# 5. Nada de segredos, .git ou código-fonte em nenhuma camada ----------------------------
scan_rc=0
docker save "$IMAGE" | "$PY" tests/inspect_image.py "$SECRET" "$SOURCE_MARKER" >"$TMP/scan.txt" || scan_rc=$?
if ((scan_rc == 0)); then
  pass "5. nenhuma camada contém o token do .env, arquivos .git ou o código-fonte"
else
  details=()
  for kind in secret git source; do
    n=$(grep -c "^$kind" "$TMP/scan.txt" || true)
    ((n > 0)) || continue
    case $kind in
      secret) label="token PAYMENTS_API_TOKEN encontrado" ;;
      git) label="arquivos de .git encontrados" ;;
      source) label="código-fonte do serviço encontrado" ;;
    esac
    details+=("$label ($n): $(grep "^$kind" "$TMP/scan.txt" | head -n 3 | cut -f2 | paste -sd ' ' -)")
  done
  ((${#details[@]})) || details=("$(cat "$TMP/scan.txt")")
  fail "5. a imagem carrega o que não deveria (todas as camadas e o histórico contam):" "${details[@]}"
fi

if ((app_ok)); then
  pass "6. a aplicação responde: /healthz e /api/greeting (GREETING lido do ambiente)"
elif ((!ready)); then
  fail "6. a aplicação não respondeu" "${not_ready_reason[@]}"
else
  fail "6. /api/greeting respondeu algo inesperado" "esperado: {\"message\":\"$GREETING\"}" "recebido: ${body:-<vazio>}"
fi

if ((ro_ok)); then
  pass "7. roda com --read-only, --cap-drop ALL e no-new-privileges"
else
  fail "7. o container não funciona com --read-only --cap-drop ALL --security-opt no-new-privileges" \
    "$(cat "$TMP/ro.err")" "$(last_logs "$RO_NAME")"
fi
docker rm -f "$RUN_NAME" "$RO_NAME" >/dev/null 2>&1 || true

# 8. Cache de dependências -----------------------------------------------------------
# Copia o contexto, muda só um arquivo .go e reconstrói: a etapa `go mod download`
# deve sair do cache do BuildKit (CACHED).
mkdir -p "$TMP/ctx"
tar -cf - --exclude=./build . | tar -xf - -C "$TMP/ctx"
printf '\n// cache probe %s\n' "$$" >>"$TMP/ctx/app/main.go"
if ! docker build --progress=plain -t "$PROBE_IMAGE" "$TMP/ctx" >"$TMP/probe.log" 2>&1; then
  fail "8. o rebuild após alterar um arquivo .go falhou" "$(tail -n 10 "$TMP/probe.log")"
else
  steps=$(grep -E '^#[0-9]+ \[[^]]*\] RUN .*go mod download' "$TMP/probe.log" | sed 's/^#\([0-9]*\) .*/\1/' | sort -u || true)
  if [[ -z "$steps" ]]; then
    fail "8. nenhuma etapa RUN com 'go mod download' foi encontrada" \
      "as dependências devem ser baixadas numa etapa própria, que o cache reaproveite"
  else
    uncached=0
    for id in $steps; do
      grep -qx "#$id CACHED" "$TMP/probe.log" || uncached=1
    done
    if ((uncached)); then
      fail "8. alterar só um arquivo .go invalidou a etapa 'go mod download'" \
        "$(grep -E '^#[0-9]+ \[[^]]*\] RUN .*go mod download' "$TMP/probe.log" | head -n 1)" \
        "veja a ordem das instruções no Dockerfile e o que cada COPY traz"
    else
      pass "8. mudar só o código Go reaproveita a camada de dependências (go mod download: CACHED)"
    fi
  fi
fi

echo
if ((failures == 0)); then
  gq_ok "todos os 8 critérios foram atendidos — imagem pronta para produção!"
  exit 0
fi
gq_error "$failures critério(s) não atendido(s). Dicas: ./scripts/gitquest hints 1"
exit 1
