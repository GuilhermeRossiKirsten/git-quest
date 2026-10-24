# Avaliador do desafio-32 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build    compila build/tlvparse (C17, -Werror, ASan + UBSan + LSan)
#   make harden   compila build/tlvparse-hardened (-O2, _FORTIFY_SOURCE=3, -Wconversion...)
#   make demo     roda o tlvparse com três pacotes de exemplo
#   make test     executa os critérios de tests/run.py
#   make clean    apaga build/

CC       := gcc
MIN_GCC  := 14
STD      := -std=c17 -D_GNU_SOURCE
WARN     := -Wall -Wextra -Wpedantic -Werror
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS   := $(STD) $(WARN) $(SANITIZE) -g -O1

# Build separada, sem sanitizers, como iria para o coletor em produção.
HARDEN_FLAGS := $(STD) -O2 -D_FORTIFY_SOURCE=3 -fstack-protector-strong \
                -Wall -Wextra -Wpedantic -Wformat=2 -Wconversion -Wsign-conversion -Werror

SRC     := $(sort $(wildcard src/*.c))
LIB_SRC := $(filter-out src/main.c,$(SRC))
HDR     := $(wildcard src/*.h)

.PHONY: build harden demo test check-toolchain clean

build: check-toolchain build/tlvparse
	@echo "ok  build/tlvparse compilado com -std=c17 $(WARN) e sanitizers"

build/tlvparse: $(SRC) $(HDR) Makefile
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC)

# Driver de fuzzing do avaliador: tlv_process() chamado em laço, no mesmo processo.
build/fuzz-driver: tests/fuzz_driver.c $(LIB_SRC) $(HDR) Makefile
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/fuzz_driver.c $(LIB_SRC)

harden: check-toolchain
	@mkdir -p build
	$(CC) $(HARDEN_FLAGS) -o build/tlvparse-hardened $(SRC)

demo: build
	@python3 tests/demo.py

test: check-toolchain
	@python3 tests/run.py

check-toolchain:
	@v=$$($(CC) -dumpfullversion 2>/dev/null || $(CC) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CC) $$v encontrado; este desafio exige gcc >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build
