# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do
# container gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1).
CXX ?= g++
# Sanitizers ligados por padrão; o modo local no Windows (MinGW) os desliga com SAN=.
SAN ?= -fsanitize=address,undefined -fno-sanitize-recover=all
CXXFLAGS := -std=c++20 -pedantic-errors -Wall -Wextra -g -O1 $(SAN) -Isrc -Itests

HEADERS := $(wildcard src/gq/*.hpp) $(wildcard tests/support/*.hpp)

.PHONY: build test clean

build: build/report build/runtime_tests

build/report: src/report.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

build/runtime_tests: tests/runtime_tests.cpp $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test:
	@bash tests/run.sh

clean:
	rm -rf build
