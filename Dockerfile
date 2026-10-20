# Imagem do serviço greeter.
# "Funciona na minha máquina" — foi o primeiro Dockerfile do time e nunca mais
# ninguém mexeu nele.
FROM golang:1.25.3-bookworm

WORKDIR /src
COPY . .

WORKDIR /src/app
RUN go mod download
# Compila uma vez para aquecer o cache: assim o "go run" do CMD sobe rápido.
RUN go build ./...

EXPOSE 8080
CMD ["go", "run", "."]
