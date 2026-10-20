# Avaliador do desafio-16 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila src/ nas duas configurações (correção e desempenho)
#   make test    compila os testes e executa cada um em processo próprio
#   make clean   apaga build/
#
# Duas configurações:
#   build/san   -O1 -g com AddressSanitizer + UndefinedBehaviorSanitizer (tests/test_*.cpp)
#   build/perf  -O2 -DNDEBUG, sem sanitizers, para medir tempo (tests/perf_*.cpp)

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++17
WARN     := -pedantic-errors -Wall -Wextra
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

SAN_FLAGS  := $(STD) $(WARN) $(SANITIZE) -g -O1 -MMD -MP
PERF_FLAGS := $(STD) $(WARN) -O2 -DNDEBUG -MMD -MP

SRC        := $(sort $(wildcard src/*.cpp))
SAN_TESTS  := tests/main.cpp $(sort $(wildcard tests/test_*.cpp))
PERF_TESTS := tests/main.cpp $(sort $(wildcard tests/perf_*.cpp))

SAN_SRC_OBJ   := $(SRC:%.cpp=build/san/%.o)
SAN_TEST_OBJ  := $(SAN_TESTS:%.cpp=build/san/%.o)
PERF_SRC_OBJ  := $(SRC:%.cpp=build/perf/%.o)
PERF_TEST_OBJ := $(PERF_TESTS:%.cpp=build/perf/%.o)

.PHONY: build test check-toolchain clean

build: check-toolchain $(SAN_SRC_OBJ) $(PERF_SRC_OBJ)
	@echo "ok  src/ compilado em C++17 (-O1 com ASan + UBSan e -O2 sem sanitizers)"

test: check-toolchain
	@bash tests/run.sh

build/test-runner: $(SAN_SRC_OBJ) $(SAN_TEST_OBJ)
	$(CXX) $(SANITIZE) -o $@ $^

build/perf-runner: $(PERF_SRC_OBJ) $(PERF_TEST_OBJ)
	$(CXX) -o $@ $^

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/san/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_FLAGS) -c $< -o $@

build/perf/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(PERF_FLAGS) -c $< -o $@

check-toolchain:
	@v=$$($(CXX) -dumpfullversion 2>/dev/null || $(CXX) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CXX) $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(wildcard build/san/*/*.d build/perf/*/*.d)
