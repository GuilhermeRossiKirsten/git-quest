# desafio-10 · O Deployment que nunca ficou pronto

> Trilha: kubernetes · Nível: advanced · ~90 min · 300 pontos

## Contexto

Sexta-feira, 17h40. O `inventory-api` — o serviço de estoque que você empacotou numa
imagem enxuta no desafio anterior — finalmente vai para o Kubernetes. Quem escreveu os
manifests saiu de férias logo depois do primeiro `kubectl apply`, e deixou só um bilhete:
"tá quase, é só subir". Desde então o Deployment está lá, criado, mas **nunca ficou
pronto**. O time de produto quer o catálogo respondendo dentro do cluster ainda hoje.

## Problema

Os manifests em `k8s/` são aceitos pelo cluster sem nenhum erro, mas o serviço não sobe:

- `kubectl get pods` nunca mostra `2/2` réplicas `READY`; a coluna `STATUS` muda de
  assunto conforme você mexe nos arquivos — cada correção revela o próximo problema;
- o Service `inventory-api` existe, mas uma requisição a
  `http://inventory-api.desafio-10.svc.cluster.local/items`, feita de dentro do cluster,
  não chega a lugar nenhum;
- o time de segurança avisou que contêineres sem endurecimento básico não entram em
  produção.

O código do serviço (`app/`) está correto e **não deve ser alterado**: tudo o que você
precisa corrigir está nos manifests. Requisitos do time que valem como regra:

- o serviço precisa de **2 réplicas**;
- o cache em memória precisa de **64 MiB** (`CACHE_MB`) — diminuir o cache não é solução;
- o contêiner precisa declarar requests e limits de recursos e ter readiness e liveness
  probes **HTTP**.

Ferramentas para investigar (sempre com `--context kind-gitquest -n desafio-10`):
`kubectl get pods -o wide`, `kubectl describe pod`, `kubectl logs` (e `--previous`),
`kubectl get events --sort-by=.lastTimestamp`, `kubectl get endpointslices`,
`kubectl get pods --show-labels` e `kubectl explain`. A imagem não tem shell nem `curl`;
para fazer requisições de dentro de um pod use o próprio binário:

```bash
kubectl --context kind-gitquest -n desafio-10 exec deploy/inventory-api -- /app probe http://127.0.0.1:8080/healthz
```

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Investigar pods com `get`, `describe`, `logs --previous` e eventos até a causa de
  `ImagePullBackOff`, `CrashLoopBackOff` e `OOMKilled`.
- Injetar configuração com ConfigMap e `envFrom`.
- Dimensionar requests e limits de memória a partir do consumo real.
- Configurar readiness e liveness probes HTTP.
- Ligar Service e pods por labels e selectors; usar o DNS do cluster.
- Endurecer o contêiner com `securityContext`.

## Conhecimentos necessários

- Containers e imagens Docker (desafio-9).
- Noções de YAML e dos objetos Pod, Deployment, Service e ConfigMap.
- Uso básico do `kubectl` (`apply`, `get`, `describe`, `logs`).

## Ambiente

- Docker >= 20.10 (Docker Desktop com WSL2 no Windows/macOS), com ao menos 4 GB de memória.
- Bash (Linux, macOS, WSL2 ou Git Bash).
- `kind` v0.27.0 e `kubectl` 1.31–1.33: se não estiverem no `PATH` (ou estiverem em outra
  versão), o `./start.sh` baixa `kind v0.27.0` e `kubectl v1.32.2` para `./.tools/bin`,
  conferindo o sha256 publicado ao lado de cada binário. Para usá-los no seu terminal:
  `export PATH="$PWD/.tools/bin:$PATH"`.
- Cluster kind `gitquest` (um nó, Kubernetes v1.32.2, imagem do nó fixada por digest),
  contexto `kind-gitquest`. Os scripts **nunca** usam o seu contexto atual: tudo passa
  `--context kind-gitquest`, e o seu `current-context` é restaurado depois da criação.
- Imagem do serviço: `gitquest/inventory-api:1.0.0`, construída a partir de `app/` e
  carregada no cluster com `kind load docker-image` (não há registry).

## Como começar

```bash
./start.sh                       # cria (ou reutiliza) o cluster e carrega a imagem
kubectl --context kind-gitquest -n desafio-10 apply -f k8s/
kubectl --context kind-gitquest -n desafio-10 get pods -w
```

Corrija um problema, aplique de novo, observe. `./start.sh --reset` recria **somente** o
cluster `gitquest`. Quando terminar a trilha: `kind delete cluster --name gitquest`.

## Arquivos que você pode modificar

- `k8s/` — `configmap.yaml`, `deployment.yaml` e `service.yaml` (você pode adicionar
  outros manifests de ConfigMap/Secret/Deployment/Service; não declare outro namespace).

Não altere `app/`, `env/`, `test.sh`, `start.sh` nem `challenge.yaml`: o avaliador usa as
versões originais. Não renomeie o Deployment nem o Service (`inventory-api`).

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador **apaga e recria o namespace `desafio-10`** a cada execução, aplica `k8s/`,
espera o rollout (até 120 s, falhando cedo em estados sem volta) e mostra cada critério com
✔/✘, os eventos recentes e o log do contêiner quando algo falha. Os objetos ficam no
namespace depois do teste para você investigar.

## Critérios de conclusão

- [ ] Os manifests passam em `kubectl apply --dry-run=server --validate=strict`.
- [ ] O Deployment usa a imagem `gitquest/inventory-api:1.0.0` com pelo menos 2 réplicas.
- [ ] Todas as réplicas ficam `Ready` em até 120 s.
- [ ] Nenhum reinício (`restartCount == 0`) 15 s depois do rollout.
- [ ] `requests.cpu`, `requests.memory` e `limits.memory` declarados.
- [ ] readiness e liveness probes HTTP configuradas, e cada uma responde 200 dentro do pod.
- [ ] O Service `inventory-api` tem um endpoint pronto por réplica.
- [ ] `GET http://inventory-api.desafio-10.svc.cluster.local/items`, de dentro do cluster, responde 200 com o catálogo.
- [ ] O serviço reporta cache de pelo menos 64 MiB.
- [ ] `securityContext` com `runAsNonRoot: true`, `allowPrivilegeEscalation: false` e `readOnlyRootFilesystem: true`.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Kubernetes: Up and Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan Evenson — cap. 5 (Pods: health checks e resource management), cap. 6 (Labels and Annotations), cap. 7 (Service Discovery), cap. 10 (Deployments), cap. 13 (ConfigMaps and Secrets) e cap. 19 (Securing Applications in Kubernetes).
- Kubernetes docs — [Debug Pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/) e [Debug Services](https://kubernetes.io/docs/tasks/debug/debug-application/debug-service/).
- Kubernetes docs — [Configure Liveness, Readiness and Startup Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/).
- Kubernetes docs — [Resource Management for Pods and Containers](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/).
- Kubernetes docs — [Configure a Pod to Use a ConfigMap](https://kubernetes.io/docs/tasks/configure-pod-container/configure-pod-configmap/) e [Configure a Security Context](https://kubernetes.io/docs/tasks/configure-pod-container/security-context/).
- kind — [Quick Start](https://kind.sigs.k8s.io/docs/user/quick-start/) (inclusive "Loading an Image Into Your Cluster").

## Próximo desafio

Este é o último desafio disponível da trilha por enquanto. Na sequência planejada da
trilha Kubernetes vêm probes para serviços de inicialização lenta (liveness × readiness ×
startup) e estratégias de rollout e rollback. Para liberar recursos:
`kind delete cluster --name gitquest`.
