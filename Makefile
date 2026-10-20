# Avaliador do desafio-12 — NÃO edite: ./test.sh compila e testa com estas flags.
# Os alvos rodam dentro do container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
#
#   make build   compila o relatório build/logscan (C++23, sem sanitizers: mostra os sintomas)
#   make test    compila os testes e executa cada um em processo próprio
#   make clean   apaga build/
CXX ?= g++
# Sanitizers ligados por padrão; o modo local no Windows (MinGW) os desliga com SAN=.
SAN ?= -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
BASEFLAGS := -std=c++23 -pedantic-errors -Wall -Wextra -g -O1 -Isrc -Itests
CXXFLAGS := $(BASEFLAGS) $(SAN)

HEADERS := $(wildcard src/gq/*.hpp) $(wildcard tests/support/*.hpp)

.PHONY: build test clean

build: build/logscan

build/logscan: src/main.cpp src/logscan.cpp $(HEADERS) Makefile
	@mkdir -p build
	$(CXX) $(BASEFLAGS) src/main.cpp src/logscan.cpp -o $@

build/runtime_tests: tests/runtime_tests.cpp src/logscan.cpp $(HEADERS) Makefile
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/runtime_tests.cpp src/logscan.cpp -o $@

test:
	@bash tests/run.sh

clean:
	rm -rf build
