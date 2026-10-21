#!/usr/bin/env bash
# Avaliador do desafio-18: código de saída 0 somente se todos os critérios passarem.
#
# Recria o namespace desafio-18 no cluster kind "gitquest", aplica a dependência
# pricing (env/pricing.yaml) e os manifests de k8s/, e então provoca, em ordem,
# as situações que as probes precisam distinguir:
#   1. aquecimento de 25 s           → nenhum reinício; pods só Ready depois dele
#   2. loop principal travado        → só o pod travado é reiniciado, rápido
#   3. dependência fora do ar        → pods saem do Service, sem reiniciar
#   4. aquecimento lento (75 s)      → ainda nenhum reinício
# Usa sempre --context kind-gitquest; nada fora do namespace desafio-18 é tocado.
# Duração típica: 3 a 4 minutos.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/cluster.sh
source env/cluster.sh

DEPLOY=warmup-api
SERVICE=warmup-api
SERVICE_URL="http://$SERVICE.$GQ_NS.svc.cluster.local"
WARMUP=25           # aquecimento padrão do serviço (segundos)
ROLLOUT_TIMEOUT=90  # rollout inicial completo
HANG_LIMIT=35       # travamento → contêiner reiniciado
UNREADY_LIMIT=20    # dependência caiu → pods fora do Service
DEP_HOLD=35         # tempo total com a dependência fora do ar
RECOVER_TIMEOUT=75  # dependência voltou → todas as réplicas de novo no Service
SLOW_WARMUP=75      # aquecimento de um nó frio
SLOW_TIMEOUT=$((SLOW_WARMUP + 45))

failures=0
pass() { printf '  %s✔%s %s\n' "$GQ_GREEN" "$GQ_RESET" "$1"; }
fail() {
  printf '  %s✘%s %s\n' "$GQ_RED" "$GQ_RESET" "$1"
  shift
  local line
  for line in "$@"; do printf '      %s\n' "$line"; done
  failures=$((failures + 1))
}
skip() { fail "$1" "não verificado: $2"; }
indent() { sed 's/^/      │ /'; }

# jp <recurso> <jsonpath> — lê um campo de um objeto do namespace do desafio.
jp() { k -n "$GQ_NS" get "$1" -o "jsonpath=$2" 2>/dev/null || true; }

events_of() { # events_of [pod] — eventos recentes (do pod, se informado)
  local sel=()
  [[ -n "${1:-}" ]] && sel=(--field-selector "involvedObject.name=$1")
  echo "      eventos recentes${1:+ de $1}:"
  k -n "$GQ_NS" get events "${sel[@]}" --sort-by=.lastTimestamp \
    -o custom-columns='TIPO:.type,MOTIVO:.reason,OBJETO:.involvedObject.name,MENSAGEM:.message' 2>/dev/null |
    tail -n 8 | cut -c1-200 | indent
}

pod_logs() { # pod_logs <pod> — log da execução anterior (se houve reinício) ou atual
  local out
  out=$(k -n "$GQ_NS" logs "$1" --previous --tail=5 2>/dev/null) ||
    out=$(k -n "$GQ_NS" logs "$1" --tail=5 2>/dev/null) || true
  if [[ -n "$out" ]]; then
    echo "      log do contêiner ($1):"
    printf '%s\n' "$out" | cut -c1-200 | indent
  fi
}

# probes_summary — as probes declaradas, como o servidor as vê.
probes_summary() {
  local kind
  echo "      probes do Deployment:"
  for kind in startupProbe livenessProbe readinessProbe; do
    local t="{.spec.template.spec.containers[0].$kind"
    local path port tcp exe
    path=$(jp "deploy/$DEPLOY" "$t.httpGet.path}")
    port=$(jp "deploy/$DEPLOY" "$t.httpGet.port}")
    tcp=$(jp "deploy/$DEPLOY" "$t.tcpSocket.port}")
    exe=$(jp "deploy/$DEPLOY" "$t.exec.command}")
    local what="(ausente)"
    if [[ -n "$port" ]]; then
      what="httpGet ${path:-/} porta $port"
    elif [[ -n "$tcp" ]]; then
      what="tcpSocket porta $tcp"
    elif [[ -n "$exe" ]]; then
      what="exec $exe"
    fi
    if [[ "$what" != "(ausente)" ]]; then
      what+=$(jp "deploy/$DEPLOY" \
        "{\" initialDelay=\"}$t.initialDelaySeconds}{\"s period=\"}$t.periodSeconds}{\"s timeout=\"}$t.timeoutSeconds}{\"s failureThreshold=\"}$t.failureThreshold}")
    fi
    what=${what//=s/=0s} # campo omitido: valor padrão (initialDelaySeconds = 0)
    printf '      │ %-15s %s\n' "$kind:" "$what"
  done
}

# Cada linha: nome|ready|reinícios|motivo-espera|último-término
pod_rows() {
  k -n "$GQ_NS" get pods -l "app=$DEPLOY" -o jsonpath='{range .items[*]}{.metadata.name}{"|"}{.status.containerStatuses[0].ready}{"|"}{.status.containerStatuses[0].restartCount}{"|"}{.status.containerStatuses[0].state.waiting.reason}{"|"}{.status.containerStatuses[0].lastState.terminated.reason}{"\n"}{end}' 2>/dev/null || true
}
restarts_of() { jp "pod/$1" '{.status.containerStatuses[0].restartCount}'; }

ready_endpoint_pods() {
  k -n "$GQ_NS" get endpointslices -l "kubernetes.io/service-name=$SERVICE" \
    -o jsonpath='{range .items[*].endpoints[?(@.conditions.ready==true)]}{.targetRef.name}{" "}{end}' 2>/dev/null || true
}
ready_endpoints() {
  k -n "$GQ_NS" get endpointslices -l "kubernetes.io/service-name=$SERVICE" \
    -o jsonpath='{range .items[*].endpoints[*]}{.conditions.ready}{"\n"}{end}' 2>/dev/null | grep -c '^true$' || true
}

# pod_get <pod> <url> — GET feito de dentro do pod (a imagem não tem shell nem curl).
pod_get() {
  MSYS_NO_PATHCONV=1 gq_timeout 20 kubectl --context "$GQ_CONTEXT" -n "$GQ_NS" \
    exec "$1" -- /app probe "$2" 2>&1 | tr -d '\r'
}
pricing_pod() { k -n "$GQ_NS" get pods -l app=pricing -o jsonpath='{.items[0].metadata.name}' 2>/dev/null || true; }
set_dependency() { # set_dependency up|down
  pod_get "$(pricing_pod)" "http://127.0.0.1:8080/admin/state?state=$1" >/dev/null
}

# ---------------------------------------------------------------------------
gq_title "desafio-18 · avaliando as probes de k8s/"
gq_require_docker
gq_require_cluster
compgen -G "k8s/*.y*ml" >/dev/null || gq_die "nenhum manifest .yaml encontrado em k8s/"

gq_info "Recriando o namespace $GQ_NS (cada execução começa do zero)"
k delete namespace "$GQ_NS" --ignore-not-found --wait=true --timeout=120s >/dev/null ||
  gq_die "não foi possível apagar o namespace $GQ_NS em 120s"
k create namespace "$GQ_NS" >/dev/null
k -n "$GQ_NS" apply -f env/pricing.yaml >/dev/null || gq_die "falha ao aplicar env/pricing.yaml"
k -n "$GQ_NS" rollout status deployment/pricing --timeout=90s >/dev/null ||
  gq_die "a dependência pricing (do ambiente, não sua) não ficou pronta. Rode ./start.sh e tente de novo."

gq_title "Critérios"

# 1. Manifests válidos e restritos ao warmup-api.
dry_out=""
if ! dry_out=$(k -n "$GQ_NS" apply --dry-run=server --validate=strict -f k8s/ -o name 2>&1); then
  fail "manifests válidos (kubectl apply --dry-run=server)" "o servidor rejeitou os manifests:"
  printf '%s\n' "$dry_out" | tail -n 8 | indent
  gq_die "corrija os erros de sintaxe/schema acima e rode ./test.sh de novo"
fi
bad_objects=$(printf '%s\n' "$dry_out" | grep -vxE "deployment\.apps/$DEPLOY|service/$SERVICE|configmap/.+" | grep -v '^$' || true)
if [[ -n "$bad_objects" ]]; then
  fail "manifests restritos ao warmup-api" "objetos não permitidos em k8s/ (a dependência pricing é do ambiente):" "$bad_objects"
  gq_die "declare apenas o Deployment e o Service warmup-api (e, se quiser, ConfigMaps)"
fi
if ! grep -qx "deployment.apps/$DEPLOY" <<<"$dry_out" || ! grep -qx "service/$SERVICE" <<<"$dry_out"; then
  gq_die "é preciso um Deployment e um Service chamados '$DEPLOY'; não renomeie os objetos do desafio"
fi
pass "manifests válidos no servidor (dry-run, validação estrita)"

apply_out=$(k -n "$GQ_NS" apply -f k8s/ 2>&1) || {
  printf '%s\n' "$apply_out" | indent
  gq_die "kubectl apply falhou"
}
applied_at=$SECONDS

container=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].name}')
image=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].image}')
override=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].command}{.spec.template.spec.containers[0].args}')
replicas=$(jp "deploy/$DEPLOY" '{.spec.replicas}')
replicas=${replicas:-1}

# 2. O serviço é o mesmo: imagem, réplicas, sem command/args.
if [[ ("$image" == "$GQ_APP_IMAGE" || "$image" == "docker.io/$GQ_APP_IMAGE") && -z "$override" ]] && ((replicas >= 2)); then
  pass "imagem $GQ_APP_IMAGE, sem command/args, $replicas réplicas"
else
  fail "Deployment do serviço original" \
    "esperado: imagem $GQ_APP_IMAGE, sem command/args, ao menos 2 réplicas" \
    "encontrado: imagem '$image', command/args '${override:-}', $replicas réplica(s)"
fi

# 3. Rollout: todas as réplicas Ready e NENHUM reinício durante o aquecimento.
rollout_ok=0 problem="" problem_pod=""
while ((SECONDS - applied_at < ROLLOUT_TIMEOUT)); do
  ready_count=0 total=0
  while IFS='|' read -r name ready restarts wreason lastterm; do
    [[ -n "$name" ]] || continue
    total=$((total + 1))
    [[ "$ready" == true ]] && ready_count=$((ready_count + 1))
    if [[ "${restarts:-0}" != 0 ]]; then
      problem="o pod $name foi reiniciado durante o aquecimento (reinícios: $restarts${lastterm:+, último término: $lastterm})"
      problem_pod=$name
    elif [[ "$wreason" =~ ^(ErrImagePull|ImagePullBackOff|ErrImageNeverPull|InvalidImageName|CreateContainerConfigError|CreateContainerError|CrashLoopBackOff)$ ]]; then
      problem="o contêiner de $name não sobe ($wreason)"
      problem_pod=$name
    fi
  done < <(pod_rows)
  [[ -n "$problem" ]] && break
  updated=$(jp "deploy/$DEPLOY" '{.status.updatedReplicas}')
  if ((total == replicas && ready_count == replicas)) && [[ "${updated:-0}" == "$replicas" ]]; then
    rollout_ok=1
    break
  fi
  sleep 2
done
rollout_secs=$((SECONDS - applied_at))
if ((rollout_ok)); then
  pass "rollout concluído sem reinícios: $replicas/$replicas réplicas Ready em ${rollout_secs}s (aquecimento de ${WARMUP}s)"
else
  [[ -n "$problem" ]] || problem="as réplicas não ficaram Ready em ${ROLLOUT_TIMEOUT}s (o aquecimento leva ${WARMUP}s)"
  fail "rollout sem reinícios durante o aquecimento" "$problem"
  probes_summary
  events_of "$problem_pod"
  [[ -n "$problem_pod" ]] && pod_logs "$problem_pod"
  echo "      investigue: kubectl --context $GQ_CONTEXT -n $GQ_NS describe pod ${problem_pod:-<pod>}"
fi

# 4. O serviço responde pelo Service, com o aquecimento original.
if ((rollout_ok)); then
  pods=()
  mapfile -t pods < <(k -n "$GQ_NS" get pods -l "app=$DEPLOY" -o jsonpath='{range .items[*]}{.metadata.name}{"\n"}{end}' | sort)
  health=$(pod_get "${pods[0]}" "http://127.0.0.1:8080/healthz" || true)
  warm=$(grep -oE '"warmup_seconds":[0-9]+' <<<"$health" | grep -oE '[0-9]+$' || echo 0)
  work=$(pod_get "$(pricing_pod)" "$SERVICE_URL/work" || true)
  if ! grep -q '"role":"warmup-api"' <<<"$health"; then
    fail "warmup-api respondendo" "GET /healthz na porta 8080 de ${pods[0]}: $(head -c 200 <<<"$health")"
  elif ((warm < WARMUP)); then
    fail "aquecimento original de ${WARMUP}s" "o serviço reporta warmup_seconds=$warm; encurtar o aquecimento não é uma correção aceita"
  elif ! grep -q '"recommendation"' <<<"$work"; then
    fail "GET $SERVICE_URL/work → 200" "resposta: $(head -c 200 <<<"$work")" \
      "investigue: kubectl --context $GQ_CONTEXT -n $GQ_NS get endpointslices,pods --show-labels"
  else
    pass "GET $SERVICE_URL/work → 200 (aquecimento de ${warm}s mantido)"
  fi
else
  skip "GET $SERVICE_URL/work → 200" "o rollout não terminou"
fi

# 5. Travamento: só o pod travado é reiniciado, e rápido.
if ((rollout_ok)); then
  victim=${pods[0]}
  gq_info "Travando o loop principal de $victim (GET /admin/hang)"
  pod_get "$victim" "http://127.0.0.1:8080/admin/hang" >/dev/null || true
  hung_at=$SECONDS
  victim_restarts=0
  while ((SECONDS - hung_at < HANG_LIMIT)); do
    victim_restarts=$(restarts_of "$victim")
    ((${victim_restarts:-0} > 0)) && break
    sleep 2
  done
  others=""
  for p in "${pods[@]:1}"; do
    r=$(restarts_of "$p")
    [[ "${r:-0}" == 0 ]] || others+="$p (reinícios: $r) "
  done
  if ((${victim_restarts:-0} > 0)) && [[ -z "$others" ]]; then
    pass "pod travado reiniciado pela liveness em $((SECONDS - hung_at))s (limite ${HANG_LIMIT}s); os outros intactos"
  elif ((${victim_restarts:-0} == 0)); then
    fail "pod travado reiniciado em até ${HANG_LIMIT}s" \
      "$victim não responde a nada desde o travamento, mas continua rodando sem reinício" \
      "(confira: kubectl --context $GQ_CONTEXT -n $GQ_NS exec $victim -- /app probe http://127.0.0.1:8080/healthz)"
    probes_summary
    events_of "$victim"
    # Tira o pod travado do caminho para não contaminar os próximos critérios.
    k -n "$GQ_NS" delete pod "$victim" --wait=false >/dev/null 2>&1 || true
    pods=("${pods[@]:1}")
  else
    fail "só o pod travado é reiniciado" "pods que não travaram também reiniciaram: $others"
    probes_summary
    events_of
  fi
else
  skip "pod travado reiniciado em até ${HANG_LIMIT}s" "o rollout não terminou"
fi

# 6 e 7. Dependência fora do ar: pods saem do Service, mas NÃO reiniciam.
if ((rollout_ok)); then
  declare -A before=()
  for p in "${pods[@]}"; do before[$p]=$(restarts_of "$p"); done
  survivor=${pods[${#pods[@]} - 1]}
  gq_info "Derrubando a dependência pricing por ${DEP_HOLD}s"
  set_dependency down
  down_at=$SECONDS
  left=0
  while ((SECONDS - down_at < UNREADY_LIMIT)); do
    if (($(ready_endpoints) == 0)); then
      left=1
      break
    fi
    sleep 1
  done
  left_secs=$((SECONDS - down_at))
  # O endpoint da liveness não pode depender do pricing.
  live_path=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].livenessProbe.httpGet.path}')
  live_port=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].livenessProbe.httpGet.port}')
  live_out=""
  if [[ -n "$live_port" ]]; then
    [[ "$live_port" =~ ^[0-9]+$ ]] ||
      live_port=$(jp "deploy/$DEPLOY" "{.spec.template.spec.containers[0].ports[?(@.name==\"$live_port\")].containerPort}")
    live_out=$(pod_get "$survivor" "http://127.0.0.1:${live_port:-0}${live_path:-/}" 2>&1) || live_out="FALHOU: $live_out"
  fi
  while ((SECONDS - down_at < DEP_HOLD)); do sleep 1; done
  restarted=""
  for p in "${pods[@]}"; do
    r=$(restarts_of "$p")
    [[ "${r:-0}" == "${before[$p]:-0}" ]] || restarted+="$p (reinícios: ${before[$p]:-0} → $r) "
  done

  if ((left)); then
    pass "dependência fora: nenhum pod pronto no Service em ${left_secs}s (limite ${UNREADY_LIMIT}s)"
  else
    fail "dependência fora: pods saem do Service em até ${UNREADY_LIMIT}s" \
      "depois de ${UNREADY_LIMIT}s ainda recebiam tráfego (que vai falhar): $(ready_endpoint_pods)" \
      "(GET /readyz no próprio pod $survivor: $(pod_get "$survivor" "http://127.0.0.1:8080/readyz" | head -n 1))"
    probes_summary
  fi
  if [[ -z "$restarted" && "$live_out" != FALHOU* ]]; then
    pass "dependência fora por ${DEP_HOLD}s: nenhum reinício (a liveness não depende do pricing)"
  elif [[ -n "$restarted" ]]; then
    fail "dependência fora: nenhum pod reiniciado" "reiniciar não traz o pricing de volta, e os pods voltariam a aquecer: $restarted"
    probes_summary
    events_of "$survivor"
  else
    fail "dependência fora: a liveness continua respondendo 200" \
      "GET ${live_path:-/} na porta da liveness, em $survivor: $(head -n 1 <<<"${live_out#FALHOU: }")"
    probes_summary
  fi

  gq_info "Restaurando a dependência pricing"
  set_dependency up
  up_at=$SECONDS
  back=0
  while ((SECONDS - up_at < RECOVER_TIMEOUT)); do
    if (($(ready_endpoints) >= replicas)); then
      back=1
      break
    fi
    sleep 2
  done
  if ((back)); then
    pass "dependência de volta: $replicas réplicas prontas no Service em $((SECONDS - up_at))s"
  else
    fail "dependência de volta: todas as réplicas voltam ao Service em até ${RECOVER_TIMEOUT}s" \
      "endpoints prontos: $(ready_endpoints) de $replicas"
    probes_summary
    events_of
  fi
else
  skip "dependência fora: pods saem do Service em até ${UNREADY_LIMIT}s" "o rollout não terminou"
  skip "dependência fora: nenhum pod reiniciado" "o rollout não terminou"
  skip "dependência de volta: réplicas voltam ao Service" "o rollout não terminou"
fi

# 8. Nó frio: o mesmo Deployment com aquecimento de 75 s não pode reiniciar.
if ((rollout_ok)); then
  gq_info "Simulando um nó frio: novo rollout com WARMUP_SECONDS=$SLOW_WARMUP (até ${SLOW_TIMEOUT}s)"
  patch=$(printf '{"spec":{"strategy":{"type":"RollingUpdate","rollingUpdate":{"maxSurge":"100%%","maxUnavailable":0}},"template":{"spec":{"containers":[{"name":"%s","env":[{"name":"WARMUP_SECONDS","value":"%s"}]}]}}}}' "$container" "$SLOW_WARMUP")
  k -n "$GQ_NS" patch "deploy/$DEPLOY" --type=strategic -p "$patch" >/dev/null
  slow_at=$SECONDS
  slow_ok=0 problem="" problem_pod=""
  while ((SECONDS - slow_at < SLOW_TIMEOUT)); do
    new_ready=0
    while IFS='|' read -r name warm_env ready restarts lastterm; do
      [[ -n "$name" && "$warm_env" == "$SLOW_WARMUP" ]] || continue
      [[ "$ready" == true ]] && new_ready=$((new_ready + 1))
      if [[ "${restarts:-0}" != 0 ]]; then
        problem="o pod $name foi reiniciado durante o aquecimento de ${SLOW_WARMUP}s (reinícios: $restarts${lastterm:+, último término: $lastterm})"
        problem_pod=$name
      fi
    done < <(k -n "$GQ_NS" get pods -l "app=$DEPLOY" -o jsonpath='{range .items[*]}{.metadata.name}{"|"}{.spec.containers[0].env[?(@.name=="WARMUP_SECONDS")].value}{"|"}{.status.containerStatuses[0].ready}{"|"}{.status.containerStatuses[0].restartCount}{"|"}{.status.containerStatuses[0].lastState.terminated.reason}{"\n"}{end}' 2>/dev/null || true)
    [[ -n "$problem" ]] && break
    if ((new_ready >= replicas)); then
      slow_ok=1
      break
    fi
    sleep 3
  done
  if ((slow_ok)); then
    pass "aquecimento de ${SLOW_WARMUP}s: $replicas réplicas Ready em $((SECONDS - slow_at))s, sem reinícios"
  else
    [[ -n "$problem" ]] || problem="as réplicas com aquecimento de ${SLOW_WARMUP}s não ficaram Ready em ${SLOW_TIMEOUT}s"
    fail "aquecimento lento (${SLOW_WARMUP}s) tolerado sem reinícios" "$problem"
    probes_summary
    events_of "$problem_pod"
  fi
else
  skip "aquecimento lento (${SLOW_WARMUP}s) tolerado sem reinícios" "o rollout não terminou"
fi

echo
if ((failures == 0)); then
  gq_ok "todos os critérios foram atendidos — as probes agora dizem a verdade!"
  exit 0
fi
gq_error "$failures critério(s) não atendido(s)."
echo "  Os objetos ficam no namespace $GQ_NS para você investigar:"
echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS get pods,endpointslices"
echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS describe pod <pod>"
exit 1
