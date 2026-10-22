# Imagem da loja do desafio-25 (construída pelo start.sh; não faz parte dos
# arquivos editáveis). Um único binário faz o papel de todos os componentes.
FROM golang:1.25.3-bookworm AS build
ENV GOTOOLCHAIN=local GOFLAGS=-buildvcs=false CGO_ENABLED=0
WORKDIR /src
COPY go.mod ./
COPY *.go ./
RUN go vet ./... && go build -trimpath -ldflags="-s -w" -o /out/app .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/app /app
# UID numérico: permite que o kubelet verifique runAsNonRoot.
USER 65532:65532
EXPOSE 8080 5432
ENTRYPOINT ["/app"]
