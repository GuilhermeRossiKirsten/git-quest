# Dicas — desafio-18

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Para cada probe do Deployment, responda: que pergunta ela faz ao contêiner, e o que o
Kubernetes faz quando a resposta é "não"? Uma delas reinicia o contêiner; outra só o tira
do Service; existe uma terceira que este Deployment nem declara. Agora olhe a tabela do
README: em qual situação faz sentido reiniciar? Em qual reiniciar só piora as coisas?

## Nível 2

Onde olhar:

- `kubectl describe pod`: os eventos `Unhealthy` dizem qual probe falhou e por quê
  (`connection refused`, timeout, código HTTP, ou um erro que nem chega a fazer a
  requisição); `Killing ... failed liveness probe` diz quem mandou reiniciar.
- `kubectl logs --previous`: em que ponto do carregamento o processo recebeu o sinal?
- `kubectl get endpointslices -w` enquanto você derruba o `pricing`: quanto tempo os pods
  levam para sair? Esse tempo é `periodSeconds × failureThreshold` (mais um pouco) — de
  que probe?
- O tempo até uma probe desistir é `initialDelaySeconds + periodSeconds × failureThreshold`.
  Faça a conta para o aquecimento de 25 s e para o pior caso de 90 s. Depois faça a conta
  de quanto tempo um pod travado levaria para ser reiniciado com essa mesma configuração.

## Nível 3

Uma abordagem:

1. Corrija o que impede a probe de sequer rodar (a porta referenciada existe no contêiner?).
2. Liveness pergunta "o processo está vivo?": aponte para o endpoint que **não** depende
   do `pricing`, com período curto, para detectar o travamento em bem menos de 35 s.
3. Readiness pergunta "posso receber tráfego?": aponte para o endpoint que considera a
   dependência, com `periodSeconds`/`failureThreshold` pequenos para sair do Service em
   poucos segundos.
4. Adicione uma `startupProbe` com orçamento (`periodSeconds × failureThreshold`) que
   cubra o pior caso de aquecimento com folga. Enquanto ela não passa, liveness e
   readiness não rodam — por isso a liveness não precisa de `initialDelaySeconds` grande
   (que atrasaria a detecção de travamentos logo após o início).
