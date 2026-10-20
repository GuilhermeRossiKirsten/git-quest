# Toolchain do desafio: apenas ferramentas, nenhum código do desafio é copiado.
# O código é montado em /work por gq_docker_run (sem rede, usuário não-root).
FROM golang:1.25.3-bookworm

# - GOTOOLCHAIN=local: nunca tentar baixar outra versão do Go (o container não tem rede).
# - GOFLAGS=-buildvcs=false: /work/.git de um worktree aponta para fora do container.
# - CGO_ENABLED=1: o race detector (-race) precisa de cgo.
# - GOCACHE em /gocache: um volume nomeado montado ali guarda o cache de build entre
#   execuções; o diretório nasce gravável por qualquer usuário (o volume herda o modo).
ENV GOTOOLCHAIN=local \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=1 \
    GOCACHE=/gocache/build \
    GOMODCACHE=/gocache/mod
RUN mkdir -p /gocache/build /gocache/mod && chmod -R 1777 /gocache
