#!/usr/bin/env bash
# Avaliador do desafio-19: código de saída 0 somente se todos os critérios passarem.
#
# Recria o namespace desafio-19 no cluster kind "gitquest", aplica k8s/ (versão
# 1.0.0) e encena uma sequência de releases enquanto um "prober" dentro do
# cluster faz 10 requisições por segundo ao Service:
#   1. rollout bom          1.0.0 -> 1.0.1
#   2. rollback             kubectl rollout undo (volta para 1.0.0)
#   3. rollout defeituoso   1.0.0 -> 2.0.0 (precisa ser barrado e detectado)
#   4. rollback             kubectl rollout undo (volta para 1.0.0)
#   5. PodDisruptionBudget  status e evictions de verdade
# Usa sempre --context kind-gitquest; nada fora do namespace desafio-19 é apagado.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/cluster.sh
source env/cluster.sh

INITIAL_TIMEOUT=120 # primeira subida da 1.0.0
ROLLOUT_TIMEOUT=150 # cada rollout bom / rollback
DEADLINE_BUDGET=100 # o rollout ruim precisa ser declarado falho em até isso
SETTLE=6            # observação depois de cada etapa (pods antigos encerrando)
DEPLOY=catalog-api
SERVICE=catalog-api
PROBER=catalog-prober
SERVICE_URL="http://$SERVICE.$GQ_NS.svc.cluster.local/items"
V_START=1.0.0 V_PATCH=1.0.1 V_BAD=2.0.0

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
    grep -v "$PROBER" | tail -n 8 | cut -c1-200 | indent
}

# Diagnóstico de rollout: histórico de revisões e ReplicaSets.
rollout_diag() {
  echo "      kubectl rollout history deploy/$DEPLOY:"
  k -n "$GQ_NS" rollout history "deploy/$DEPLOY" 2>&1 | grep -v '^$' | indent || true
  echo "      ReplicaSets:"
  k -n "$GQ_NS" get rs -o custom-columns='NOME:.metadata.name,REVISÃO:.metadata.annotations.deployment\.kubernetes\.io/revision,IMAGEM:.spec.template.spec.containers[0].image,DESEJADAS:.spec.replicas,PRONTAS:.status.readyReplicas,DISPONÍVEIS:.status.availableReplicas' 2>/dev/null |
    indent || true
}

# ---------------------------------------------------------------------------
# Acompanhamento de rollouts

# watch_rollout <timeout> <parar-no-prazo:1|0>
# Acompanha o Deployment a cada segundo até o rollout concluir (RESULT=complete),
# a condição Progressing virar ProgressDeadlineExceeded (RESULT=deadline, se
# pedido) ou o tempo acabar (RESULT=timeout). Registra o menor número de
# réplicas disponíveis observado (MIN_AVAILABLE).
watch_rollout() {
  local limit=$1 stop_on_deadline=$2 start=$SECONDS row gen obs want cur upd avail reason
  RESULT=timeout MIN_AVAILABLE=999 LAST_STATUS="?"
  while ((SECONDS - start < limit)); do
    row=$(jp "deploy/$DEPLOY" '{.metadata.generation}|{.status.observedGeneration}|{.spec.replicas}|{.status.replicas}|{.status.updatedReplicas}|{.status.availableReplicas}|{.status.conditions[?(@.type=="Progressing")].reason}')
    IFS='|' read -r gen obs want cur upd avail reason <<<"$row"
    cur=${cur:-0} upd=${upd:-0} avail=${avail:-0}
    ((avail < MIN_AVAILABLE)) && MIN_AVAILABLE=$avail
    LAST_STATUS="desejadas=$want, existentes=$cur, atualizadas=$upd, disponíveis=$avail, Progressing=${reason:-?}"
    if [[ -n "$gen" && "$gen" == "$obs" ]]; then
      if ((stop_on_deadline)) && [[ "$reason" == ProgressDeadlineExceeded ]]; then
        RESULT=deadline
        break
      fi
      if [[ "$upd" == "$want" && "$cur" == "$want" && "$avail" == "$want" ]]; then
        RESULT=complete
        break
      fi
    fi
    sleep 1
  done
  ELAPSED=$((SECONDS - start))
}

container=""
selector=""
replicas=0

# set_image <versão> — o "pipeline de deploy": troca a imagem e registra o motivo.
set_image() {
  k -n "$GQ_NS" set image "deploy/$DEPLOY" "$container=$GQ_APP_REPO:$1" >/dev/null
  k -n "$GQ_NS" annotate "deploy/$DEPLOY" --overwrite \
    "kubernetes.io/change-cause=avaliador: release $1" >/dev/null || true
}

# pod_versions — "<pod>=<versão>" de cada pod do Deployment que não está encerrando,
# perguntando a cada pod (GET /version de dentro do próprio contêiner).
pod_versions() {
  local name deleting out v
  while IFS='|' read -r name deleting; do
    [[ -n "$name" && -z "$deleting" ]] || continue
    out=$(MSYS_NO_PATHCONV=1 gq_timeout 20 kubectl --context "$GQ_CONTEXT" -n "$GQ_NS" \
      exec "$name" -c "$container" -- /app probe http://127.0.0.1:8080/version 2>&1 | tr -d '\r' || true)
    v=$(grep -oE '"version":"[^"]*"' <<<"$out" | cut -d'"' -f4 || true)
    printf '%s=%s\n' "$name" "${v:-?}"
  done < <(k -n "$GQ_NS" get pods -l "$selector" \
    -o jsonpath='{range .items[*]}{.metadata.name}{"|"}{.metadata.deletionTimestamp}{"\n"}{end}' 2>/dev/null || true)
}

# all_pods_at <versão> — sucesso se todos os pods ativos servem <versão>; VERSIONS guarda o detalhe.
VERSIONS=""
all_pods_at() {
  VERSIONS=$(pod_versions | paste -sd' ' -)
  [[ -n "$VERSIONS" ]] && ! tr ' ' '\n' <<<"$VERSIONS" | grep -qv "=$1\$"
}

# ---------------------------------------------------------------------------
# Prober: um pod com o próprio binário em modo loadcheck (GET contínuo ao Service).

start_prober() {
  k -n "$GQ_NS" apply -f - >/dev/null <<EOF
apiVersion: v1
kind: Pod
metadata:
  name: $PROBER
  labels:
    app.kubernetes.io/name: $PROBER
spec:
  restartPolicy: Never
  terminationGracePeriodSeconds: 2
  securityContext:
    runAsNonRoot: true
  containers:
    - name: prober
      image: $GQ_APP_REPO:$V_START
      imagePullPolicy: IfNotPresent
      args: ["loadcheck", "$SERVICE_URL"]
      resources:
        requests: {cpu: 20m, memory: 16Mi}
        limits: {memory: 64Mi}
      securityContext:
        allowPrivilegeEscalation: false
        readOnlyRootFilesystem: true
EOF
  k -n "$GQ_NS" wait --for=condition=Ready "pod/$PROBER" --timeout=60s >/dev/null 2>&1 ||
    gq_die "o pod $PROBER (prober do avaliador) não ficou pronto em 60s"
  local deadline=$((SECONDS + 20))
  until prober_logs | grep -qE '^STAT ok=[1-9]'; do
    ((SECONDS < deadline)) || {
      prober_logs | tail -n 5 | indent
      gq_die "o prober não conseguiu nenhuma resposta 200 de $SERVICE_URL (veja acima)"
    }
    sleep 2
  done
}

prober_logs() { k -n "$GQ_NS" logs "pod/$PROBER" 2>/dev/null || true; }
prober_fail_count() { prober_logs | grep -c '^FAIL' || true; }

phase_begin() { PHASE_FAILS_BEFORE=$(prober_fail_count); }

# phase_end — PHASE_FAILS = falhas do prober desde phase_begin; PHASE_SAMPLE = algumas delas.
phase_end() {
  sleep "$SETTLE"
  local logs status
  status=$(jp "pod/$PROBER" '{.status.phase}')
  [[ "$status" == Running ]] || gq_die "o prober do avaliador parou (fase $status); rode ./test.sh de novo"
  logs=$(prober_logs)
  PHASE_FAILS=$(($(grep -c '^FAIL' <<<"$logs" || true) - PHASE_FAILS_BEFORE))
  PHASE_SAMPLE=$(grep '^FAIL' <<<"$logs" | tail -n +"$((PHASE_FAILS_BEFORE + 1))" | head -n 5 | cut -c1-160 || true)
  PHASE_STAT=$(grep '^STAT' <<<"$logs" | tail -n 1 || true)
}

# check_availability <descrição> — critério "nenhuma requisição falhou e a
# capacidade nunca ficou abaixo de spec.replicas" para a etapa que terminou.
check_availability() {
  local what=$1 problems=()
  ((PHASE_FAILS == 0)) || problems+=("$PHASE_FAILS requisição(ões) ao Service falharam durante a etapa")
  ((MIN_AVAILABLE >= replicas)) ||
    problems+=("réplicas disponíveis caíram para $MIN_AVAILABLE (de $replicas) durante a etapa")
  if ((${#problems[@]} == 0)); then
    pass "disponibilidade durante $what: 0 falhas no prober, sempre >= $replicas réplicas disponíveis"
  else
    fail "disponibilidade durante $what" "${problems[@]}"
    if [[ -n "$PHASE_SAMPLE" ]]; then
      echo "      primeiras falhas vistas pelo prober (GET $SERVICE_URL):"
      printf '%s\n' "$PHASE_SAMPLE" | indent
    fi
    [[ -n "$PHASE_STAT" ]] && echo "      acumulado do prober: ${PHASE_STAT#STAT }"
  fi
}

skip_rest() { # skip_rest <motivo> <critério...>
  local why=$1 c
  shift
  for c in "$@"; do fail "$c" "não verificado: $why"; done
}

# ---------------------------------------------------------------------------
gq_title "desafio-19 · avaliando a estratégia de deploy de k8s/"
gq_require_docker
gq_require_cluster

compgen -G "k8s/*.y*ml" >/dev/null || gq_die "nenhum manifest .yaml encontrado em k8s/"

gq_info "Recriando o namespace $GQ_NS (cada execução começa do zero)"
k delete namespace "$GQ_NS" --ignore-not-found --wait=true --timeout=120s >/dev/null ||
  gq_die "não foi possível apagar o namespace $GQ_NS em 120s"
k create namespace "$GQ_NS" >/dev/null

gq_title "Critérios"

# 1. Manifests válidos, restritos aos objetos do desafio.
dry_out=""
if ! dry_out=$(k -n "$GQ_NS" apply --dry-run=server --validate=strict -f k8s/ -o name 2>&1); then
  fail "manifests válidos (kubectl apply --dry-run=server)" "o servidor rejeitou os manifests:"
  printf '%s\n' "$dry_out" | tail -n 8 | indent
  gq_die "corrija os erros de sintaxe/schema acima e rode ./test.sh de novo"
fi
bad_objects=$(printf '%s\n' "$dry_out" |
  grep -vE '^(service|deployment\.apps|poddisruptionbudget\.policy)/' | grep -v '^$' || true)
if [[ -n "$bad_objects" ]]; then
  fail "manifests restritos ao desafio" "objetos não permitidos neste desafio:" "$bad_objects"
  gq_die "use apenas Deployment, Service e PodDisruptionBudget"
fi
if ! grep -qx "deployment.apps/$DEPLOY" <<<"$dry_out" || ! grep -qx "service/$SERVICE" <<<"$dry_out" ||
  (($(grep -c '^deployment\.apps/' <<<"$dry_out") != 1)); then
  fail "manifests válidos" "é preciso exatamente um Deployment, chamado '$DEPLOY', e um Service '$SERVICE'"
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

# 2. Ponto de partida: 1.0.0, com redundância.
if [[ "$image" != "$GQ_APP_REPO:$V_START" && "$image" != "docker.io/$GQ_APP_REPO:$V_START" ]]; then
  fail "imagem inicial $GQ_APP_REPO:$V_START" "o Deployment usa '$image'; o avaliador parte da versão em produção"
  gq_die "mantenha a imagem $GQ_APP_REPO:$V_START em k8s/ (o avaliador troca a versão sozinho)"
fi
if ((replicas >= 2)); then
  pass "$replicas réplicas declaradas (mínimo 2: uma réplica sozinha não sobrevive a nenhuma troca)"
else
  fail "réplicas" "spec.replicas = $replicas; o catálogo precisa de ao menos 2 réplicas"
fi

# 3. Primeira subida.
watch_rollout "$INITIAL_TIMEOUT" 0
if [[ "$RESULT" != complete ]]; then
  fail "versão $V_START no ar" "o Deployment não ficou disponível em ${INITIAL_TIMEOUT}s ($LAST_STATUS)"
  recent_events
  skip_rest "a versão inicial não subiu" "rollout $V_START → $V_PATCH" "rollback após rollout concluído" \
    "rollout defeituoso $V_BAD barrado e detectado" "rollback do release defeituoso" "PodDisruptionBudget"
  echo
  gq_error "$failures critério(s) não atendido(s)."
  exit 1
fi
pass "versão $V_START no ar: $replicas/$replicas réplicas disponíveis em ${ELAPSED}s"

gq_info "Iniciando o prober: GET $SERVICE_URL a cada 100ms, de dentro do cluster"
start_prober

# 4. Rollout bom: 1.0.0 -> 1.0.1.
gq_info "Release $V_PATCH (versão boa): kubectl set image ... $container=$GQ_APP_REPO:$V_PATCH"
phase_begin
set_image "$V_PATCH"
watch_rollout "$ROLLOUT_TIMEOUT" 1
phase_end
stuck=""
if [[ "$RESULT" == complete ]] && all_pods_at "$V_PATCH"; then
  pass "rollout $V_START → $V_PATCH concluído em ${ELAPSED}s; todos os pods servem $V_PATCH"
else
  case "$RESULT" in
    complete) fail "rollout $V_START → $V_PATCH" "o rollout terminou, mas nem todos os pods servem $V_PATCH:" "versões dos pods: ${VERSIONS:-?}" ;;
    deadline) fail "rollout $V_START → $V_PATCH" "uma versão BOA foi declarada falha (ProgressDeadlineExceeded) após ${ELAPSED}s" \
      "progressDeadlineSeconds precisa dar tempo para o rollout normal terminar ($LAST_STATUS)" ;;
    *) fail "rollout $V_START → $V_PATCH" "não terminou em ${ROLLOUT_TIMEOUT}s ($LAST_STATUS)" ;;
  esac
  [[ "$RESULT" == complete ]] || stuck="o rollout da $V_PATCH não terminou"
  rollout_diag
  recent_events
fi
check_availability "o rollout $V_START → $V_PATCH"

if [[ -n "$stuck" ]]; then
  skip_rest "$stuck" "rollback após rollout concluído" "rollout defeituoso $V_BAD barrado e detectado" \
    "rollback do release defeituoso"
else
  # 5. Rollback de um rollout concluído: exige histórico de revisões.
  gq_info "Rollback da $V_PATCH: kubectl rollout undo deploy/$DEPLOY"
  phase_begin
  if undo_out=$(k -n "$GQ_NS" rollout undo "deploy/$DEPLOY" 2>&1); then
    watch_rollout "$ROLLOUT_TIMEOUT" 0
    phase_end
    if [[ "$RESULT" == complete ]] && all_pods_at "$V_START"; then
      pass "rollout undo após rollout concluído: de volta à $V_START em ${ELAPSED}s"
    else
      fail "rollback após rollout concluído" "o undo foi aceito, mas o Deployment não voltou à $V_START ($LAST_STATUS)" "versões dos pods: ${VERSIONS:-?}"
      rollout_diag
    fi
    check_availability "o rollback para $V_START"
  else
    phase_end
    fail "rollback após rollout concluído" "kubectl rollout undo falhou: $(head -n 1 <<<"$undo_out")" \
      "sem a revisão anterior guardada não há para onde voltar"
    rollout_diag
  fi

  # 6. Rollout defeituoso: 2.0.0 precisa ser barrado sem derrubar ninguém.
  before_bad=$(jp "deploy/$DEPLOY" '{.spec.template.spec.containers[0].image}')
  before_bad=${before_bad##*:}
  deadline_cfg=$(jp "deploy/$DEPLOY" '{.spec.progressDeadlineSeconds}')
  gq_info "Release $V_BAD (versão defeituosa): kubectl set image ... $container=$GQ_APP_REPO:$V_BAD"
  phase_begin
  set_image "$V_BAD"
  watch_rollout "$DEADLINE_BUDGET" 1
  phase_end
  case "$RESULT" in
    deadline)
      pass "rollout da $V_BAD barrado e detectado: ProgressDeadlineExceeded após ${ELAPSED}s (progressDeadlineSeconds=$deadline_cfg)" ;;
    complete)
      fail "rollout defeituoso $V_BAD barrado e detectado" \
        "o Deployment declarou a $V_BAD disponível e concluiu o rollout em ${ELAPSED}s — nada barrou a versão defeituosa" \
        "investigue: kubectl --context $GQ_CONTEXT -n $GQ_NS logs -l $selector --tail=20"
      rollout_diag ;;
    *)
      fail "rollout defeituoso $V_BAD barrado e detectado" \
        "nenhuma condição Progressing=False/ProgressDeadlineExceeded em ${DEADLINE_BUDGET}s (progressDeadlineSeconds=${deadline_cfg:-?})" \
        "estado: $LAST_STATUS"
      rollout_diag ;;
  esac
  check_availability "o rollout defeituoso $V_BAD"

  # 7. Rollback do release defeituoso.
  gq_info "Rollback da $V_BAD: kubectl rollout undo deploy/$DEPLOY"
  phase_begin
  if undo_out=$(k -n "$GQ_NS" rollout undo "deploy/$DEPLOY" 2>&1); then
    watch_rollout "$ROLLOUT_TIMEOUT" 0
    phase_end
    if [[ "$RESULT" == complete ]] && all_pods_at "$before_bad"; then
      pass "rollout undo da $V_BAD: todos os pods de volta à $before_bad em ${ELAPSED}s"
    else
      fail "rollback do release defeituoso" "o Deployment não voltou para a $before_bad ($LAST_STATUS)" "versões dos pods: ${VERSIONS:-?}"
      rollout_diag
    fi
    check_availability "o rollback do release defeituoso"
  else
    phase_end
    fail "rollback do release defeituoso" "kubectl rollout undo falhou: $(head -n 1 <<<"$undo_out")"
    rollout_diag
  fi
fi

k -n "$GQ_NS" delete pod "$PROBER" --wait=false >/dev/null 2>&1 || true

# 8. PodDisruptionBudget: existe, cobre os pods do Deployment, protege e não trava manutenção.
pdb_row=""
for _ in $(seq 1 15); do
  pdb_row=$(jp pdb '{range .items[*]}{.metadata.name}{"|"}{.status.expectedPods}{"|"}{.status.currentHealthy}{"|"}{.status.desiredHealthy}{"|"}{.status.disruptionsAllowed}{"\n"}{end}' |
    awk -F'|' -v n="$replicas" '$2 == n && $3 == n' | head -n 1)
  [[ -n "$pdb_row" ]] && break
  sleep 2
done
if [[ -z "$pdb_row" ]]; then
  existing=$(jp pdb '{range .items[*]}{.metadata.name}{" (pods esperados: "}{.status.expectedPods}{", saudáveis: "}{.status.currentHealthy}{")"}{"\n"}{end}')
  fail "PodDisruptionBudget cobrindo as $replicas réplicas" \
    "nenhum PDB cobre exatamente os $replicas pods do Deployment com todos saudáveis" \
    "PDBs no namespace: ${existing:-nenhum}" \
    "sem PDB, uma manutenção do nó (kubectl drain) pode remover todas as réplicas ao mesmo tempo"
else
  IFS='|' read -r pdb_name _ _ desired allowed <<<"$pdb_row"
  if ((allowed < 1)); then
    fail "PodDisruptionBudget coerente" "$pdb_name não permite nenhuma interrupção voluntária (disruptionsAllowed=0)" \
      "com todos os pods saudáveis, um drain de nó ficaria travado para sempre"
  elif ((desired < 1)); then
    fail "PodDisruptionBudget coerente" "$pdb_name exige $desired pod(s) saudável(is): não protege nada"
  else
    # Evictions de verdade, uma atrás da outra, como faria um kubectl drain.
    evicted=0 refused=0 ev_out=""
    while IFS= read -r pod; do
      [[ -n "$pod" ]] || continue
      body="{\"apiVersion\":\"policy/v1\",\"kind\":\"Eviction\",\"metadata\":{\"name\":\"$pod\",\"namespace\":\"$GQ_NS\"}}"
      if ev_out=$(printf '%s' "$body" | MSYS_NO_PATHCONV=1 kubectl --context "$GQ_CONTEXT" create --raw \
        "/api/v1/namespaces/$GQ_NS/pods/$pod/eviction" -f - 2>&1); then
        evicted=$((evicted + 1))
      elif grep -qiE 'disruption budget|TooManyRequests' <<<"$ev_out"; then
        refused=1
        break
      else
        break
      fi
    done < <(k -n "$GQ_NS" get pods -l "$selector" -o name 2>/dev/null | sed 's#^pod/##')
    if ((evicted >= 1 && refused)); then
      pass "PodDisruptionBudget $pdb_name: $evicted eviction(s) aceita(s), a seguinte recusada (mínimo saudável: $desired)"
    elif ((evicted == 0)); then
      fail "PodDisruptionBudget coerente" "a primeira eviction falhou: $(head -n 1 <<<"$ev_out" | tr -d '\r')"
    else
      fail "PodDisruptionBudget coerente" "$evicted eviction(s) aceitas em sequência sem nenhuma recusa: o PDB não protegeu o serviço" \
        "${ev_out:+última resposta: $(head -n 1 <<<"$ev_out" | tr -d '\r')}"
    fi
  fi
fi

echo
if ((failures == 0)); then
  gq_ok "todos os critérios foram atendidos — releases ruins não derrubam mais o catálogo!"
  exit 0
fi
gq_error "$failures critério(s) não atendido(s). Corrija um problema por vez e rode ./test.sh de novo."
echo "  Os objetos ficam no namespace $GQ_NS para você investigar:"
echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS rollout history deploy/$DEPLOY"
echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS get deploy,rs,pods,pdb"
exit 1
