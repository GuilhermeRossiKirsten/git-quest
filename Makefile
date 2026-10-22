# Avaliador do desafio-21 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila o minisup (C17 + POSIX, -Werror, ASan + UBSan) e o gq-helper
#   make demo    roda o minisup com examples/demo.conf e mostra os processos
#   make test    executa os critérios de tests/run.py
#   make clean   apaga build/

CC       := gcc
MIN_GCC  := 14
STD      := -std=c17 -D_GNU_SOURCE
WARN     := -Wall -Wextra -Wpedantic -Werror
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS   := $(STD) $(WARN) $(SANITIZE) -g -O1 -MMD -MP

SRC := $(sort $(wildcard src/*.c))
OBJ := $(SRC:%.c=build/%.o)

.PHONY: build demo test check-toolchain clean

build: check-toolchain build/minisup build/gq-helper
	@echo "ok  build/minisup compilado com -std=c17 $(WARN) e sanitizers"

build/minisup: $(OBJ)
	$(CC) $(SANITIZE) -o $@ $^

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# O programa auxiliar dos testes não usa sanitizers: ele só finge ser um serviço.
build/gq-helper: tests/helper.c Makefile
	@mkdir -p build
	$(CC) $(STD) -Wall -Wextra -Werror -O2 -o $@ $<

demo: build
	@bash tests/demo.sh

test: check-toolchain build/gq-helper
	@python3 tests/run.py

check-toolchain:
	@v=$$($(CC) -dumpfullversion 2>/dev/null || $(CC) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CC) $$v encontrado; este desafio exige gcc >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(OBJ:.o=.d)
