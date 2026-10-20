# Alvos usados por start.sh (build/simulate) e test.sh (test). Rodam dentro do
# container de toolchain, na rede do compose (DATABASE_URL aponta para o
# serviço postgres). Não altere: o avaliador usa esta versão.
.PHONY: build simulate test clean

GO ?= go

build:
	$(GO) build ./...

# Roda um cenário com reentregas, quedas e 4 consumidores e mostra o que deu errado.
simulate:
	$(GO) run ./cmd/simulate

# Gera build/vet.log e build/test.log; test.sh transforma os logs em critérios ✔/✘.
test:
	@mkdir -p build
	@rc=0; \
	$(GO) vet ./... >build/vet.log 2>&1 || rc=1; \
	$(GO) test -race -count=1 -timeout 240s -v ./tests/... >build/test.log 2>&1 || rc=1; \
	exit $$rc

clean:
	rm -rf build
