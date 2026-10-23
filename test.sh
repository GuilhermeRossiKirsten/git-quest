#!/usr/bin/env bash
# Avaliador do desafio-25: código de saída 0 somente se todos os critérios passarem.
#
#   0. Recusa (ANTES de tocar no cluster) manifests com objetos fora da lista
#      permitida — inclusive qualquer recurso cluster-scoped — ou declarados em
#      outro namespace. O desafio não pode afetar nada fora de desafio-25.
#   1. Recria o namespace desafio-25 e aplica k8s/.
#   2. A loja continua funcionando: frontend → api → banco, e o job de
#      manutenção grava o relatório dele.
#   3. RBAC: matriz de "kubectl auth can-i" para cada ServiceAccount em uso.
#   4. Tokens de ServiceAccount só onde a API do Kubernetes é usada.
#   5. Rede: matriz de conexões executadas de dentro dos pods (deny-by-default).
# Usa sempre --context kind-gitquest; nada fora do namespace desafio-25 é tocado.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/cluster.sh
source env/cluster.sh

ROLLOUT_TIMEOUT=120
JOB_TIMEOUT=90
APPS=(frontend api db)
JOB_CRON=maintenance
REPORT_CM=maintenance-report
OTHER_CM=frontend-config
DB_SECRET=db-credentials
SA_PREFIX="system:serviceaccount:$GQ_NS"

# Tipos permitidos em k8s/ (todos com namespace). Qualquer outro é recusado.
ALLOWED_KINDS="ServiceAccount|v1 Secret|v1 ConfigMap|v1 Service|v1 Deployment|apps/v1 CronJob|batch/v1
Role|rbac.authorization.k8s.io/v1 RoleBinding|rbac.authorization.k8s.io/v1 NetworkPolicy|networking.k8s.io/v1"

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
section() { printf '\n%s%s%s\n' "$GQ_BOLD" "$1" "$GQ_RESET"; }

# jp <recurso> <jsonpath> — lê um campo de um objeto do namespace do desafio.
jp() { k -n "$GQ_NS" get "$1" -o "jsonpath=$2" 2>/dev/null || true; }

# kx <alvo> <args...> — roda /app <args> dentro de um pod (sem shell na imagem).
kx() {
  local target=$1
  shift
  MSYS_NO_PATHCONV=1 gq_timeout 25 kubectl --context "$GQ_CONTEXT" -n "$GQ_NS" \
    exec "$target" -- /app "$@" 2>&1 | tr -d '\r'
}

finish() {
  echo
  if ((failures == 0)); then
    gq_ok "todos os critérios foram atendidos — cada peça da loja só pode o que precisa!"
    exit 0
  fi
  gq_error "$failures critério(s) não atendido(s). Corrija um problema por vez e rode ./test.sh de novo."
  echo "  Os objetos ficam no namespace $GQ_NS para você investigar:"
  echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS get pods,sa,role,rolebinding,netpol"
  echo "    kubectl --context $GQ_CONTEXT -n $GQ_NS auth can-i --list --as=$SA_PREFIX:<serviceaccount>"
  exit 1
}

# ---------------------------------------------------------------------------
gq_title "desafio-25 · avaliando quem pode o quê em k8s/"
gq_require_docker
gq_require_cluster
compgen -G "k8s/*.y*ml" >/dev/null || gq_die "nenhum manifest .yaml encontrado em k8s/"

section "Escopo dos manifests (verificado antes de aplicar qualquer coisa)"
# Leitura local (--dry-run=client): nada é enviado ao cluster. Com -n, objetos sem
# namespace recebem desafio-25 e objetos de outro namespace fazem o kubectl recusar.
scope_tpl='{{define "o"}}{{.kind}} {{.apiVersion}} {{with .metadata.namespace}}{{.}}{{else}}<none>{{end}} {{.metadata.name}}{{"\n"}}{{end}}{{if .items}}{{range .items}}{{template "o" .}}{{end}}{{else}}{{template "o" .}}{{end}}'
if ! scope=$(MSYS_NO_PATHCONV=1 kubectl --context "$GQ_CONTEXT" -n "$GQ_NS" apply --dry-run=client -f k8s/ \
  -o go-template="$scope_tpl" 2>&1 | tr -d '\r'); then
  if grep -q 'does not match the namespace' <<<"$scope"; then
    fail "manifests restritos ao namespace $GQ_NS" "$(grep -m1 'does not match' <<<"$scope" | cut -c1-200)"
    gq_die "nada foi aplicado. Não declare outro namespace em k8s/: tudo vive em $GQ_NS"
  fi
  fail "manifests legíveis" "o kubectl não conseguiu ler k8s/:"
  printf '%s\n' "$scope" | tail -n 8 | indent
  gq_die "corrija a sintaxe dos manifests e rode ./test.sh de novo"
fi
violations=()
while read -r kind api ns name; do
  [[ -n "$kind" ]] || continue
  if ! grep -qwF -- "$kind|$api" <<<"$ALLOWED_KINDS"; then
    violations+=("$kind $name ($api): tipo não permitido neste desafio")
  elif [[ "$ns" != "<none>" && "$ns" != "$GQ_NS" ]]; then
    violations+=("$kind $name: declarado no namespace '$ns' (só $GQ_NS é permitido)")
  fi
done <<<"$scope"
if ((${#violations[@]})); then
  fail "manifests restritos ao namespace $GQ_NS" "${violations[@]}" \
    "tipos permitidos: $(tr '\n' ' ' <<<"$ALLOWED_KINDS" | sed 's/|[^ ]*//g')"
  gq_die "nada foi aplicado. Use só os tipos permitidos; recursos cluster-scoped (ClusterRole, ClusterRoleBinding, Namespace...) estão fora do escopo"
fi
pass "$(grep -c . <<<"$scope") objetos, todos de tipos com namespace e nenhum fora de $GQ_NS"

gq_info "Recriando o namespace $GQ_NS (cada execução começa do zero)"
k delete namespace "$GQ_NS" --ignore-not-found --wait=true --timeout=120s >/dev/null ||
  gq_die "não foi possível apagar o namespace $GQ_NS em 120s"
k create namespace "$GQ_NS" >/dev/null

if ! dry_out=$(k -n "$GQ_NS" apply --dry-run=server --validate=strict -f k8s/ -o name 2>&1); then
  fail "manifests válidos (kubectl apply --dry-run=server)" "o servidor rejeitou os manifests:"
  printf '%s\n' "$dry_out" | tail -n 8 | indent
  gq_die "corrija os erros acima e rode ./test.sh de novo"
fi
missing=()
for obj in deployment.apps/frontend deployment.apps/api deployment.apps/db service/frontend service/api \
  service/db cronjob.batch/$JOB_CRON configmap/$REPORT_CM configmap/$OTHER_CM secret/$DB_SECRET; do
  grep -qx "$obj" <<<"$dry_out" || missing+=("$obj")
done
if ((${#missing[@]})); then
  fail "objetos da loja presentes" "faltam em k8s/: ${missing[*]}"
  gq_die "não renomeie nem remova os objetos da loja"
fi
pass "manifests válidos no servidor (dry-run, validação estrita)"

apply_out=$(k -n "$GQ_NS" apply -f k8s/ 2>&1) || {
  printf '%s\n' "$apply_out" | indent
  gq_die "kubectl apply falhou"
}
# O avaliador dispara o job sozinho; execuções agendadas atrapalhariam a leitura do relatório.
k -n "$GQ_NS" patch "cronjob/$JOB_CRON" --type=merge -p '{"spec":{"suspend":true}}' >/dev/null

# ---------------------------------------------------------------------------
section "A loja continua funcionando"

app_up=1
for app in "${APPS[@]}"; do
  if ! out=$(k -n "$GQ_NS" rollout status "deploy/$app" --timeout="${ROLLOUT_TIMEOUT}s" 2>&1); then
    app_up=0
    fail "deployment $app disponível" "não ficou pronto em ${ROLLOUT_TIMEOUT}s: $(tail -n 1 <<<"$out")"
    k -n "$GQ_NS" get events --field-selector type=Warning \
      -o custom-columns='OBJETO:.involvedObject.name,MOTIVO:.reason,MENSAGEM:.message' 2>/dev/null |
      grep -E "^(NAME|OBJETO|$app)" | tail -n 5 | cut -c1-200 | indent || true
  fi
done
((app_up)) && pass "frontend, api e db disponíveis"

# Fluxo ponta a ponta, pedido ao próprio frontend (loopback: não depende de quem pode chegar nele).
e2e=""
if ((app_up)); then
  for _ in 1 2 3 4 5; do
    e2e=$(kx deploy/frontend probe http://127.0.0.1:8080/ || true)
    grep -q 'PED-1001' <<<"$e2e" && break
    sleep 3
  done
fi
if grep -q 'PED-1001' <<<"$e2e"; then
  pass "frontend → api → banco: a página lista os pedidos ($(head -n 2 <<<"$e2e" | tail -n 1 | cut -c1-60))"
else
  fail "frontend → api → banco" "GET / no frontend não mostrou os pedidos do banco:"
  printf '%s\n' "${e2e:-(sem resposta)}" | head -n 4 | cut -c1-200 | indent
fi

# O job de manutenção, disparado a partir do CronJob, precisa gravar o relatório.
job="gq-maintenance-$(date +%s)"
job_ok=0 job_pod=""
if k -n "$GQ_NS" create job "$job" --from="cronjob/$JOB_CRON" >/dev/null 2>&1; then
  deadline=$((SECONDS + JOB_TIMEOUT)) state=""
  while ((SECONDS < deadline)); do
    state=$(jp "job/$job" '{.status.succeeded}|{.status.failed}')
    [[ "$state" == 1\|* || "$state" == *\|[1-9]* ]] && break
    sleep 2
  done
  job_pod=$(k -n "$GQ_NS" get pods -l "job-name=$job" -o 'jsonpath={.items[0].metadata.name}' 2>/dev/null || true)
  report_by=$(jp "configmap/$REPORT_CM" '{.data.lastRunBy}')
  report_pods=$(jp "configmap/$REPORT_CM" '{.data.pods}')
  if [[ "$state" == 1\|* && -n "$job_pod" && "$report_by" == "$job_pod" && "$report_pods" == *frontend=* ]]; then
    job_ok=1
    pass "job de manutenção gravou o relatório $REPORT_CM (pods: $report_pods)"
  else
    fail "job de manutenção grava o relatório $REPORT_CM" \
      "estado do job: ${state:-?} (sucesso|falha); relatório escrito por '${report_by:-ninguém}', esperado '${job_pod:-?}'"
    if [[ -n "$job_pod" ]]; then
      echo "      kubectl logs $job_pod:"
      k -n "$GQ_NS" logs "$job_pod" 2>&1 | tail -n 5 | cut -c1-220 | indent || true
    fi
  fi
else
  fail "job de manutenção grava o relatório $REPORT_CM" "kubectl create job --from=cronjob/$JOB_CRON falhou"
fi

# Pods auxiliares do avaliador (sem token, mesma imagem, modo "idle"):
#   gq-outsider  um pod qualquer do namespace com saída liberada pelo avaliador:
#                testa as regras de ENTRADA de cada componente isoladamente;
#   gq-newcomer  um pod novo, sem nenhuma regra escrita para ele: testa o
#                deny-by-default (entrada e saída);
#   gq-job-twin  um pod com os mesmos labels dos pods do job de manutenção.
# shellcheck disable=SC2016  # go-template: os $ pertencem ao template, não ao shell
twin_labels=$(k -n "$GQ_NS" get "cronjob/$JOB_CRON" \
  -o go-template='{{range $k, $v := .spec.jobTemplate.spec.template.metadata.labels}}    {{$k}}: "{{$v}}"{{"\n"}}{{end}}' 2>/dev/null || true)
helper_pod() { # helper_pod <nome> <papel> <labels extras (yaml)>
  cat <<EOF
---
apiVersion: v1
kind: Pod
metadata:
  name: $1
  labels:
    gitquest.dev/evaluator: $2
$3
spec:
  automountServiceAccountToken: false
  terminationGracePeriodSeconds: 1
  securityContext:
    runAsNonRoot: true
  containers:
    - name: helper
      image: $GQ_APP_IMAGE
      imagePullPolicy: IfNotPresent
      args: ["idle"]
      ports: [{name: http, containerPort: 8080}]
      readinessProbe: {httpGet: {path: /healthz, port: http}, periodSeconds: 2}
      resources: {requests: {cpu: 10m, memory: 16Mi}, limits: {memory: 64Mi}}
      securityContext: {allowPrivilegeEscalation: false, readOnlyRootFilesystem: true, capabilities: {drop: ["ALL"]}}
EOF
}
{
  helper_pod gq-outsider outsider ""
  helper_pod gq-newcomer newcomer '    app.kubernetes.io/name: newcomer'
  helper_pod gq-job-twin job-twin "$twin_labels"
  cat <<EOF
---
# NetworkPolicies somam permissões: esta libera a saída do gq-outsider, qualquer
# que seja o deny-by-default do participante.
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: gq-evaluator-outsider-egress
spec:
  podSelector: {matchLabels: {gitquest.dev/evaluator: outsider}}
  policyTypes: [Egress]
  egress: [{}]
EOF
} | k -n "$GQ_NS" apply -f - >/dev/null || gq_die "não foi possível criar os pods auxiliares do avaliador"

# ---------------------------------------------------------------------------
section "Identidades (ServiceAccounts em uso)"

sa_of() { # sa_of <recurso> <caminho até o pod spec>
  local sa
  sa=$(jp "$1" "{$2.serviceAccountName}")
  echo "${sa:-default}"
}
declare -A SA
for app in "${APPS[@]}"; do SA[$app]=$(sa_of "deploy/$app" .spec.template.spec); done
SA[job]=$(sa_of "cronjob/$JOB_CRON" .spec.jobTemplate.spec.template.spec)
summary="frontend=${SA[frontend]}, api=${SA[api]}, db=${SA[db]}, job=${SA[job]}"
on_default=()
for c in frontend api db job; do [[ "${SA[$c]}" == default ]] && on_default+=("$c"); done
if ((${#on_default[@]})); then
  fail "nenhum componente usa a ServiceAccount 'default'" "usam a default: ${on_default[*]} ($summary)" \
    "a 'default' é dada a qualquer pod criado sem identidade; ela não deve carregar permissões nem ser compartilhada"
else
  pass "nenhum componente usa a ServiceAccount 'default' ($summary)"
fi
shared=()
for app in "${APPS[@]}"; do [[ "${SA[$app]}" == "${SA[job]}" ]] && shared+=("$app"); done
if ((${#shared[@]})); then
  fail "o job tem uma identidade só dele" "a ServiceAccount '${SA[job]}' do job também é usada por: ${shared[*]}"
else
  pass "a ServiceAccount do job ('${SA[job]}') não é compartilhada"
fi

# ---------------------------------------------------------------------------
section "RBAC: kubectl auth can-i --as=$SA_PREFIX:<sa>"

# can_i <sa> <verbo> <recurso> [namespace] [flags...] -> "yes" | "no"
can_i() {
  local sa=$1 verb=$2 res=$3 ns=${4:-$GQ_NS}
  shift 3
  (($#)) && shift
  k auth can-i "$verb" "$res" -n "$ns" "$@" --as="$SA_PREFIX:$sa" \
    --as-group=system:serviceaccounts --as-group="system:serviceaccounts:$GQ_NS" \
    --as-group=system:authenticated 2>/dev/null | head -n 1 || true
}

# check_matrix <sa> <descrição> <esperado: yes|no> <linhas "verbo recurso [ns] [flags]">
# Imprime as linhas cuja resposta difere do esperado.
check_matrix() {
  local sa=$1 want=$2 verb res ns flag got
  while read -r verb res ns flag; do
    [[ -n "$verb" ]] || continue
    [[ "$ns" == - ]] && ns=""
    if [[ -n "$flag" ]]; then
      got=$(can_i "$sa" "$verb" "$res" "${ns:-$GQ_NS}" "$flag")
    else
      got=$(can_i "$sa" "$verb" "$res" "${ns:-$GQ_NS}")
    fi
    [[ "$got" == "$want" ]] || printf '%s %s%s%s -> %s\n' "$verb" "$res" "${flag:+ $flag}" \
      "${ns:+ -n $ns}" "${got:-erro}"
  done
}

JOB_MUST='list pods
get configmaps/maintenance-report
update configmaps/maintenance-report'
JOB_MUST_NOT='get secrets/db-credentials
list secrets
get configmaps/frontend-config
update configmaps/frontend-config
list configmaps
create configmaps
delete configmaps/maintenance-report
create pods
delete pods
create pods - --subresource=exec
patch deployments.apps
create serviceaccounts - --subresource=token
impersonate serviceaccounts
create rolebindings.rbac.authorization.k8s.io
escalate roles.rbac.authorization.k8s.io
bind clusterroles.rbac.authorization.k8s.io
* *
list pods kube-system
update configmaps default'
NOBODY_MUST_NOT='get pods
list pods
get configmaps/frontend-config
update configmaps/maintenance-report
get secrets/db-credentials
list secrets
create pods
create pods - --subresource=exec
create deployments.apps
* *'

if ((job_ok)); then
  bad=$(check_matrix "${SA[job]}" yes <<<"$JOB_MUST")
  if [[ -z "$bad" ]]; then
    pass "job (${SA[job]}) pode o que precisa: list pods, get/update configmaps/$REPORT_CM"
  else
    fail "job (${SA[job]}) pode o que precisa" "negado, mas necessário:"
    printf '%s\n' "$bad" | indent
  fi
else
  bad=$(check_matrix "${SA[job]}" yes <<<"$JOB_MUST")
  fail "job (${SA[job]}) pode o que precisa" "o job não gravou o relatório (veja acima)" \
    ${bad:+"negado, mas necessário: $(paste -sd';' - <<<"$bad")"}
fi
bad=$(check_matrix "${SA[job]}" no <<<"$JOB_MUST_NOT")
if [[ -z "$bad" ]]; then
  pass "job (${SA[job]}) não pode mais nada: $(grep -c . <<<"$JOB_MUST_NOT") ações proibidas negadas (secrets, exec, outros ConfigMaps, outros namespaces, escalate/bind...)"
else
  fail "job (${SA[job]}) só pode o que precisa (menor privilégio)" "permitido, mas deveria ser negado:"
  printf '%s\n' "$bad" | indent
fi

checked=" "
for c in default frontend api db; do
  sa=${SA[$c]:-default}
  [[ "$c" == default ]] && sa=default
  [[ "$checked" == *" $sa "* || "$sa" == "${SA[job]}" ]] && continue
  checked+="$sa "
  users=""
  for app in "${APPS[@]}"; do [[ "${SA[$app]}" == "$sa" ]] && users+="$app "; done
  label="ServiceAccount '$sa'${users:+ (usada por: ${users% })}"
  [[ "$sa" == default ]] && label+="${users:+;} a que todo pod sem identidade recebe"
  bad=$(check_matrix "$sa" no <<<"$NOBODY_MUST_NOT")
  if [[ -z "$bad" ]]; then
    pass "$label: sem permissões na API"
  else
    fail "$label não precisa de permissão nenhuma" "permitido, mas deveria ser negado:"
    printf '%s\n' "$bad" | indent
  fi
done

# ---------------------------------------------------------------------------
section "Tokens de ServiceAccount montados (verificado dentro dos pods em execução)"
if ((app_up)); then
  with_token=() details=()
  for app in "${APPS[@]}"; do
    out=$(kx "deploy/$app" tokencheck || true)
    case "$out" in
      *"token: ausente"*) ;;
      *"token: presente"*) with_token+=("$app") ;;
      *) details+=("$app: não foi possível verificar ($(head -n 1 <<<"$out" | cut -c1-120))") ;;
    esac
  done
  if ((${#with_token[@]} == 0 && ${#details[@]} == 0)); then
    pass "frontend, api e db rodam sem token (nenhum deles usa a API do Kubernetes)"
  else
    fail "sem token onde a API do Kubernetes não é usada" \
      ${with_token[@]+"com token montado em /var/run/secrets/kubernetes.io/serviceaccount: ${with_token[*]}"} \
      ${details[@]+"${details[@]}"} \
      "quem invadir esses pods herda a identidade do token, mesmo que o código nunca o use"
  fi
  if ((job_ok)); then
    pass "o job, que usa a API, recebeu o token e funcionou"
  fi
else
  fail "sem token onde a API do Kubernetes não é usada" "não verificado: a loja não subiu"
fi

# ---------------------------------------------------------------------------
section "Rede: conexões testadas de dentro dos pods"

svc_addr() { # svc_addr <service> <dns|ip> -> host:porta
  local ip port
  port=$(jp "service/$1" '{.spec.ports[0].port}')
  if [[ "$2" == dns ]]; then
    echo "$1:${port:-0}"
  else
    ip=$(jp "service/$1" '{.spec.clusterIP}')
    echo "${ip:-0.0.0.0}:${port:-0}"
  fi
}
k -n "$GQ_NS" wait --for=condition=Ready pod/gq-outsider pod/gq-newcomer pod/gq-job-twin --timeout=60s >/dev/null 2>&1 ||
  gq_die "os pods auxiliares do avaliador não ficaram prontos em 60s"
newcomer_ip=$(jp pod/gq-newcomer '{.status.podIP}')

# connect_from <origem> <destino> -> código de /app connect (0 ok, 2 dns, 3 sem resposta, 4 recusada)
CONNECT_OUT=""
connect_from() {
  local rc=0
  CONNECT_OUT=$(kx "$1" connect "$2") || rc=$?
  if ((rc != 0)) && ! grep -q '^ERRO' <<<"$CONNECT_OUT"; then rc=99; fi
  return "$rc"
}

expect_allowed() { # expect_allowed <origem> <rótulo-origem> <destino> <rótulo-destino> <por quê>
  local rc _
  for _ in 1 2 3; do
    rc=0
    connect_from "$1" "$3" || rc=$?
    ((rc == 0)) && break
    sleep 2
  done
  if ((rc == 0)); then
    pass "$2 → $4 permitido ($5)"
    return
  fi
  local why
  case $rc in
    2) why="o nome não resolveu: o pod consegue falar com o DNS do cluster (kube-dns, porta 53 UDP e TCP)?" ;;
    3) why="pacotes descartados: alguma NetworkPolicy bloqueia esse fluxo (saída da origem ou entrada do destino)" ;;
    4) why="conexão recusada: o destino está ouvindo nessa porta?" ;;
    *) why="não foi possível executar o teste" ;;
  esac
  fail "$2 → $4 deveria ser permitido ($5)" "$(head -n 1 <<<"$CONNECT_OUT" | cut -c1-200)" "$why"
}

expect_denied() { # expect_denied <origem> <rótulo-origem> <destino> <rótulo-destino> <por quê>
  local rc=0
  connect_from "$1" "$3" || rc=$?
  if ((rc == 0)); then # confirma, para não culpar uma regra recém-programada
    sleep 3
    rc=0
    connect_from "$1" "$3" || rc=$?
  fi
  case $rc in
    3 | 4) pass "$2 ↛ $4 bloqueado ($5)" ;;
    0) fail "$2 → $4 deveria ser bloqueado ($5)" "$(head -n 1 <<<"$CONNECT_OUT" | cut -c1-200)" ;;
    *) fail "$2 → $4 deveria ser bloqueado ($5)" "não verificado: $(head -n 1 <<<"$CONNECT_OUT" | cut -c1-200)" ;;
  esac
}

if ((app_up)); then
  echo "  fluxos necessários (por nome DNS):"
  expect_allowed deploy/frontend frontend "$(svc_addr api dns)" api "o frontend consulta a api"
  expect_allowed deploy/api api "$(svc_addr db dns)" db "a api consulta o banco"
  expect_allowed pod/gq-outsider "outro pod" "$(svc_addr frontend dns)" frontend "o frontend é a porta de entrada"
  echo "  fluxos proibidos (por IP, sem depender do DNS):"
  expect_denied pod/gq-outsider "outro pod" "$(svc_addr db ip)" db "só a api fala com o banco"
  expect_denied pod/gq-outsider "outro pod" "$(svc_addr api ip)" api "só o frontend chama a api"
  expect_denied deploy/frontend frontend "$(svc_addr db ip)" db "o frontend não pula a api"
  expect_denied pod/gq-job-twin "job de manutenção" "$(svc_addr db ip)" db "o job só fala com a API do Kubernetes"
  expect_denied pod/gq-job-twin "job de manutenção" "$(svc_addr frontend ip)" frontend "saída do job"
  expect_denied deploy/api api "$(svc_addr frontend ip)" frontend "saída da api: só o banco"
  expect_denied deploy/db db "$(svc_addr frontend ip)" frontend "o banco não inicia conexões"
  echo "  deny-by-default (um pod novo, sem regras escritas para ele):"
  expect_denied pod/gq-outsider "outro pod" "${newcomer_ip:-0.0.0.0}:8080" "pod novo" "entrada negada por padrão"
  expect_denied pod/gq-newcomer "pod novo" "$(svc_addr frontend ip)" frontend "saída negada por padrão"
else
  fail "matriz de conectividade" "não verificada: a loja não subiu"
fi

# Origens fora do namespace: o avaliador não cria pods em outros namespaces, então
# este critério lê as políticas APLICADAS no cluster. Uma regra de entrada sem "from"
# ou com namespaceSelector/ipBlock só é aceitável no frontend (a porta de entrada).
echo "  origens de outros namespaces (políticas aplicadas):"
broad=$(k -n "$GQ_NS" get networkpolicy,deployment,cronjob -o json | "$(gq_python)" -c '
import json, sys

items = json.load(sys.stdin)["items"]
GUARDED = {("Deployment", "api"), ("Deployment", "db"), ("CronJob", "maintenance")}

def template_labels(obj):
    spec = obj["spec"]
    if obj["kind"] == "CronJob":
        spec = spec["jobTemplate"]["spec"]
    return spec["template"]["metadata"].get("labels") or {}

def selects(sel, labels):
    for key, val in (sel.get("matchLabels") or {}).items():
        if labels.get(key) != val:
            return False
    for expr in sel.get("matchExpressions") or []:
        key, op, vals = expr["key"], expr["operator"], expr.get("values") or []
        if (op == "In" and labels.get(key) not in vals) \
                or (op == "NotIn" and key in labels and labels[key] in vals) \
                or (op == "Exists" and key not in labels) \
                or (op == "DoesNotExist" and key in labels):
            return False
    return True

guarded = {o["metadata"]["name"]: template_labels(o) for o in items
           if (o["kind"], o["metadata"]["name"]) in GUARDED}
for pol in items:
    if pol["kind"] != "NetworkPolicy":
        continue
    for rule in pol["spec"].get("ingress") or []:
        peers = rule.get("from")
        if not peers:
            why = "regra de entrada sem \"from\" (qualquer origem, de qualquer namespace)"
        elif any("namespaceSelector" in p or "ipBlock" in p for p in peers):
            why = "origem com namespaceSelector/ipBlock"
        else:
            continue
        hit = sorted(n for n, labels in guarded.items()
                     if selects(pol["spec"].get("podSelector") or {}, labels))
        if hit:
            print("%s: %s vale para %s" % (pol["metadata"]["name"], why, ", ".join(hit)))
')
if [[ -z "$broad" ]]; then
  pass "api, banco e job não aceitam conexões de outros namespaces nem de IPs arbitrários"
else
  mapfile -t broad_lines <<<"$broad"
  fail "api, banco e job não aceitam conexões de outros namespaces nem de IPs arbitrários" \
    "${broad_lines[@]}" "(só o frontend é porta de entrada; use podSelector sem namespaceSelector)"
fi

k -n "$GQ_NS" delete pod gq-outsider gq-newcomer gq-job-twin --wait=false >/dev/null 2>&1 || true
k -n "$GQ_NS" delete networkpolicy gq-evaluator-outsider-egress >/dev/null 2>&1 || true
finish
