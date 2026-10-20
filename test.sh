#!/usr/bin/env bash
# Avaliador do desafio-10: código de saída 0 somente se todos os critérios passarem.
#
# Aplica os manifests de k8s/ num namespace desafio-10 recriado do zero no
# cluster kind "gitquest" e verifica o comportamento do Deployment e do Service.
# Usa sempre --context kind-gitquest; nada fora do namespace desafio-10 é apagado.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/cluster.sh
source env/cluster.sh

ROLLOUT_TIMEOUT=${ROLLOUT_TIMEOUT:-120} # segundos para todas as réplicas ficarem Ready
UNREADY_GRACE=45                        # Running mas nunca Ready por mais que isso: falha
STABILITY_WAIT=15                       # observação depois do rollout (reinícios, OOM)
DEPLOY=inventory-api
SERVICE=inventory-api
SERVICE_URL="http://$SERVICE.$GQ_NS.svc.cluster.local"
REQUIRED_CACHE_MB=64

failures=0
pass() { printf '  %s✔%s %s\n' "$GQ_GREEN" "$GQ_RESET" "$1"; }
fail() {
  printf '  %s✘%s %s\n' "$GQ_RED" "$GQ_RESET" "$1"
  shift
  local line
  for line in "$@"; do printf '      %s\n' "$line"; done
  failures=$((failures + 1))
}
indent() { sed 's/^/      │ /'; }

# jp <recurso> <jsonpath> — lê um campo de um objeto do namespace do desafio.
jp() { k -n "$GQ_NS" get "$1" -o "jsonpath=$2" 2>/dev/null || true; }

recent_events() {
  echo "      últimos eventos do namespace:"
  k -n "$GQ_NS" get events --sort-by=.lastTimestamp \
    -o custom-columns='TIPO:.type,MOTIVO:.reason,OBJETO:.involvedObject.name,MENSAGEM:.message' 2>/dev/null |
    tail -n 8 | cut -c1-220 | indent
}

pod_logs() { # pod_logs <pod> — log da execução anterior (se houve reinício) ou atual
  local out
  out=$(k -n "$GQ_NS" logs "$1" --previous --tail=6 2>/dev/null) ||
    out=$(k -n "$GQ_NS" logs "$1" --tail=6 2>/dev/null) || true
  if [[ -n "$out" ]]; then
    echo "      log do contêiner ($1):"
    printf '%s\n' "$out" | cut -c1-220 | indent
  fi
}

# ---------------------------------------------------------------------------
gq_title "desafio-10 · avaliando os manifests de k8s/"
gq_require_docker
gq_require_cluster

compgen -G "k8s/*.y*ml" >/dev/null || gq_die "nenhum manifest .yaml encontrado em k8s/"

gq_info "Recriando o namespace $GQ_NS (cada execução começa do zero)"
k delete namespace "$GQ_NS" --ignore-not-found --wait=true --timeout=120s >/dev/null ||
  gq_die "não foi possível apagar o namespace $GQ_NS em 120s"
k create namespace "$GQ_NS" >/dev/null

gq_title "Critérios"

# 1. Validação dos manifests no servidor (schema estrito), sem criar nada.
dry_out=""
if ! dry_out=$(k -n "$GQ_NS" apply --dry-run=server --validate=strict -f k8s/ -o name 2>&1); then
  fail "manifests válidos (kubectl apply --dry-run=server)" "o servidor rejeitou os manifests:"
  printf '%s\n' "$dry_out" | tail -n 8 | indent
  gq_die "corrija os erros de sintaxe/schema acima e rode ./test.sh de novo"
fi
bad_objects=$(printf '%s\n' "$dry_out" |
  grep -vE '^(configmap|secret|service|deployment\.apps)/|^namespace/desafio-10$' | grep -v '^$' || true)
if [[ -n "$bad_objects" ]]; then
  fail "manifests válidos e restritos ao desafio" "objetos não permitidos neste desafio:" "$bad_objects"
  gq_die "use apenas ConfigMap, Secret, Deployment e Service"
fi
if ! grep -qx "deployment.apps/$DEPLOY" <<<"$dry_out" || ! grep -qx "service/$SERVICE" <<<"$dry_out"; then
  fail "manifests válidos" "é preciso um Deployment e um Service chamados '$DEPLOY'"
  gq_die "não renomeie os objetos do desafio"
fi
pass "manifests válidos no servidor (dry-run, validação estrita)"

apply_out=$(k -n "$GQ_NS" apply -f k8s/ 2>&1) || {
  printf '%s\n' "$apply_out" | indent
  gq_die "kubectl apply falhou"
}

container=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].name}')
image=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].image}')
replicas=$(jp "deploy/$DEPLOY" '{.spec.replicas}')
replicas=${replicas:-1}
# shellcheck disable=SC2016  # go-template: os $ pertencem ao template, não ao shell
selector=$(k -n "$GQ_NS" get "deploy/$DEPLOY" \
  -o go-template='{{range $k, $v := .spec.selector.matchLabels}}{{$k}}={{$v}},{{end}}' 2>/dev/null || true)
selector=${selector%,}

# 2. Imagem e réplicas declaradas.
if [[ "$image" == "$GQ_APP_IMAGE" || "$image" == "docker.io/$GQ_APP_IMAGE" ]]; then
  pass "imagem $GQ_APP_IMAGE"
else
  fail "imagem do contêiner" "o Deployment usa '$image', que não é a versão publicada pelo time" \
    "(dica: veja o que existe no nó: docker exec $GQ_NODE_CONTAINER crictl images | grep inventory)"
fi
if ((replicas >= 2)); then
  pass "$replicas réplicas declaradas (mínimo 2 para alta disponibilidade)"
else
  fail "réplicas" "spec.replicas = $replicas; o serviço precisa de ao menos 2 réplicas"
fi

# 3. Rollout: espera todas as réplicas Ready, falhando cedo em estados sem volta.
# Cada linha: nome|fase|ready|motivo-espera|mensagem-espera|último-término|reinícios
pod_rows() {
  k -n "$GQ_NS" get pods -l "$selector" -o jsonpath='{range .items[*]}{.metadata.name}{"|"}{.status.phase}{"|"}{.status.containerStatuses[0].ready}{"|"}{.status.containerStatuses[0].state.waiting.reason}{"|"}{.status.containerStatuses[0].state.waiting.message}{"|"}{.status.containerStatuses[0].lastState.terminated.reason}{"|"}{.status.containerStatuses[0].restartCount}{"\n"}{end}' 2>/dev/null || true
}

rollout_ok=0
problem="" problem_pod="" problem_detail=""
start=$SECONDS
running_since=""
while ((SECONDS - start < ROLLOUT_TIMEOUT)); do
  ready_count=0 total=0 all_running=1 last_pod=""
  while IFS='|' read -r name phase ready wreason wmsg lastterm _restarts; do
    [[ -n "$name" ]] || continue
    total=$((total + 1))
    last_pod=$name
    [[ "$ready" == true ]] && ready_count=$((ready_count + 1))
    [[ "$phase" == Running ]] || all_running=0
    case "$wreason" in
      ErrImagePull | ImagePullBackOff | InvalidImageName | ErrImageNeverPull)
        problem="a imagem não pôde ser obtida ($wreason)" problem_pod=$name problem_detail=$wmsg ;;
      CreateContainerConfigError | CreateContainerError | RunContainerError)
        problem="o contêiner não pôde ser criado ($wreason)" problem_pod=$name problem_detail=$wmsg ;;
      CrashLoopBackOff)
        if [[ "$lastterm" == OOMKilled ]]; then
          problem="o contêiner foi morto por falta de memória (OOMKilled) e está em CrashLoopBackOff"
        else
          problem="o contêiner termina logo após iniciar (CrashLoopBackOff, último término: ${lastterm:-?})"
        fi
        problem_pod=$name problem_detail="" ;;
    esac
    if [[ -z "$problem" && "$lastterm" == OOMKilled ]]; then
      problem="o contêiner foi morto por falta de memória (OOMKilled)" problem_pod=$name problem_detail=""
    fi
  done < <(pod_rows)
  [[ -n "$problem" ]] && break

  failure_cond=$(jp "deploy/$DEPLOY" '{.status.conditions[?(@.type=="ReplicaFailure")].message}')
  if [[ -n "$failure_cond" ]]; then
    problem="o ReplicaSet não conseguiu criar os pods" problem_detail=$failure_cond
    break
  fi

  updated=$(jp "deploy/$DEPLOY" '{.status.updatedReplicas}')
  if ((total == replicas && ready_count == replicas)) && [[ "${updated:-0}" == "$replicas" ]]; then
    rollout_ok=1
    break
  fi
  if ((total > 0 && all_running)); then
    running_since=${running_since:-$SECONDS}
    if ((SECONDS - running_since > UNREADY_GRACE)); then
      problem="os pods estão Running há mais de ${UNREADY_GRACE}s, mas não ficam Ready (${ready_count}/${replicas})"
      problem_pod=$last_pod
      break
    fi
  else
    running_since=""
  fi
  sleep 3
done

if ((rollout_ok)); then
  pass "rollout concluído: $replicas/$replicas réplicas Ready em $((SECONDS - start))s"
else
  [[ -n "$problem" ]] || problem="as réplicas não ficaram Ready em ${ROLLOUT_TIMEOUT}s"
  fail "todas as réplicas Ready" "$problem"
  [[ -n "$problem_detail" ]] && printf '      detalhe: %s\n' "${problem_detail:0:300}"
  if [[ -n "$problem_pod" ]]; then
    pod_logs "$problem_pod"
  fi
  recent_events
  echo "      investigue: kubectl --context $GQ_CONTEXT -n $GQ_NS describe pod ${problem_pod:-<pod>}"
fi

# 4. Estabilidade: nenhum reinício nem OOMKilled depois de alguns segundos.
if ((rollout_ok)); then
  gq_info "Observando os pods por ${STABILITY_WAIT}s"
  sleep "$STABILITY_WAIT"
  unstable=""
  while IFS='|' read -r name _phase ready _w _m lastterm restarts; do
    [[ -n "$name" ]] || continue
    if [[ "${restarts:-0}" != 0 || "$ready" != true ]]; then
      unstable+="$name: ready=$ready, reinícios=${restarts:-?}${lastterm:+, último término: $lastterm}; "
    fi
  done < <(pod_rows)
  if [[ -z "$unstable" ]]; then
    pass "pods estáveis: nenhum reinício após ${STABILITY_WAIT}s"
  else
    fail "pods estáveis (restartCount == 0)" "$unstable"
    recent_events
  fi
else
  fail "pods estáveis (restartCount == 0)" "não verificado: o rollout não terminou"
fi

# 5. Recursos declarados.
req_cpu=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].resources.requests.cpu}')
req_mem=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].resources.requests.memory}')
lim_mem=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].resources.limits.memory}')
missing=()
[[ -n "$req_cpu" ]] || missing+=(requests.cpu)
[[ -n "$req_mem" ]] || missing+=(requests.memory)
[[ -n "$lim_mem" ]] || missing+=(limits.memory)
if ((${#missing[@]} == 0)); then
  pass "recursos declarados (requests cpu=$req_cpu memory=$req_mem; limits memory=$lim_mem)"
else
  fail "requests e limits declarados" "faltando: ${missing[*]}"
fi

# app_exec <url> — roda "/app probe <url>" num pod do Deployment (a imagem não
# tem shell nem curl), com tempo limite.
app_exec() {
  MSYS_NO_PATHCONV=1 gq_timeout 30 kubectl --context "$GQ_CONTEXT" -n "$GQ_NS"     exec "deploy/$DEPLOY" -c "$container" -- /app probe "$1" 2>&1 | tr -d '
'
}

# 6. Probes HTTP configuradas e respondendo 200 dentro do pod.
port_of() { # port_of <porta|nome-da-porta>
  if [[ "$1" =~ ^[0-9]+$ ]]; then
    echo "$1"
  else
    jp "deploy/$DEPLOY" "{.spec.template.spec.containers[0].ports[?(@.name==\"$1\")].containerPort}"
  fi
}
check_probe() { # check_probe <readinessProbe|livenessProbe>
  local kind=$1 path port out
  path=$(jp "deploy/$DEPLOY" "{.spec.template.spec.containers[0].$kind.httpGet.path}")
  port=$(jp "deploy/$DEPLOY" "{.spec.template.spec.containers[0].$kind.httpGet.port}")
  if [[ -z "$port" ]]; then
    fail "$kind HTTP configurada" "o contêiner não tem $kind com httpGet"
    return
  fi
  if ((!rollout_ok)); then
    fail "$kind responde 200" "configurada (${path:-/} na porta $port), mas não verificada: o rollout não terminou"
    return
  fi
  local resolved
  resolved=$(port_of "$port")
  if out=$(app_exec "http://127.0.0.1:${resolved:-0}${path:-/}" 2>&1); then
    pass "$kind responde 200 (${path:-/} na porta $port)"
  else
    fail "$kind responde 200" "GET ${path:-/} na porta $port dentro do pod: $(head -n 1 <<<"$out")"
  fi
}
check_probe readinessProbe
check_probe livenessProbe

# 7. Service com um endpoint pronto por réplica.
# shellcheck disable=SC2016  # go-template: os $ pertencem ao template, não ao shell
svc_selector=$(k -n "$GQ_NS" get "svc/$SERVICE" \
  -o go-template='{{range $k, $v := .spec.selector}}{{$k}}={{$v}},{{end}}' 2>/dev/null || true)
svc_selector=${svc_selector%,}
endpoints=0
for _ in $(seq 1 10); do
  endpoints=$(k -n "$GQ_NS" get endpointslices -l "kubernetes.io/service-name=$SERVICE" \
    -o jsonpath='{range .items[*].endpoints[*]}{.conditions.ready}{"\n"}{end}' 2>/dev/null | grep -c '^true$' || true)
  ((endpoints >= replicas)) && break
  sleep 2
done
if ((endpoints >= replicas)); then
  pass "Service $SERVICE com $endpoints endpoints prontos"
else
  pods_matching=$(k -n "$GQ_NS" get pods -l "${svc_selector:-x-sem-seletor=1}" -o name 2>/dev/null | wc -l | tr -d ' ')
  fail "Service com $replicas endpoints prontos" "encontrados: $endpoints" \
    "seletor do Service: ${svc_selector:-<vazio>} → casa com $pods_matching pod(s)" \
    "labels dos pods: $(k -n "$GQ_NS" get pods -l "$selector" -o jsonpath='{.items[0].metadata.labels}' 2>/dev/null || echo '?')" \
    "investigue: kubectl --context $GQ_CONTEXT -n $GQ_NS get endpointslices,pods --show-labels"
fi

# 8 e 9. Requisições de dentro do cluster, pelo DNS do Service.
in_cluster() { # in_cluster <caminho>
  app_exec "$SERVICE_URL$1" 2>&1
}
if ((rollout_ok)); then
  if out=$(in_cluster /items) && grep -q '"sku":"GQ-001"' <<<"$out" && grep -q '"sku":"GQ-003"' <<<"$out"; then
    pass "GET $SERVICE_URL/items → 200 com o catálogo"
  else
    fail "GET $SERVICE_URL/items de dentro do cluster" "resposta: $(head -c 300 <<<"$out")"
  fi
  if out=$(in_cluster /healthz); then
    cache_mb=$(grep -oE '"cache_mb":[0-9]+' <<<"$out" | grep -oE '[0-9]+$' || echo 0)
    if ((cache_mb >= REQUIRED_CACHE_MB)); then
      pass "cache de $cache_mb MiB (requisito: >= $REQUIRED_CACHE_MB MiB)"
    else
      fail "cache de pelo menos $REQUIRED_CACHE_MB MiB" "o serviço reporta cache_mb=$cache_mb; diminuir o cache não é uma correção aceita"
    fi
  else
    fail "cache de pelo menos $REQUIRED_CACHE_MB MiB" "GET /healthz pelo Service falhou: $(head -n 1 <<<"$out")"
  fi
else
  fail "GET $SERVICE_URL/items de dentro do cluster" "não verificado: o rollout não terminou"
  fail "cache de pelo menos $REQUIRED_CACHE_MB MiB" "não verificado: o rollout não terminou"
fi

# 10. securityContext endurecido (campos efetivos do objeto aplicado).
nonroot_c=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].securityContext.runAsNonRoot}')
nonroot_p=$(jp "deploy/$DEPLOY" '{.spec.template.spec.securityContext.runAsNonRoot}')
ape=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].securityContext.allowPrivilegeEscalation}')
rofs=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].securityContext.readOnlyRootFilesystem}')
sec_missing=()
if [[ "$nonroot_c" != true ]] && ! [[ -z "$nonroot_c" && "$nonroot_p" == true ]]; then
  sec_missing+=("runAsNonRoot: true")
fi
[[ "$ape" == false ]] || sec_missing+=("allowPrivilegeEscalation: false")
[[ "$rofs" == true ]] || sec_missing+=("readOnlyRootFilesystem: true")
if ((${#sec_missing[@]} == 0)); then
  pass "securityContext: runAsNonRoot, sem escalada de privilégio, sistema de arquivos somente leitura"
else
  missing_list=$(IFS=,; echo "${sec_missing[*]}")
  fail "securityContext endurecido" "faltando: ${missing_list//,/, }"
fi

echo
if ((failures == 0)); then
  gq_ok "todos os critérios foram atendidos — o inventory-api está no ar!"
  exit 0
fi
gq_error "$failures critério(s) não atendido(s). Corrija um problema por vez e rode ./test.sh de novo."
echo "  Os objetos ficam no namespace $GQ_NS para você investigar:"
echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS get pods,svc,endpointslices"
exit 1
