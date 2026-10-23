# Avaliador do desafio-33 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila src/ nas três variantes (ASan+UBSan, TSan e otimizada)
#   make test    compila os testes e executa cada um em processo próprio (veja tests/run.sh)
#   make clean   apaga build/
#
# AddressSanitizer e ThreadSanitizer não podem coexistir num binário: build/asan/ e
# build/tsan/ são builds separados. build/perf/ (-O2, sem sanitizers) só serve para a
# medição informativa de vazão.

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++20
WARN     := -pedantic-errors -Wall -Wextra
BASE     := $(STD) $(WARN) -pthread -g -fno-omit-frame-pointer -MMD -MP

ASAN     := -O1 -fsanitize=address,undefined -fno-sanitize-recover=all
TSAN     := -O1 -fsanitize=thread
PERF     := -O2 -DNDEBUG

SRC      := $(sort $(wildcard src/*.cpp))
TESTS    := $(sort $(wildcard tests/*.cpp))

ASAN_OBJ := $(SRC:%.cpp=build/asan/%.o) $(TESTS:%.cpp=build/asan/%.o)
TSAN_OBJ := $(SRC:%.cpp=build/tsan/%.o) $(TESTS:%.cpp=build/tsan/%.o)
PERF_OBJ := $(SRC:%.cpp=build/perf/%.o) $(TESTS:%.cpp=build/perf/%.o)

.PHONY: build test check-toolchain clean

build: check-toolchain $(SRC:%.cpp=build/asan/%.o) $(SRC:%.cpp=build/tsan/%.o) $(SRC:%.cpp=build/perf/%.o)
	@echo "ok  src/ compilado com $(STD) -pedantic-errors (ASan+UBSan, TSan e -O2)"

test: check-toolchain
	@bash tests/run.sh

build/asan/test-runner: $(ASAN_OBJ)
	$(CXX) -pthread -fsanitize=address,undefined -o $@ $^

build/tsan/test-runner: $(TSAN_OBJ)
	$(CXX) -pthread -fsanitize=thread -o $@ $^

build/perf/test-runner: $(PERF_OBJ)
	$(CXX) -pthread -o $@ $^

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/asan/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(BASE) $(ASAN) -c $< -o $@

build/tsan/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(BASE) $(TSAN) -c $< -o $@

build/perf/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(BASE) $(PERF) -c $< -o $@

check-toolchain:
	@v=$$($(CXX) -dumpfullversion 2>/dev/null || $(CXX) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CXX) $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(ASAN_OBJ:.o=.d) $(TSAN_OBJ:.o=.d) $(PERF_OBJ:.o=.d)
