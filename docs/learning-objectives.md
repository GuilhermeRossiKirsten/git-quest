# Objetivos de aprendizagem — desafio-19

Ao concluir este desafio você será capaz de:

1. Configurar a estratégia `RollingUpdate` com `maxSurge` e `maxUnavailable` para que a
   troca de versão nunca reduza a capacidade abaixo de `spec.replicas`.
2. Usar `readinessProbe` e `minReadySeconds` para que só pods realmente prontos recebam
   tráfego, e só pods estáveis por um tempo mínimo contem como disponíveis para o rollout.
3. Detectar um rollout travado com `progressDeadlineSeconds` e ler a condição
   `Progressing=False` / `ProgressDeadlineExceeded` (`kubectl describe`, `rollout status`).
4. Manter histórico de revisões (`revisionHistoryLimit`) e reverter com
   `kubectl rollout undo`, conferindo o resultado com `kubectl rollout history`.
5. Proteger o serviço de interrupções voluntárias (drain, evictions) com réplicas e um
   `PodDisruptionBudget` que protege sem travar a manutenção.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. `maxSurge`/`maxUnavailable` | Durante cada etapa (rollout 1.0.1, os dois `rollout undo` e o rollout 2.0.0), o avaliador lê `status.availableReplicas` a cada segundo e exige que nunca fique abaixo de `spec.replicas`; um prober no cluster faz 10 GET/s ao Service e precisa terminar cada etapa com 0 falhas. |
| 2. Readiness e `minReadySeconds` | Toda versão responde 503 nos primeiros segundos: sem readiness o prober registra falhas. A 2.0.0 fica pronta por alguns segundos e depois deixa de ficar: sem `minReadySeconds` ela vira "disponível", substitui os pods bons e o rollout é concluído (✘); com readiness lenta ela continua no Service quando começa a responder 500 (✘ no prober). |
| 3. `progressDeadlineSeconds` | Depois do `set image` para a 2.0.0, a condição `Progressing` precisa chegar a `ProgressDeadlineExceeded` em até 100 s; o rollout 1.0.1 precisa terminar sem ser declarado falho. |
| 4. Histórico e `rollout undo` | O avaliador executa `kubectl rollout undo` depois do rollout 1.0.1 concluído e depois do 2.0.0 travado; os dois precisam funcionar e terminar com todos os pods respondendo `GET /version` com a versão anterior (consultado dentro de cada pod). |
| 5. Réplicas e PDB | `spec.replicas >= 2`; um PDB precisa cobrir exatamente os pods do Deployment (`expectedPods`), permitir ao menos uma interrupção e exigir ao menos um pod saudável; evictions reais pela API: a primeira é aceita, a seguinte é recusada. |
