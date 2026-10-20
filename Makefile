# Avaliador do desafio-14 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila a demonstração examples/telemetry_demo.cpp (C++17, ASan + UBSan)
#   make demo    compila e executa a demonstração do log de telemetria
#   make test    compila cada tests/test_*.cpp num binário próprio e executa os testes
#   make clean   apaga build/

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++17
WARN     := -pedantic-errors -Wall -Wextra
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CXXFLAGS := $(STD) $(WARN) $(SANITIZE) -g -O1 -Isrc -Itests -MMD -MP
LDFLAGS  := $(SANITIZE)

TESTS     := $(sort $(wildcard tests/test_*.cpp))
TEST_BINS := $(TESTS:tests/%.cpp=build/bin/%)
OBJS      := build/obj/tests/main.o build/obj/examples/telemetry_demo.o $(TESTS:%.cpp=build/obj/%.o)

.PHONY: build demo test check-toolchain clean

build: check-toolchain build/bin/telemetry_demo
	@echo "ok  src/ring_buffer.h compila em $(STD) -pedantic-errors com sanitizers (build/bin/telemetry_demo)"

demo: build
	@./build/bin/telemetry_demo

test: check-toolchain
	@bash tests/run.sh

build/bin/telemetry_demo: build/obj/examples/telemetry_demo.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^

build/bin/test_%: build/obj/tests/test_%.o build/obj/tests/main.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^

# Recompila se as flags mudarem (o Makefile é dependência de cada objeto); os
# arquivos .d gerados por -MMD fazem o mesmo quando src/ring_buffer.h muda.
build/obj/%.o: %.cpp Makefile
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

-include $(OBJS:.o=.d)
