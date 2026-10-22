# Toolchain do desafio: Go fixado, só biblioteca padrão (o cliente Redis é o
# pacote internal/redisx). Nenhum código do desafio é copiado: ele é montado em
# /work por gq_docker_run, que roda como usuário não-root e ligado só à rede
# interna do compose (sem internet).
FROM golang:1.25.3-bookworm

# - GOTOOLCHAIN=local e GOPROXY=off: nunca buscar nada na rede.
# - GOFLAGS=-buildvcs=false: /work/.git de um worktree aponta para fora do container.
# - CGO_ENABLED=1: exigido pelo race detector (go test -race).
# - GOCACHE/GOMODCACHE em /gocache: um volume nomeado guarda o cache de build entre execuções.
ENV GOTOOLCHAIN=local \
    GOPROXY=off \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=1 \
    GOCACHE=/gocache/build \
    GOMODCACHE=/gocache/mod

RUN mkdir -p /gocache/build /gocache/mod && chmod -R 1777 /gocache
