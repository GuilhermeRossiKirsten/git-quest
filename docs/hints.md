# Dicas — desafio-9

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Rode `docker history --no-trunc gitquest/desafio-9-app:dev`. Quais camadas são
necessárias para *executar* o serviço e quais só serviram para *compilá-lo*? E o que
exatamente o `COPY . .` levou para dentro da imagem — o que há na pasta do desafio
além de `app/`? Pense também: precisa de `go` instalado para rodar um binário Go?

## Nível 2

- Leia sobre *multi-stage builds*: um estágio com o toolchain compila, outro estágio
  mínimo recebe só o binário (`COPY --from=...`). Com uma base sem libc (distroless
  `static` ou `scratch`), o binário precisa ser estático — procure o efeito de `CGO_ENABLED`.
- O `.dockerignore` define o contexto de build. Apagar um arquivo num `RUN` posterior não
  o remove das camadas anteriores.
- Imagens sem shell não executam `HEALTHCHECK CMD curl ...` (forma shell). O binário do
  serviço tem um subcomando próprio para isso.
- O cache do BuildKit é invalidado a partir da primeira instrução cujas entradas mudaram.
  Em que ordem estão `COPY` e `go mod download`?

## Nível 3

Abordagem, passo a passo:

1. Estágio `build` a partir de `golang:1.25.3-bookworm`: copie primeiro **só** `app/go.mod`
   (e `go.sum`, se existir), rode `go mod download`, depois copie os `*.go` e compile com
   `CGO_ENABLED=0` (opcionalmente `-trimpath -ldflags="-s -w"`).
2. Estágio final a partir de `gcr.io/distroless/static-debian12:nonroot` (ou `scratch`
   com um `USER` numérico): copie só o binário do estágio anterior.
3. Declare `USER` não-root, `ENTRYPOINT` em forma exec e um `HEALTHCHECK` em forma exec
   chamando o subcomando `healthcheck` do binário, com `--interval` curto.
4. Troque o `.dockerignore` por uma lista de permissão: ignore tudo (`*`) e libere com
   `!` apenas o que o Dockerfile copia.
5. Confira com `docker build --progress=plain .` duas vezes, alterando um `.go` numa
   cópia da pasta entre elas: a etapa `go mod download` deve aparecer como `CACHED`.
