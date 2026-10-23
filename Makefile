# Alvos usados por start.sh (build/demo), calc.sh (calc) e test.sh (test). Rodam
# dentro do container (ou localmente com GITQUEST_LOCAL=1). Não altere: o
# avaliador usa esta versão.
.PHONY: build demo calc test clean

GO ?= go

build:
	$(GO) build ./...

# Reproduz os chamados do suporte: fórmula, resultado esperado e obtido.
demo:
	$(GO) run ./cmd/calc -demo

# make calc ARGS="'2 - 3 - 4'" — use ./calc.sh, que cuida das aspas.
calc:
	$(GO) run ./cmd/calc $(ARGS)

# Gera build/vet.log e build/test.log; test.sh transforma os logs em critérios ✔/✘.
test:
	@mkdir -p build
	@rc=0; \
	$(GO) vet ./... >build/vet.log 2>&1 || rc=1; \
	$(GO) test -count=1 -timeout 240s -v ./... >build/test.log 2>&1 || rc=1; \
	exit $$rc

clean:
	rm -rf build
