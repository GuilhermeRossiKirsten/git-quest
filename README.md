# desafio-18 · Probes que mentem

> Trilha: kubernetes · Nível: intermediate · ~60 min · 200 pontos

## Contexto

O `warmup-api`, o serviço de recomendações da loja, carrega um modelo inteiro na memória
antes de atender a primeira requisição. Desde que ele foi para o cluster, o plantão não
dorme: na terça os pods entraram num ciclo de reinícios que só parou quando alguém
escalou o Deployment para zero; na quarta o `pricing` (a dependência de preços) ficou
fora do ar por dois minutos e, quando voltou, o `warmup-api` continuou indisponível por
mais um bom tempo; e na quinta um pod travou e passou a madrugada inteira "Running",
recebendo tráfego e respondendo nada. O dashboard dizia que estava tudo verde.

As probes do Deployment existem — elas só não dizem a verdade.

## Problema

Com os manifests de `k8s/`, aplicados no namespace `desafio-18`:

- logo depois do `apply` a coluna `RESTARTS` começa a subir e os pods nunca chegam a
  `1/1 READY`; o log mostra o carregamento do modelo sendo interrompido no meio;
- quando o `pricing` cai, os pods continuam recebendo tráfego que vai falhar — e, se você
  "consertar" de qualquer jeito, eles passam a ser reiniciados em massa, voltando a
  aquecer do zero justamente quando a dependência volta;
- quando o loop principal de um pod trava, nada acontece: o pod segue `Running`;
- em nós frios, o carregamento do modelo chega a levar bem mais que os 25 s habituais.

Comportamento do serviço (fixo — detalhes em `app/main.go`, que **não** deve ser alterado):

| Situação | `/healthz` | `/readyz` | porta 8080 |
|---|---|---|---|
| carregando o modelo (`WARMUP_SECONDS`, padrão 25 s; até **90 s** em nó frio) | — | — | **fechada** |
| normal | 200 | 200 | aberta |
| `pricing` fora do ar | 200 | 503 | aberta |
| loop principal travado (`GET /admin/hang`) | sem resposta | sem resposta | aberta |

Requisitos do time: ao menos **2 réplicas**, a imagem `gitquest/warmup-api:1.0.0` sem
`command`/`args`, o aquecimento real mantido (não reduza `WARMUP_SECONDS`), e um pod
travado precisa ser **reiniciado em até 35 s** depois de travar.

Ferramentas para investigar (sempre com `--context kind-gitquest -n desafio-18`):
`kubectl get pods -w`, `kubectl describe pod` (seção `Events`: `Unhealthy`, `Killing`),
`kubectl logs` e `--previous`, `kubectl get events --sort-by=.lastTimestamp`,
`kubectl get endpointslices -w` e `kubectl explain pod.spec.containers.startupProbe`.
A imagem não tem shell nem `curl`; use o próprio binário:

```bash
kubectl --context kind-gitquest -n desafio-18 exec deploy/warmup-api -- /app probe http://127.0.0.1:8080/readyz
kubectl --context kind-gitquest -n desafio-18 exec deploy/pricing -- /app probe 'http://127.0.0.1:8080/admin/state?state=down'   # e state=up
kubectl --context kind-gitquest -n desafio-18 exec <pod> -- /app probe http://127.0.0.1:8080/admin/hang
```

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Distinguir o que cada probe decide: startup, liveness e readiness.
- Proteger um serviço de inicialização lenta com `startupProbe`, dimensionada pelo pior caso.
- Manter a liveness independente de dependências externas.
- Ajustar a readiness para tirar pods do Service rápido, sem reiniciá-los.
- Diagnosticar probes por eventos, `restartCount` e EndpointSlices.

## Conhecimentos necessários

- Deployment, Service, labels e probes HTTP básicas (desafio-10).
- Uso do `kubectl` (`apply`, `get`, `describe`, `logs`, `exec`).

## Ambiente

- Docker >= 20.10 (Docker Desktop com WSL2 no Windows/macOS), com ao menos 4 GB de memória.
- Bash (Linux, macOS, WSL2 ou Git Bash).
- `kind` v0.27.0 e `kubectl` 1.31–1.33: se faltarem, o `./start.sh` baixa versões fixadas
  para `./.tools/bin`, conferindo o sha256 (`export PATH="$PWD/.tools/bin:$PATH"`).
- Cluster kind `gitquest` (Kubernetes v1.32.2, o mesmo do desafio-10), contexto
  `kind-gitquest`. Os scripts sempre passam `--context kind-gitquest` e só mexem no
  namespace `desafio-18`.
- Imagem `gitquest/warmup-api:1.0.0`, construída de `app/` e carregada com `kind load`.
  A mesma imagem roda a dependência `pricing` (`env/pricing.yaml`), que o `start.sh` e o
  `test.sh` aplicam no namespace.

## Como começar

```bash
./start.sh                       # cluster, imagem, namespace e a dependência pricing
kubectl --context kind-gitquest -n desafio-18 apply -f k8s/
kubectl --context kind-gitquest -n desafio-18 get pods -w
```

Reproduza cada incidente do Contexto à mão (derrube o `pricing`, trave um pod) e observe
pods, eventos e EndpointSlices antes de mudar qualquer número.

## Arquivos que você pode modificar

- `k8s/` — `deployment.yaml` e `service.yaml` (ConfigMaps adicionais são aceitos).

Não altere `app/`, `env/`, `test.sh`, `start.sh` nem `challenge.yaml`, e não renomeie o
Deployment nem o Service (`warmup-api`).

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador **recria o namespace `desafio-18`**, aplica `env/pricing.yaml` e `k8s/`, e
provoca, em ordem: o aquecimento normal; um travamento (`/admin/hang`) num pod; a queda
do `pricing` por 35 s seguida da volta; e um novo rollout com `WARMUP_SECONDS=75`
(nó frio). Leva de 3 a 4 minutos. Cada critério aparece com ✔/✘ e, nas falhas, as probes
declaradas, os eventos e o log do pod.

## Critérios de conclusão

- [ ] Os manifests passam em `kubectl apply --dry-run=server --validate=strict`.
- [ ] Imagem `gitquest/warmup-api:1.0.0`, sem `command`/`args`, ao menos 2 réplicas.
- [ ] O rollout termina (todas as réplicas Ready) em até 90 s **sem nenhum reinício**.
- [ ] `GET http://warmup-api.desafio-18.svc.cluster.local/work` responde 200, com o aquecimento de 25 s mantido.
- [ ] Um pod travado é reiniciado em até 35 s, e os outros não reiniciam.
- [ ] Com o `pricing` fora do ar, nenhum pod fica pronto no Service em até 20 s.
- [ ] Com o `pricing` fora do ar por 35 s, nenhum pod reinicia e o endpoint da liveness continua respondendo 200.
- [ ] Com o `pricing` de volta, todas as réplicas voltam ao Service.
- [ ] Um rollout com aquecimento de 75 s termina sem nenhum reinício.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Kubernetes: Up and Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan Evenson — cap. 5 (Pods: "Health Checks" — liveness, readiness e startup probes) e cap. 7 (Service Discovery: readiness checks e endpoints).
- Kubernetes docs — [Configure Liveness, Readiness and Startup Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/).
- Kubernetes docs — [Pod Lifecycle: Container probes](https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/#container-probes).
- Henning Jacobs — ["Kubernetes Liveness Probes are Dangerous"](https://srcco.de/posts/kubernetes-liveness-probes-are-dangerous.html).

## Próximo desafio

**desafio-19 — Rollout e rollback**: com probes que dizem a verdade, o Deployment passa a
saber quando uma nova versão está realmente pronta. Próximo passo: estratégias de rollout
e como voltar atrás quando a nova versão não fica pronta.
