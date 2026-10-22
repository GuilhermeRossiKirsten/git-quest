# Alvos usados por start.sh (build/demo) e test.sh (test). Rodam dentro do
# container de toolchain, na rede do compose (REDIS_ADDR aponta para o serviço
# redis). Não altere: o avaliador usa esta versão.
.PHONY: build demo test clean

GO ?= go

build:
	$(GO) build ./...

# Roda cenários de produção contra o Redis (banco lógico 1) e mostra os sintomas.
demo:
	$(GO) run ./cmd/demo

# Gera build/vet.log e build/test.log; test.sh transforma os logs em critérios ✔/✘.
test:
	@mkdir -p build
	@rc=0; \
	$(GO) vet ./... >build/vet.log 2>&1 || rc=1; \
	$(GO) test -race -count=1 -timeout 180s -v ./tests/... >build/test.log 2>&1 || rc=1; \
	exit $$rc

clean:
	rm -rf build
