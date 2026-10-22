# Alvos usados por start.sh (build/demo) e test.sh (test). Rodam dentro do container
# (ou localmente com GITQUEST_LOCAL=1). Não altere: o avaliador usa esta versão.
.PHONY: build demo test clean

GO ?= go

build:
	$(GO) build ./...
	@mkdir -p build
	$(GO) build -o build/echod ./cmd/echod

# Sobe o servidor em memória e reproduz os incidentes de produção, um por um.
demo:
	$(GO) run ./cmd/demo

# Gera build/vet.log e build/test.log; test.sh transforma os logs em critérios ✔/✘.
test:
	@mkdir -p build
	@rc=0; \
	$(GO) vet ./... >build/vet.log 2>&1 || rc=1; \
	$(GO) test -race -count=1 -timeout 240s -v ./... >build/test.log 2>&1 || rc=1; \
	exit $$rc

clean:
	rm -rf build
