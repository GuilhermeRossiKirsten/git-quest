# Dicas — desafio-25

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelas perguntas do pentest, uma de cada vez. **Quem** é cada pod para a API do
Kubernetes (qual ServiceAccount aparece em `kubectl get pod -o yaml`)? O que essa identidade
pode fazer (`kubectl auth can-i --list --as=system:serviceaccount:desafio-25:<sa>`)? E na rede:
de dentro de cada pod, o que `/app connect <host:porta>` alcança? Por fim, compare os labels
reais dos pods (`kubectl get pods --show-labels`) com o `podSelector` da política
`db-only-from-api`.

## Nível 2

- `app/main.go` diz exatamente o que cada componente faz: só `runMaintenance` usa a API, e ela
  faz três chamadas HTTP. Traduza cada uma em verbo do RBAC (um `GET` numa coleção é `list`; um
  `GET` num objeto é `get`; um `PUT` é `update`).
- Todo pod sem `serviceAccountName` usa a `default`, e o token dela é montado mesmo que o código
  nunca o leia. Veja `automountServiceAccountToken` (na ServiceAccount e no pod).
- Um pod só fica isolado quando alguma NetworkPolicy o seleciona; políticas se **somam**. Um
  deny-by-default seleciona todos os pods (`podSelector: {}`) — e saída negada inclui a porta 53
  do `kube-dns`, no namespace `kube-system`.

## Nível 3

- Crie uma ServiceAccount por componente. Só a do job recebe uma `Role` com duas regras: `list`
  em `pods` e `get`/`update` em `configmaps` restrito por `resourceNames` ao
  `maintenance-report`. `create` e `list` não podem ser restringidos por nome — por isso o
  relatório já é criado pelos manifests. Troque o sujeito do `RoleBinding`; não use `*` nem papéis
  prontos como `edit`.
- Nos Deployments, `serviceAccountName` + `automountServiceAccountToken: false`. O CronJob usa a SA
  do job (que precisa do token).
- Rede: uma política que nega entrada e saída para todos os pods; uma que libera a saída DNS
  (UDP e TCP 53) para o `kube-dns`; e uma por componente com exatamente os fluxos da tabela do
  README. Atenção: nas regras, a porta é a do **contêiner** (a do pod), não a do Service.
- Verifique cada mudança com `can-i` e `/app connect` antes de rodar o `./test.sh`.
