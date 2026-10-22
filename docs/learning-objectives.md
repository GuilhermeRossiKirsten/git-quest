# Objetivos de aprendizagem — desafio-25

Ao concluir este desafio você será capaz de:

1. Dar a cada workload uma ServiceAccount própria e manter a `default` sem permissão nenhuma.
2. Escrever `Role` e `RoleBinding` de menor privilégio: grupos de API, recursos e verbos exatos,
   `resourceNames` onde couber, sem curingas e sem papéis prontos amplos.
3. Auditar permissões com `kubectl auth can-i --as=system:serviceaccount:<ns>:<sa>`, inclusive as
   perigosas: Secrets, `pods/exec`, `serviceaccounts/token`, `impersonate`, `escalate` e `bind`.
4. Desligar `automountServiceAccountToken` nos pods que não falam com a API do Kubernetes.
5. Isolar um namespace com NetworkPolicy deny-by-default de entrada e saída, liberando apenas os
   fluxos necessários e o DNS do cluster.
6. Diagnosticar conectividade de dentro dos pods, distinguindo falha de DNS, pacote descartado e
   porta fechada.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Identidades | lê `serviceAccountName` dos Deployments e do CronJob aplicados: nenhum usa `default` e a do job não é compartilhada; roda a matriz "sem permissão" para a `default` e para as SAs do frontend, da api e do banco |
| 2. Menor privilégio | `kubectl auth can-i` como a SA do job: precisa responder `yes` para `list pods` e `get`/`update configmaps/maintenance-report` e `no` para 19 ações (outros ConfigMaps, `list`/`create` de ConfigMaps, Secrets, pods, Deployments, RBAC, `*`, outros namespaces) |
| 2. (comportamento) | dispara o job com `kubectl create job --from=cronjob/maintenance` e confere que o `maintenance-report` foi escrito pelo pod dele |
| 3. Auditoria | a própria matriz usa impersonação (`--as` + grupos `system:serviceaccounts`), como o participante deve fazer |
| 4. Tokens | `kubectl exec ... /app tokencheck` nos pods em execução do frontend, da api e do banco: o token não pode estar montado; o job, que usa a API, precisa funcionar |
| 5. Deny-by-default | conexões TCP executadas de dentro dos pods: 3 fluxos permitidos (por nome DNS), 7 proibidos (por IP do Service, para não depender do DNS) e 2 com um pod novo sem regras (entrada e saída negadas); um pod do avaliador com saída liberada isola as regras de entrada |
| 5. (DNS) | os fluxos permitidos usam nomes (`api:8080`, `db:5432`); se o DNS estiver bloqueado, o teste diz isso explicitamente |
| 6. Diagnóstico | as mensagens do avaliador e o `/app connect` distinguem "não resolveu", "sem resposta" e "recusada" |
| Escopo | antes de aplicar, `kubectl apply --dry-run=client -n desafio-25` lista os objetos: qualquer tipo fora da lista (inclusive cluster-scoped) ou outro namespace faz o avaliador parar sem tocar no cluster |
