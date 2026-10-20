# Avaliador do desafio-13 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila o código de src/ nas duas variantes (ASan+UBSan e TSan)
#   make test    compila os testes e executa cada um em processo próprio, nas duas variantes
#   make clean   apaga build/
#
# AddressSanitizer e ThreadSanitizer não podem ser combinados num mesmo binário,
# por isso há dois builds separados: build/asan/ e build/tsan/.

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++20
WARN     := -pedantic-errors -Wall -Wextra
BASE     := $(STD) $(WARN) -pthread -g -O1 -fno-omit-frame-pointer -MMD -MP

ASAN     := -fsanitize=address,undefined -fno-sanitize-recover=all
TSAN     := -fsanitize=thread

SRC      := $(sort $(wildcard src/*.cpp))
TESTS    := $(sort $(wildcard tests/*.cpp))

ASAN_OBJ := $(SRC:%.cpp=build/asan/%.o) $(TESTS:%.cpp=build/asan/%.o)
TSAN_OBJ := $(SRC:%.cpp=build/tsan/%.o) $(TESTS:%.cpp=build/tsan/%.o)

.PHONY: build test check-toolchain clean

build: check-toolchain $(SRC:%.cpp=build/asan/%.o) $(SRC:%.cpp=build/tsan/%.o)
	@echo "ok  src/ compilado com $(STD) -pedantic-errors (ASan+UBSan e TSan)"

test: check-toolchain
	@bash tests/run.sh

build/asan/test-runner: $(ASAN_OBJ)
	$(CXX) -pthread $(ASAN) -o $@ $^

build/tsan/test-runner: $(TSAN_OBJ)
	$(CXX) -pthread $(TSAN) -o $@ $^

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/asan/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(BASE) $(ASAN) -c $< -o $@

build/tsan/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(BASE) $(TSAN) -c $< -o $@

check-toolchain:
	@v=$$($(CXX) -dumpfullversion 2>/dev/null || $(CXX) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CXX) $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(ASAN_OBJ:.o=.d) $(TSAN_OBJ:.o=.d)
