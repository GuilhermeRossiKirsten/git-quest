# Dicas — desafio-10

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Não tente consertar tudo de uma vez: os problemas aparecem em cadeia, um escondendo o
seguinte. Qual é o `STATUS` dos pods agora, e o que a seção `Events` de
`kubectl describe pod` diz sobre ele? Quando o contêiner chega a rodar e morre, o que ele
escreveu antes de morrer (`kubectl logs --previous`)? E quando o `STATUS` não diz nada de
errado mas o pod não fica `READY`, quem decide que ele está pronto?

## Nível 2

Para cada sintoma, compare o que o cluster vê com o que o manifest pede:

- falha ao obter a imagem: quais tags existem de fato no nó?
  (`docker exec gitquest-control-plane crictl images`);
- o processo reclama de configuração: que variáveis de ambiente o contêiner recebe com
  `envFrom` e que nomes o programa espera (veja o log e `app/main.go`)?
- `OOMKilled` em `Last State`: quanta memória o processo usa por projeto e quanto o
  manifest permite?
- `Readiness probe failed` nos eventos: em que porta e caminho o serviço realmente responde?
- pods `Ready` mas Service sem endpoints: compare `spec.selector` do Service com
  `kubectl get pods --show-labels`.

## Nível 3

Uma abordagem, em ordem:

1. Ajuste a tag da imagem para a que `start.sh` construiu e carregou no kind.
2. No ConfigMap, a **chave** vira o nome da variável: renomeie-a para o nome que o
   programa exige (o valor já está certo).
3. Mantenha `CACHE_MB` em 64 e dê ao contêiner um limite de memória com folga para o
   cache mais o runtime do Go (algo como o dobro do cache); ajuste o request de acordo.
4. Aponte a readiness probe para o endpoint de prontidão na porta do contêiner (dá para
   usar o nome da porta) e adicione uma liveness probe no endpoint de saúde.
5. Faça o seletor do Service casar com os labels do template de pods.
6. Adicione `securityContext` (no pod e/ou no contêiner) com os três campos exigidos;
   a imagem já roda com um UID numérico não-root e não escreve no disco.

## Nível 4

Se tudo parece certo e algum critério ainda falha, rode o próprio avaliador do cluster:
`kubectl --context kind-gitquest -n desafio-10 exec deploy/inventory-api -- /app probe <url>`
com a mesma URL que o critério usa (`http://127.0.0.1:<porta><caminho>` para as probes,
`http://inventory-api.desafio-10.svc.cluster.local/items` para o Service) e leia a
resposta. Lembre que `test.sh` recria o namespace: tudo precisa estar nos arquivos de `k8s/`,
não em mudanças feitas à mão com `kubectl edit`.
