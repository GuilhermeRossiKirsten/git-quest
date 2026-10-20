# Alvos usados por start.sh (build) e test.sh (test). Rodam dentro do container
# gcc:14.2.0 (ou localmente com GITQUEST_LOCAL=1 e g++ >= 14).
#
# Dois executáveis, compilados de formas diferentes de propósito:
#   build/correctness  testes de unidade, invariantes e diferencial, com
#                      AddressSanitizer + UndefinedBehaviorSanitizer (-O1 -g)
#   build/perf         teste de desempenho, -O2 SEM sanitizers (eles deixariam
#                      o código 2-3x mais lento e distorceriam a medição)
.PHONY: build test clean

CXX := g++
STD := -std=c++17
WARN := -Wall -Wextra -pedantic
INCLUDES := -Isrc -Itests
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

BUILD := build
SRC_DEPS := $(wildcard src/*)
CHECK_SRCS := tests/main.cpp tests/unit_test.cpp tests/invariant_test.cpp tests/differential_test.cpp

build: $(BUILD)/correctness $(BUILD)/perf

$(BUILD)/correctness: $(CHECK_SRCS) tests/mini_test.hpp $(SRC_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(STD) $(WARN) $(INCLUDES) -O1 -g $(SANITIZE) $(CHECK_SRCS) -o $@

$(BUILD)/perf: tests/perf_test.cpp $(SRC_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(STD) $(WARN) $(INCLUDES) -O2 -DNDEBUG tests/perf_test.cpp -o $@

test:
	@bash tests/run.sh

clean:
	rm -rf $(BUILD)
