# Dicas — desafio-19

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Reproduza o incidente com `kubectl get deploy,rs,pods -w` aberto em outro terminal. No
instante do `set image`, quantos pods da versão antiga continuam vivos? Quando o pod novo
passa a receber tráfego — quando o contêiner sobe, ou quando o serviço dentro dele está
pronto? Leia os logs da 2.0.0 por um minuto inteiro (`kubectl logs -f`): o que ela avisa,
e quanto tempo depois de subir? E por que o `rollout undo` não encontra nada para onde voltar?

## Nível 2

Em `kubectl explain deployment.spec` (e `.strategy.rollingUpdate`) procure os campos que
respondem a cada pergunta:

- quantos pods a mais e quantos a menos o Deployment pode ter durante a troca?
- quando um pod novo conta como *disponível*, e por quanto tempo ele precisa continuar
  pronto para isso?
- depois de quanto tempo sem progresso o rollout é declarado falho?
- quantos ReplicaSets antigos ficam guardados?

O defeito da 2.0.0 aparece **depois** que ela fica pronta. Compare a janela em que ela
parece saudável com o tempo que a sua configuração exige antes de considerar um pod
disponível. Compare também quanto tempo o kubelet leva para notar que um pod deixou de
estar pronto (`periodSeconds × failureThreshold`) com o tempo que a 2.0.0 diz que ainda
vai atender antes de começar a responder 500.

## Nível 3

Uma abordagem:

1. `replicas: 2` (ou mais) e `RollingUpdate` com `maxUnavailable: 0` e `maxSurge: 1`: um
   pod antigo só sai quando um novo estiver disponível.
2. `readinessProbe` HTTP em `/readyz`, verificando com frequência (poucos segundos) e
   tirando o pod do Service depois de poucas falhas.
3. `minReadySeconds` maior que a janela em que a 2.0.0 parece saudável (algo como 15 s).
4. `progressDeadlineSeconds` curto o bastante para o avaliador ver a falha em menos de
   100 s, mas maior que o tempo de um rollout bom (60 s funciona com 2 réplicas).
5. `revisionHistoryLimit` maior que zero.
6. Um `PodDisruptionBudget` (`policy/v1`) com o mesmo seletor do Deployment que sempre
   mantenha ao menos um pod, mas ainda permita uma interrupção quando todos estão saudáveis.

## Nível 4

Se um critério de disponibilidade falhar, leia as linhas `FAIL` que o avaliador mostra: um
`503 ... catálogo ainda carregando` indica pod recebendo tráfego antes de estar pronto; um
`500 ... preços expirados` indica pod defeituoso que demorou a sair do Service; `connection
refused` ou timeout indica que não havia pod pronto algum. Lembre que `test.sh` recria o
namespace: tudo precisa estar em `k8s/`, não em `kubectl edit`. E um rollout bom precisa
caber em `progressDeadlineSeconds` também.
