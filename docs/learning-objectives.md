# Objetivos de aprendizagem — desafio-10

Ao concluir este desafio você será capaz de:

1. Investigar pods com `kubectl get`, `describe`, `logs --previous` e eventos, chegando à
   causa de `ImagePullBackOff`, `CrashLoopBackOff` e `OOMKilled` sem adivinhar.
2. Injetar configuração com ConfigMap e `envFrom`, sabendo que a chave do ConfigMap vira o
   nome da variável de ambiente.
3. Dimensionar `requests` e `limits` de memória a partir do consumo real do processo, e
   reconhecer um `OOMKilled` causado por limite baixo demais.
4. Configurar readiness e liveness probes HTTP na porta e no caminho certos e explicar a
   diferença entre elas (tirar do balanceamento × reiniciar).
5. Ligar um Service aos pods por labels e selectors e verificar endpoints e DNS do cluster.
6. Endurecer o contêiner com `securityContext` (`runAsNonRoot`,
   `allowPrivilegeEscalation: false`, `readOnlyRootFilesystem: true`).

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Diagnóstico de pods | O rollout precisa terminar com todas as réplicas `Ready` em até 120 s; o teste falha cedo ao ver `ErrImagePull`/`ImagePullBackOff`, `CrashLoopBackOff` ou `OOMKilled`, mostrando motivo, log do contêiner e eventos. Exige também a imagem `gitquest/inventory-api:1.0.0`. |
| 2. ConfigMap e `envFrom` | Sem `DATABASE_URL` válida o processo sai com código 1 (CrashLoopBackOff) e o rollout não termina. |
| 3. Requests e limits | `requests.cpu`, `requests.memory` e `limits.memory` precisam existir; `restartCount == 0` e nenhum `OOMKilled` 15 s depois do rollout; `/healthz` precisa reportar cache >= 64 MiB (reduzir o cache não vale). |
| 4. Probes | readiness e liveness precisam ser `httpGet`; o teste executa `/app probe` dentro do pod na porta e caminho declarados de cada uma e exige HTTP 200. Uma readiness errada impede o rollout (pods Running mas nunca Ready). |
| 5. Service, labels e DNS | O Service precisa ter um endpoint pronto (EndpointSlice) por réplica, e `GET http://inventory-api.desafio-10.svc.cluster.local/items`, feito de dentro de um pod, precisa responder 200 com o catálogo. |
| 6. `securityContext` | Lê o Deployment aplicado no cluster: `runAsNonRoot: true` (pod ou contêiner), `allowPrivilegeEscalation: false` e `readOnlyRootFilesystem: true`; os pods precisam continuar Ready com essas restrições. |
