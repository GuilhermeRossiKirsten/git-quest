# Alvos usados por start.sh (build/demo) e test.sh (test). Rodam dentro do container
# (ou localmente com GITQUEST_LOCAL=1). Não altere: o avaliador usa esta versão.
.PHONY: build demo test clean

GO ?= go

build:
	$(GO) build ./...

# Mostra os sintomas relatados pelo suporte (esperado × observado).
demo:
	$(GO) run ./cmd/demo

# Gera build/vet.log e build/test.log; test.sh transforma os logs em critérios ✔/✘.
test:
	@mkdir -p build
	@rc=0; \
	$(GO) vet ./... >build/vet.log 2>&1 || rc=1; \
	$(GO) test -count=1 -timeout 180s -v ./... >build/test.log 2>&1 || rc=1; \
	exit $$rc

clean:
	rm -rf build
