# desafio-19 · Rollout e rollback

> Trilha: kubernetes · Nível: advanced · ~75 min · 250 pontos

## Contexto

Terça-feira, 10h02. O time de catálogo publicou a `catalog-api` 2.0.0, a versão que traz
preços para o catálogo. Às 10h03 a loja inteira mostrava "catálogo indisponível". O
plantonista tentou voltar para a versão anterior e recebeu
`error: no rollout history found for deployment "catalog-api"`. Foram sete minutos de
incidente até alguém reescrever a tag da imagem à mão.

O post-mortem chegou a uma conclusão incômoda: o código da 2.0.0 tinha um defeito, sim,
mas **quem derrubou o catálogo foi o Deployment**. Na próxima vez que uma versão ruim
sair — e vai sair —, ela precisa ser barrada sem que nenhum cliente perceba.

## Problema

Os manifests em `k8s/` funcionam no dia a dia: a 1.0.0 sobe e responde. Mas, a cada release:

- durante qualquer troca de versão, mesmo de uma versão boa, requisições ao Service
  `catalog-api` falham por alguns segundos (`503`, conexões recusadas);
- quando a versão nova tem defeito, o Deployment a promove para 100% das réplicas e diz
  que o rollout terminou com sucesso; o problema só aparece para os clientes;
- `kubectl rollout undo` responde que não há histórico;
- o time de infraestrutura avisou que, na próxima manutenção dos nós (`kubectl drain`),
  nada impede que todas as réplicas sejam removidas de uma vez.

O código do serviço (`app/`) **não deve ser alterado**: a 2.0.0 continuará defeituosa, e
é justamente ela que o avaliador vai tentar implantar. O que você precisa mudar é **como**
o Kubernetes troca uma versão pela outra. Três imagens já estão carregadas no cluster:

| Imagem | O que é |
|---|---|
| `gitquest/catalog-api:1.0.0` | versão em produção (é a que fica em `k8s/`) |
| `gitquest/catalog-api:1.0.1` | correção pequena, sem mudança de comportamento |
| `gitquest/catalog-api:2.0.0` | release com preços — tem um defeito |

Todas respondem `GET /version`, `/healthz`, `/readyz` e `/items` na porta 8080.

Ferramentas para investigar (sempre com `--context kind-gitquest -n desafio-19`):
`kubectl rollout status`, `kubectl rollout history`, `kubectl rollout undo`,
`kubectl get deploy,rs,pods -w`, `kubectl describe deploy` (seção `Conditions`),
`kubectl logs` dos pods da versão nova, `kubectl get endpointslices -w`, `kubectl get pdb`
e `kubectl explain deployment.spec`. A imagem não tem shell nem `curl`; o binário faz
requisições sozinho, inclusive em rajada contínua:

```bash
kubectl --context kind-gitquest -n desafio-19 run carga --rm -it --restart=Never \
  --image=gitquest/catalog-api:1.0.0 --image-pull-policy=IfNotPresent \
  -- loadcheck http://catalog-api.desafio-19.svc.cluster.local/items
```

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Configurar `RollingUpdate` com `maxSurge` e `maxUnavailable` sem perder capacidade.
- Usar `readinessProbe` e `minReadySeconds` para que só pods estáveis contem como disponíveis.
- Detectar um rollout travado com `progressDeadlineSeconds`.
- Manter histórico de revisões e reverter com `kubectl rollout undo`.
- Proteger o serviço com réplicas e um `PodDisruptionBudget` coerente.

## Conhecimentos necessários

- Deployments, Services, labels e probes no kind (desafio-10).
- Uso do `kubectl` (`apply`, `get`, `describe`, `logs`, `set image`).

## Ambiente

- Docker >= 20.10 (Docker Desktop com WSL2 no Windows/macOS), com ao menos 4 GB de memória.
- Bash (Linux, macOS, WSL2 ou Git Bash).
- `kind` v0.27.0 e `kubectl` 1.31–1.33: se não estiverem no `PATH`, o `./start.sh` baixa
  `kind v0.27.0` e `kubectl v1.32.2` para `./.tools/bin`, conferindo o sha256. Para usá-los
  no terminal: `export PATH="$PWD/.tools/bin:$PATH"`.
- Cluster kind `gitquest` (um nó, Kubernetes v1.32.2, imagem do nó fixada por digest),
  contexto `kind-gitquest` — o mesmo dos outros desafios da trilha. Os scripts **nunca**
  usam o seu contexto atual e só mexem no namespace `desafio-19`.
- Imagens `gitquest/catalog-api:{1.0.0,1.0.1,2.0.0}`, construídas a partir de `app/` e
  carregadas no cluster com `kind load docker-image` (não há registry).

## Como começar

```bash
./start.sh
kubectl --context kind-gitquest -n desafio-19 apply -f k8s/
kubectl --context kind-gitquest -n desafio-19 get deploy,rs,pods -w      # em outro terminal
kubectl --context kind-gitquest -n desafio-19 set image deploy/catalog-api api=gitquest/catalog-api:2.0.0
```

Reproduza o incidente, observe o que acontece com ReplicaSets e pods, leia os logs da
2.0.0 e só então mude os manifests. Repita o release com a versão boa (`1.0.1`) também.

## Arquivos que você pode modificar

- `k8s/` — `deployment.yaml` e `service.yaml`; você pode adicionar manifests de
  `PodDisruptionBudget`. Não declare outro namespace nem outros tipos de objeto.

Não altere `app/`, `env/`, `test.sh`, `start.sh` nem `challenge.yaml`. Mantenha a imagem
`gitquest/catalog-api:1.0.0` em `k8s/` (o avaliador troca as versões sozinho) e os nomes
`catalog-api` do Deployment, do contêiner `api` e do Service.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador **recria o namespace `desafio-19`**, aplica `k8s/` e sobe um *prober* — um
pod que faz 10 requisições por segundo a `http://catalog-api.desafio-19.svc.cluster.local/items`.
Com ele no ar, encena uma sequência de releases: `1.0.1` (boa), `rollout undo`, `2.0.0`
(defeituosa) e `rollout undo` de novo; por fim testa o PodDisruptionBudget com evictions
reais. Cada etapa mostra ✔/✘, as falhas vistas pelo prober, o `rollout history`, os
ReplicaSets e os eventos. Leva cerca de 4–5 minutos.

## Critérios de conclusão

- [ ] Os manifests passam em `kubectl apply --dry-run=server --validate=strict`.
- [ ] O Deployment declara pelo menos 2 réplicas e sobe a 1.0.0 em até 120 s.
- [ ] O rollout 1.0.0 → 1.0.1 termina em até 150 s, com todos os pods servindo 1.0.1.
- [ ] `kubectl rollout undo` depois desse rollout volta todos os pods para a 1.0.0.
- [ ] O rollout da 2.0.0 **não** é concluído e o Deployment o declara falho
      (`Progressing=False`, `ProgressDeadlineExceeded`) em até 100 s.
- [ ] `kubectl rollout undo` depois do release defeituoso deixa todos os pods na versão anterior.
- [ ] Em **todas** as etapas acima: 0 requisições com falha no prober e o número de
      réplicas disponíveis nunca abaixo de `spec.replicas`.
- [ ] Um PodDisruptionBudget cobre os pods do Deployment: aceita uma eviction e recusa a
      seguinte enquanto a réplica substituta não está pronta.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Kubernetes: Up and Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan Evenson — cap. 10 (Deployments: "Deployment Strategies", "Rolling Update Strategy", "Slowing Rollouts to Ensure Service Health", "Rollout History", "Monitoring a Rollout") e cap. 5 (Pods: readiness probe).
- Kubernetes docs — [Deployments](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/) (Rolling Back a Deployment, Failed Deployment, Max Unavailable / Max Surge, Min Ready Seconds, Revision History Limit).
- Kubernetes docs — [Disruptions](https://kubernetes.io/docs/concepts/workloads/pods/disruptions/) e [Specifying a Disruption Budget for your Application](https://kubernetes.io/docs/tasks/run-application/configure-pdb/).
- Kubernetes docs — [API-initiated Eviction](https://kubernetes.io/docs/concepts/scheduling-eviction/api-eviction/).
- Kubernetes docs — [Configure Liveness, Readiness and Startup Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/).

## Próximo desafio

[desafio-20 — O consumidor que processava duas vezes](../../tree/desafio-20): da entrega
contínua de código à entrega confiável de mensagens. Para liberar recursos ao terminar a
trilha: `kind delete cluster --name gitquest`.
