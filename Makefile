# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do container
# gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1 e g++ >= 14).
.PHONY: build test toolchain contract clean

CXX      ?= g++
CXXFLAGS := -std=c++23 -pedantic-errors -Wall -Wextra -O1 -g -Isrc
SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DGQ_SANITIZE

SRC_ALL    := $(wildcard src/*.cpp)
SRC_LIB    := $(filter-out src/main.cpp,$(SRC_ALL))
HEADERS    := $(wildcard src/*.hpp src/*.h)
TEST_SRC   := tests/main_test.cpp tests/parse_test.cpp tests/analyze_test.cpp \
              tests/report_test.cpp tests/exceptions_test.cpp
TEST_HDR   := tests/check.hpp tests/support.hpp

build: build/loganalyzer

test:
	@bash tests/run.sh

toolchain:
	$(CXX) -std=c++23 -pedantic-errors -fsyntax-only tests/toolchain_check.cpp

contract:
	$(CXX) $(CXXFLAGS) -fsyntax-only tests/contract_test.cpp

build/loganalyzer: $(SRC_ALL) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC_ALL) -o $@

build/tests: $(TEST_SRC) $(TEST_HDR) $(SRC_LIB) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Itests $(TEST_SRC) $(SRC_LIB) -o $@

build/asan/loganalyzer: $(SRC_ALL) $(HEADERS)
	@mkdir -p build/asan
	$(CXX) $(CXXFLAGS) $(SANFLAGS) $(SRC_ALL) -o $@

build/asan/tests: $(TEST_SRC) $(TEST_HDR) $(SRC_LIB) $(HEADERS)
	@mkdir -p build/asan
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -Itests $(TEST_SRC) $(SRC_LIB) -o $@

clean:
	rm -rf build
