# Objetivos de aprendizagem — desafio-18

Ao concluir este desafio você será capaz de:

1. Explicar o que cada probe decide: **startup** (o processo já terminou de iniciar?),
   **liveness** (este contêiner precisa ser reiniciado?) e **readiness** (este pod deve
   receber tráfego do Service agora?).
2. Proteger um serviço de inicialização lenta com `startupProbe`, dimensionando
   `periodSeconds × failureThreshold` pelo pior caso de aquecimento, sem atrasar a
   detecção de travamentos com um `initialDelaySeconds` grande na liveness.
3. Manter a liveness independente de dependências externas, evitando reinícios em
   cascata quando a dependência cai.
4. Ajustar `periodSeconds`, `timeoutSeconds` e `failureThreshold` da readiness para que
   pods sem condição de atender saiam do Service em poucos segundos — e voltem sozinhos.
5. Diagnosticar probes com `kubectl describe` (eventos `Unhealthy` e `Killing`),
   `restartCount`, `logs --previous` e EndpointSlices.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Papel de cada probe | O avaliador provoca três situações distintas no cluster (aquecimento, travamento, queda da dependência) e exige a reação certa em cada uma: nada / reiniciar só o pod travado / tirar do Service sem reiniciar. |
| 2. `startupProbe` | O rollout inicial (aquecimento de 25 s) precisa terminar sem nenhum reinício; um segundo rollout com `WARMUP_SECONDS=75` também; e um pod travado logo após ficar pronto precisa ser reiniciado em até 35 s — o que reprova liveness com `initialDelaySeconds` grande ou `failureThreshold` gigante no lugar da startup. |
| 3. Liveness sem dependência | Com o `pricing` fora do ar por 35 s, `restartCount` não pode mudar em nenhum pod, e o caminho/porta da liveness, chamado de dentro do pod, precisa responder 200. |
| 4. Readiness rápida | Com o `pricing` fora, a EndpointSlice do Service precisa ficar sem endpoints prontos em até 20 s; com ele de volta, todas as réplicas voltam ao Service. `GET /work` pelo DNS do Service responde 200 depois do rollout. |
| 5. Diagnóstico | Cada ✘ mostra as probes como o servidor as vê, os eventos do pod envolvido e o log do contêiner (inclusive `--previous`). |
