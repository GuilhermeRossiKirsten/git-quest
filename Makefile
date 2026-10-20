# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do container
# (ou localmente com GITQUEST_LOCAL=1, exigindo g++ >= 14 com sanitizers).
CXX ?= g++
CXXFLAGS := -std=c++14 -pedantic-errors -Wall -Wextra -g -O1 \
            -fno-omit-frame-pointer -fsanitize=address,undefined -Isrc

.PHONY: build test clean

build: build/pipeline

build/pipeline: src/main.cpp $(wildcard src/gq/*.hpp)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@

test:
	@bash tests/run.sh

clean:
	rm -rf build
