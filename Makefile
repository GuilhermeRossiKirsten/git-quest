# Avaliador do desafio-30 — NÃO edite: ./test.sh compila e mede com estas flags.
# Os alvos rodam dentro do container da toolchain (ou localmente com GITQUEST_LOCAL=1).
#
#   make build     compila o painel duas vezes: com ASan + UBSan (testes funcionais)
#                  e com -O2 -g sem sanitizers (build/opt/bin/bench, medido pelo Valgrind)
#   make test      testes funcionais + medição de cache (tests/run.sh)
#   make profile   só a medição: tabela de Ir / misses de D1 / misses de LL por consulta
#   make clean     apaga build/

CXX      := g++
MIN_GCC  := 14
STD      := -std=c++20
WARN     := -pedantic-errors -Wall -Wextra
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
SAN_FLAGS := $(STD) $(WARN) $(SANITIZE) -g -O1 -Isrc -Itests -MMD -MP
OPT_FLAGS := $(STD) $(WARN) -O2 -g -Isrc -Itests -MMD -MP

TESTS     := $(sort $(wildcard tests/test_*.cpp))
TEST_BINS := $(TESTS:tests/%.cpp=build/san/bin/%)
SAN_OBJS  := build/san/obj/src/sales_store.o build/san/obj/tests/main.o $(TESTS:%.cpp=build/san/obj/%.o)
OPT_OBJS  := build/opt/obj/src/sales_store.o build/opt/obj/tests/bench.o

.PHONY: build test profile check-toolchain clean

build: check-toolchain build/san/obj/src/sales_store.o build/opt/bin/bench
	@echo "ok  src/ compila em $(STD) -pedantic-errors (com sanitizers e com -O2)"

test: check-toolchain
	@bash tests/run.sh

profile: check-toolchain
	@bash tests/perf.sh

build/san/bin/test_%: build/san/obj/tests/test_%.o build/san/obj/tests/main.o build/san/obj/src/sales_store.o
	@mkdir -p $(dir $@)
	$(CXX) $(SANITIZE) -o $@ $^

build/opt/bin/bench: $(OPT_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

# O Makefile é dependência de cada objeto (flags novas => recompila); os .d
# gerados por -MMD fazem o mesmo quando um header muda.
build/san/obj/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(SAN_FLAGS) -c $< -o $@

build/opt/obj/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(OPT_FLAGS) -c $< -o $@

check-toolchain:
	@v=$$($(CXX) -dumpfullversion 2>/dev/null || $(CXX) -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: $(CXX) $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; \
		exit 1; \
	fi

clean:
	rm -rf build

-include $(SAN_OBJS:.o=.d) $(OPT_OBJS:.o=.d)
