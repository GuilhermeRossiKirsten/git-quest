# Avaliador do desafio-15 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila src/ e os binários de teste (C++17)
#   make test    executa os testes funcionais (ASan + UBSan) e o de desempenho (-O2)
#   make clean   apaga build/

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++17
WARN     := -pedantic-errors -Wall -Wextra
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
# _GLIBCXX_ASSERTIONS: acesso fora dos limites em std::vector::operator[] aborta com mensagem.
CXXFLAGS := $(STD) $(WARN) $(SANITIZE) -D_GLIBCXX_ASSERTIONS -g -O1 -MMD -MP
LDFLAGS  := $(SANITIZE)
PERFFLAGS := $(STD) $(WARN) -O2 -DNDEBUG -MMD -MP

SRC      := $(sort $(wildcard src/*.cpp))
TESTS    := $(sort $(wildcard tests/*.cpp))
SRC_OBJ  := $(SRC:%.cpp=build/%.o)
TEST_OBJ := $(TESTS:%.cpp=build/%.o)
PERF     := build/heap-perf

.PHONY: build test check-toolchain clean

build: check-toolchain build/test-runner $(PERF)
	@echo "ok  src/ e testes compilados com $(STD) -pedantic-errors (ASan + UBSan; desempenho com -O2)"

test: check-toolchain
	@bash tests/run.sh

build/test-runner: $(SRC_OBJ) $(TEST_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

$(PERF): tests/perf/heap_perf.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(PERFFLAGS) -o $@ $<

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

-include $(SRC_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(PERF).d
