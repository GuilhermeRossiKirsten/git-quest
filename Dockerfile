# Toolchain do desafio: apenas ferramentas, nenhum código do desafio é copiado.
FROM golang:1.25.3-bookworm

# Cache de compilação gravável pelo usuário sem privilégios do container.
# test.sh monta o volume nomeado gitquest-go-cache em /cache; na primeira
# vez, o Docker copia este diretório (e suas permissões) para o volume.
RUN mkdir -p /cache/go-build /cache/gomod && chmod -R 0777 /cache

# GOTOOLCHAIN=local: nunca tentar baixar outra versão do Go (não há rede).
# CGO_ENABLED=1: o detector de corridas (-race) precisa de cgo.
ENV GOCACHE=/cache/go-build \
    GOMODCACHE=/cache/gomod \
    GOTOOLCHAIN=local \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=1
