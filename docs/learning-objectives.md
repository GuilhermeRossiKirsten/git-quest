# Objetivos de aprendizagem — desafio-9

Ao concluir este desafio você será capaz de:

1. Separar build e runtime com *multi-stage build*, entregando uma imagem final mínima
   (distroless ou `scratch`) que contém só o necessário para executar o serviço.
2. Gerar um binário Go estático (`CGO_ENABLED=0`) que roda numa imagem sem libc.
3. Executar o container como usuário não-root, com sistema de arquivos somente leitura e
   sem capabilities do Linux.
4. Declarar um `HEALTHCHECK` em forma exec que funcione numa imagem sem shell nem curl.
5. Controlar o contexto de build com `.dockerignore`, impedindo que segredos, `.git` e
   código-fonte cheguem a qualquer camada da imagem.
6. Ordenar as instruções do Dockerfile para que o cache de camadas do BuildKit evite
   refazer o download de dependências quando só o código muda.

## Como os testes verificam cada objetivo

| Objetivo | Verificação |
|---|---|
| 1. Multi-stage e imagem mínima | Critério 2: `docker image inspect` → tamanho < 25 MB (a imagem inicial tem ~900 MB). |
| 2. Binário estático | Critérios 4, 6 e 7: o container precisa subir e responder; um binário dinâmico numa base `static` falha com `exec ... no such file or directory`. |
| 3. Não-root, read-only, sem capabilities | Critério 3: `Config.User` não vazio/0/root **e** `/whoami` devolve uid ≠ 0. Critério 7: container com `--read-only --cap-drop ALL --security-opt no-new-privileges` responde em `/healthz`. |
| 4. HEALTHCHECK sem shell | Critério 4: `Config.Healthcheck` definido e `State.Health.Status` chega a `healthy` em até 45 s. |
| 5. Contexto de build limpo | Critério 5: `docker save` é varrido camada a camada (incluindo arquivos apagados em camadas posteriores) e os metadados/histórico: nenhum token do `.env`, nenhum caminho `.git`, nenhum arquivo do código-fonte de `app/`. |
| 6. Cache de dependências | Critério 8: o contexto é copiado, um `.go` é alterado e a imagem é reconstruída com `--progress=plain`; a etapa `RUN ... go mod download` precisa estar `CACHED`. |
| Serviço continua correto | Critério 6: `/healthz` responde e `/api/greeting` devolve `{"message": GREETING}` lido do ambiente. |
