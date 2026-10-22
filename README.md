# desafio-25 · Quem pode o quê

> Trilha: kubernetes · Nível: advanced · ~75 min · 250 pontos

## Contexto

Sexta-feira, 16h40. O time de segurança terminou o pentest da loja e mandou um relatório
de duas linhas: *"A partir de uma falha no frontend conseguimos ler a senha do banco,
executar comandos no pod do banco (`pods/exec`) e consultar os pedidos direto na porta 5432. Nenhuma
dessas ações exigiu outra vulnerabilidade."*

Ninguém mexeu no código. O frontend, a api, o banco e o job de manutenção funcionam
exatamente como antes — e é justamente por isso que ninguém percebeu. O problema não está
no que cada peça **faz**, e sim no que cada peça **pode fazer**. Segunda-feira o relatório
vai para a diretoria; até lá, cada componente precisa poder só o que precisa.

## Problema

A loja roda no namespace `desafio-25` e é instalada pelos manifests de `k8s/`:

| Componente | O que faz | Precisa de |
|---|---|---|
| `frontend` (Deployment + Service `frontend:80`) | mostra os pedidos; é a porta de entrada da loja | chamar a `api` |
| `api` (Deployment + Service `api:8080`) | consulta o banco | falar com o `db` (senha no Secret `db-credentials`, entregue como variável de ambiente) |
| `db` (Deployment + Service `db:5432`) | "banco" de pedidos (TCP) | só responder à `api` |
| `maintenance` (CronJob) | lista os pods do namespace e grava um resumo no ConfigMap `maintenance-report` | a API do Kubernetes |

O que o pentest observou (reproduza tudo você mesmo):

- de dentro do pod do frontend, o token em `/var/run/secrets/kubernetes.io/serviceaccount/`
  responde "yes" para quase qualquer pergunta: ler Secrets, criar pods, `pods/exec`,
  criar RoleBindings...;
- `kubectl auth can-i` mostra que o job de manutenção pode muito mais do que listar pods e
  atualizar o próprio relatório;
- qualquer pod do namespace abre conexão com o banco na porta 5432 e com a api na 8080 —
  apesar de existir uma NetworkPolicy chamada `db-only-from-api`;
- um pod novo, criado por qualquer pessoa no namespace, recebe e inicia conexões à vontade.

Uma tentativa anterior de "fechar a rede" com uma política que bloqueava tudo derrubou a
loja inteira em segundos — inclusive a resolução de nomes. O que você precisa entregar é
**menor privilégio sem quebrar nada**: a loja continua mostrando os pedidos e o job continua
gravando o relatório.

O código (`app/`) **não deve ser alterado**: leia-o para descobrir exatamente quais
chamadas à API e quais conexões cada componente faz. A imagem não tem shell; o próprio
binário traz ferramentas de diagnóstico para usar com `kubectl exec`:

```bash
NS="--context kind-gitquest -n desafio-25"
kubectl $NS exec deploy/frontend -- /app connect db:5432        # conecta? DNS? descartado?
kubectl $NS exec deploy/frontend -- /app tokencheck             # há token montado?
kubectl $NS exec deploy/frontend -- /app probe http://127.0.0.1:8080/
kubectl $NS auth can-i --list --as=system:serviceaccount:desafio-25:default
kubectl $NS auth can-i get secrets --as=system:serviceaccount:desafio-25:default
kubectl $NS describe networkpolicy
kubectl $NS get pods --show-labels
kubectl $NS create job manual-1 --from=cronjob/maintenance && kubectl $NS logs job/manual-1
```

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Dar a cada workload uma ServiceAccount própria; a `default` fica sem permissão nenhuma.
- Escrever `Role`/`RoleBinding` de menor privilégio (verbos exatos, `resourceNames`, sem `*`).
- Auditar permissões com `kubectl auth can-i --as=system:serviceaccount:<ns>:<sa>`.
- Desligar `automountServiceAccountToken` onde a API não é usada.
- Isolar o namespace com NetworkPolicy deny-by-default (entrada **e** saída) sem quebrar o DNS.

## Conhecimentos necessários

- Deployments, Services, labels e seletores no kind (desafio-10 e desafio-19).
- Noções de TCP/DNS: portas, resolução de nomes, o que é um Service (ClusterIP).
- Leitura de Go simples (para descobrir o que cada componente realmente faz em `app/main.go`).

## Ambiente

- Docker >= 20.10 (Docker Desktop com WSL2 no Windows/macOS), com ao menos 4 GB de memória.
- Bash (Linux, macOS, WSL2 ou Git Bash).
- `kind` v0.27.0 e `kubectl` 1.31–1.33: se não estiverem no `PATH`, o `./start.sh` baixa
  `kind v0.27.0` e `kubectl v1.32.2` para `./.tools/bin`, conferindo o sha256. Para usá-los
  no terminal: `export PATH="$PWD/.tools/bin:$PATH"`.
- Cluster kind `gitquest` (um nó, Kubernetes v1.32.2, CNI padrão do kind — o kindnet,
  que **aplica** NetworkPolicy), contexto `kind-gitquest`, compartilhado com os outros
  desafios da trilha. Os scripts **nunca** usam o seu contexto atual e só mexem no namespace
  `desafio-25`.
- Imagem `gitquest/desafio-25-shop:1.0.0` (um binário para todos os componentes), construída
  a partir de `app/` e carregada no cluster com `kind load docker-image` (não há registry).
- Particularidade do cluster de um nó: o tráfego de um pod para o **próprio nó** (API server,
  kubelet) não passa pelas NetworkPolicies. Por isso, aqui, quem protege a API do Kubernetes
  é o RBAC e a ausência de token — não a rede. Escreva mesmo assim a saída do job para a API
  como faria num cluster real; o avaliador não consegue medir esse fluxo neste ambiente.

## Como começar

```bash
./start.sh
kubectl --context kind-gitquest -n desafio-25 apply -f k8s/
kubectl --context kind-gitquest -n desafio-25 get pods,sa,role,rolebinding,netpol
```

Antes de mudar qualquer coisa, faça as perguntas do pentest: *o que o token do frontend
consegue fazer? quem alcança o banco? por que a política `db-only-from-api` não protege
nada?* Depois descubra, no código, o que cada componente precisa de verdade.

## Arquivos que você pode modificar

- `k8s/` — todos os manifests da loja. Você pode editar, criar e remover arquivos, mas só
  com objetos destes tipos, todos no namespace `desafio-25`: `ServiceAccount`, `Role`,
  `RoleBinding`, `NetworkPolicy`, `Deployment`, `Service`, `ConfigMap`, `Secret`, `CronJob`.
  Recursos cluster-scoped (`ClusterRole`, `ClusterRoleBinding`, `Namespace`...) ou de outro
  namespace fazem o avaliador **recusar** os manifests antes de aplicar qualquer coisa.

Mantenha os nomes dos objetos da loja (`frontend`, `api`, `db`, `maintenance`,
`maintenance-report`, `frontend-config`, `db-credentials`), os labels
`app.kubernetes.io/name` e as portas dos Services. Não altere `app/`, `env/`, `test.sh`,
`start.sh` nem `challenge.yaml`.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador recusa manifests fora do escopo, **recria o namespace `desafio-25`**, aplica
`k8s/` e então: pede a página ao frontend, dispara o job a partir do CronJob
(`kubectl create job --from=cronjob/maintenance`), roda uma matriz de
`kubectl auth can-i --as=system:serviceaccount:desafio-25:<sa>` para cada ServiceAccount em
uso, procura tokens montados dentro dos pods e executa conexões TCP **de dentro** dos pods
(`/app connect`). Para isso ele cria três pods auxiliares: `gq-outsider` (um pod qualquer,
com saída liberada pelo avaliador), `gq-newcomer` (um pod novo, sem regras escritas para
ele) e `gq-job-twin` (mesmos labels dos pods do job). Leva cerca de 1–2 minutos.

## Critérios de conclusão

- [ ] Os manifests só contêm os tipos permitidos, todos no namespace `desafio-25`, e passam em
      `kubectl apply --dry-run=server --validate=strict`.
- [ ] A loja funciona: `frontend`, `api` e `db` ficam prontos em até 120 s, a página do
      frontend lista os pedidos do banco e o job grava o `maintenance-report` em até 90 s.
- [ ] Nenhum componente usa a ServiceAccount `default`, e a do job não é compartilhada.
- [ ] A ServiceAccount do job **pode** `list pods` e `get`/`update` em
      `configmaps/maintenance-report` — e **não pode** nada além disso: outros ConfigMaps,
      `list`/`create` de ConfigMaps, Secrets, criar/apagar pods, `pods/exec`, Deployments,
      `serviceaccounts/token`, `impersonate`, RoleBindings, `escalate`, `bind`, `*`, ou
      qualquer coisa em outros namespaces.
- [ ] A `default` e as ServiceAccounts do frontend, da api e do banco não têm permissão
      nenhuma (pods, ConfigMaps, Secrets, `pods/exec`, Deployments).
- [ ] Os pods do frontend, da api e do banco rodam **sem** token de ServiceAccount montado.
- [ ] Rede, testada de dentro dos pods: `frontend → api:8080` e `api → db:5432` funcionam por
      nome DNS; qualquer pod alcança o frontend; nenhum outro fluxo funciona (outro pod →
      api/db, frontend → db, job → db/frontend, api → frontend, db → frontend).
- [ ] Deny-by-default: um pod novo, sem regras escritas para ele, não recebe nem inicia conexões.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Kubernetes: Up and Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan Evenson — cap. 14 (Role-Based Access Control: "Identity in Kubernetes", "Roles and Role Bindings in Kubernetes", "Verbs for Kubernetes roles", "Testing Authorization with can-i") e cap. 19 (Securing Applications in Kubernetes: "Service Account Management", "Network Policy").
- Kubernetes docs — [Using RBAC Authorization](https://kubernetes.io/docs/reference/access-authn-authz/rbac/) (Referring to resources, `resourceNames`, Privilege escalation prevention: `escalate` e `bind`).
- Kubernetes docs — [Role Based Access Control Good Practices](https://kubernetes.io/docs/concepts/security/rbac-good-practices/) (least privilege, wildcards, `pods/exec`, Secrets, impersonate).
- Kubernetes docs — [Service Accounts](https://kubernetes.io/docs/concepts/security/service-accounts/) e [Configure Service Accounts for Pods](https://kubernetes.io/docs/tasks/configure-pod-container/configure-service-account/) (opt out of API credential automounting).
- Kubernetes docs — [Network Policies](https://kubernetes.io/docs/concepts/services-networking/network-policies/) (Default policies, "Behavior of to and from selectors", DNS).
- Kubernetes docs — [kubectl auth can-i](https://kubernetes.io/docs/reference/kubectl/generated/kubectl_auth/kubectl_auth_can-i/) e [User impersonation](https://kubernetes.io/docs/reference/access-authn-authz/authentication/#user-impersonation).

## Próximo desafio

Este é, por enquanto, o último desafio da trilha Kubernetes. Para liberar recursos ao
terminar: `kind delete cluster --name gitquest` (apaga o cluster de **todos** os desafios
da trilha).
