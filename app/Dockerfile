# Imagem do serviço warmup-api e do pricing (construída pelo start.sh; não faz parte
# dos arquivos editáveis do desafio).
FROM golang:1.25.3-bookworm AS build
WORKDIR /src
COPY go.mod ./
COPY *.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/app .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/app /app
# UID numérico: permite que o kubelet verifique runAsNonRoot.
USER 65532:65532
EXPOSE 8080
ENTRYPOINT ["/app"]
