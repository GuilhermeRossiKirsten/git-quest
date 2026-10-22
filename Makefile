# Atalhos para mantenedores, executados na branch main.
.PHONY: help list check-env validate lint selftest sync-tooling

help:
	@echo "make list          lista o catálogo"
	@echo "make check-env     verifica ferramentas locais"
	@echo "make validate      valida catálogo, template e consistência com as branches"
	@echo "make lint          shellcheck em todos os scripts (se instalado)"
	@echo "make selftest      testes do validador (scripts/tests)"
	@echo "make sync-tooling  copia as ferramentas compartilhadas para as branches desafio-*"

list:
	@./scripts/gitquest list

check-env:
	@./scripts/check-environment.sh

validate: selftest
	@./scripts/list-challenges.sh --check

lint:
	@if command -v shellcheck >/dev/null; then \
		shellcheck -x scripts/gitquest scripts/*.sh scripts/lib/common.sh templates/challenge/*.sh; \
	else echo "shellcheck não instalado"; fi

selftest:
	@py=$$(for p in python3 python; do $$p -c '' 2>/dev/null && { echo $$p; break; }; done); \
		[ -n "$$py" ] || { echo "Python >= 3.8 não encontrado" >&2; exit 1; }; \
		PYTHONDONTWRITEBYTECODE=1 $$py -m unittest discover -s scripts/tests -v

sync-tooling:
	@./scripts/sync-tooling.sh
