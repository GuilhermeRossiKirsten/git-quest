# Toolchain do desafio: Go fixado + o driver PostgreSQL (pgx) já baixado.
# Nenhum código do desafio é copiado: ele é montado em /work por gq_docker_run,
# que roda como usuário não-root e conectado só à rede interna do compose
# (sem internet). Por isso os módulos são baixados AQUI, no build da imagem.
FROM golang:1.25.3-bookworm

# - GOTOOLCHAIN=local: nunca tentar baixar outra versão do Go.
# - GOFLAGS=-buildvcs=false: /work/.git de um worktree aponta para fora do container.
# - GOMODCACHE=/opt/gomod: módulos dentro da imagem (somente leitura em tempo de teste);
#   GOPROXY=off depois do download garante que nada é buscado na rede ao testar —
#   o go.sum do repositório confere os hashes dos módulos da imagem.
# - GOCACHE em /gocache: um volume nomeado guarda o cache de build entre execuções.
ENV GOTOOLCHAIN=local \
    GOFLAGS=-buildvcs=false \
    CGO_ENABLED=1 \
    GOMODCACHE=/opt/gomod \
    GOCACHE=/gocache/build

# Mesmas versões do go.mod/go.sum (pgx v5.7.6 e suas dependências mínimas).
RUN mkdir -p /tmp/deps && cd /tmp/deps \
 && go mod init deps \
 && go get github.com/jackc/pgx/v5@v5.7.6 \
 && go mod download all \
 && cd / && rm -rf /tmp/deps \
 && mkdir -p /gocache/build && chmod -R 1777 /gocache

ENV GOPROXY=off
