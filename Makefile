# Avaliador do desafio-28 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila o núcleo (src/promo/*.cpp), a demonstração e os testes do núcleo
#   make test    roda tests/run.sh (cada critério com ✔/✘)
#   make clean   apaga build/

CXX ?= g++
# Sanitizers ligados por padrão; o modo local no Windows (MinGW) os desliga com SAN=.
SAN ?= -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CXXFLAGS := -std=c++20 -pedantic-errors -Wall -Wextra -g -O1 $(SAN) -Isrc -Itests -MMD -MP
LDFLAGS := $(SAN)

# O "núcleo": todo .cpp em src/promo/ (você pode criar arquivos novos lá).
CORE_SRC    := $(sort $(wildcard src/promo/*.cpp))
CORE_OBJ    := $(CORE_SRC:%.cpp=build/%.o)
SUPPORT_OBJ := build/tests/support/main.o build/tests/support/alloc_counter.o
# Cada tests/plugin_*.cpp é uma unidade de tradução separada que o núcleo não conhece.
PLUGIN_SRC  := $(sort $(wildcard tests/plugin_*.cpp))
PLUGIN_OBJ  := $(PLUGIN_SRC:%.cpp=build/%.o)

.PHONY: build test clean

build: build/caixa build/core_tests

build/caixa: build/src/main.o $(CORE_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

build/core_tests: build/tests/core_tests.o $(SUPPORT_OBJ) $(CORE_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

build/plugin_tests: $(PLUGIN_OBJ) $(SUPPORT_OBJ) $(CORE_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

# Recompila tudo se as flags mudarem (o Makefile é dependência de cada objeto).
build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test:
	@bash tests/run.sh

clean:
	rm -rf build

-include $(wildcard build/src/*.d build/src/promo/*.d build/tests/*.d build/tests/support/*.d)
