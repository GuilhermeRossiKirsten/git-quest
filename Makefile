# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do container
# gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1 e g++ >= 14).
#
#   make build   compila o serviço relay (com sanitizers)
#   make run     roda o relay com examples/relay.ini
#   make test    compila e roda os testes (tests/run-tests.sh)
.PHONY: build run test clean

CXX      ?= g++
STD      := -std=c++17 -pedantic-errors
WARN     := -Wall -Wextra
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CXXFLAGS := $(STD) $(WARN) -O1 -g $(SAN)
BUILD    := build

SRC := src/config.cpp
HDR := src/config.hpp

build: $(BUILD)/relay

$(BUILD)/relay: src/main.cpp $(SRC) $(HDR)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(SRC)

$(BUILD)/test_config: tests/test_config.cpp tests/check.hpp $(SRC) $(HDR)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_config.cpp $(SRC)

run: $(BUILD)/relay
	./$(BUILD)/relay examples/relay.ini

test:
	bash tests/run-tests.sh

clean:
	rm -rf $(BUILD)
