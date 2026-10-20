# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do container
# (ou na sua máquina, se você tiver Go 1.25.3 com cgo).
.PHONY: build test demo bench clean

build:
	go build ./...

test:
	bash tests/run.sh

# Executa o gerador de miniaturas para observar os sintomas (veja o README).
demo:
	go run -race ./src/cmd/thumbs -fail 3

bench:
	go test -run '^$$' -bench . -benchmem ./tests/

clean:
	rm -rf build
