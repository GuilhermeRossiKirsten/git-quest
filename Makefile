# Avaliador do desafio-11 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila o código de src/ (C++11, -pedantic-errors, ASan + UBSan)
#   make test    compila os testes e executa cada um em processo próprio
#   make clean   apaga build/

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++11
WARN     := -pedantic-errors -Wall -Wextra
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CXXFLAGS := $(STD) $(WARN) $(SANITIZE) -g -O1 -MMD -MP
LDFLAGS  := $(SANITIZE)

SRC      := $(sort $(wildcard src/*.cpp))
TESTS    := $(sort $(wildcard tests/*.cpp))
SRC_OBJ  := $(SRC:%.cpp=build/%.o)
TEST_OBJ := $(TESTS:%.cpp=build/%.o)

.PHONY: build test static-check check-toolchain clean

build: check-toolchain $(SRC_OBJ)
	@echo "ok  src/ compilado com $(STD) -pedantic-errors e sanitizers"

test: check-toolchain
	@bash tests/run.sh

build/test-runner: $(SRC_OBJ) $(TEST_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

# Verificações em tempo de compilação (static_assert), sem gerar executável.
static-check:
	$(CXX) $(STD) $(WARN) -fsyntax-only tests/static/nothrow_move.cpp

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

check-toolchain:
	@v=$$($(CXX) -dumpfullversion 2>/dev/null || $(CXX) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CXX) $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(SRC_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
