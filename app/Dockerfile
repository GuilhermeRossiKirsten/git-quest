# Imagens do catalog-api (construídas pelo start.sh; não fazem parte dos
# arquivos editáveis do desafio). O mesmo código gera as três versões:
#   docker build --build-arg VERSION=2.0.0 --build-arg FEATURES=pricing ...
FROM golang:1.25.3-bookworm AS build
ARG VERSION=dev
ARG FEATURES=
ENV GOTOOLCHAIN=local GOFLAGS=-buildvcs=false CGO_ENABLED=0
WORKDIR /src
COPY go.mod ./
COPY *.go ./
RUN go vet ./... && \
    go build -trimpath -ldflags="-s -w -X main.version=${VERSION} -X main.features=${FEATURES}" -o /out/app .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/app /app
# UID numérico: permite que o kubelet verifique runAsNonRoot.
USER 65532:65532
EXPOSE 8080
ENTRYPOINT ["/app"]
