# Avaliador do desafio-27 — NÃO edite: start.sh e test.sh chamam estes alvos.
# Rodam dentro do container (gcc:14.2.0 + CMake 3.31.6 + Ninja 1.12.1) ou,
# com GITQUEST_LOCAL=1, com as ferramentas da sua máquina.
#
#   make build   compila o projeto textstats/ do jeito mais simples (cópia em build/start)
#   make test    roda os 7 critérios de tests/run.sh
#   make clean   apaga build/

MIN_GCC   := 14
MIN_CMAKE := 3.25

.PHONY: build test check-toolchain clean

build: check-toolchain
	@rm -rf build/start && mkdir -p build/start
	@cp -R textstats build/start/src
	@cmake -S build/start/src -B build/start/build -G Ninja -DCMAKE_BUILD_TYPE=Release >build/start/configure.log 2>&1 || \
		{ tail -20 build/start/configure.log; exit 1; }
	@cmake --build build/start/build >build/start/build.log 2>&1 || { tail -20 build/start/build.log; exit 1; }
	@echo "ok  textstats/ configurado e compilado numa cópia em build/start (caminho sem espaços)"

test: check-toolchain
	@bash tests/run.sh

check-toolchain:
	@command -v cmake >/dev/null && command -v ninja >/dev/null && command -v g++ >/dev/null || \
		{ echo "erro: são necessários g++ >= $(MIN_GCC), cmake >= $(MIN_CMAKE) e ninja" >&2; exit 1; }
	@v=$$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion); \
	if [ "$${v%%.*}" -lt $(MIN_GCC) ] 2>/dev/null; then \
		echo "erro: g++ $$v encontrado; este desafio exige g++ >= $(MIN_GCC) (imagem gcc:14.2.0)" >&2; exit 1; \
	fi
	@v=$$(cmake --version | head -1 | sed 's/[^0-9.]//g'); major=$${v%%.*}; rest=$${v#*.}; minor=$${rest%%.*}; \
	if [ "$$major" -lt 3 ] || { [ "$$major" -eq 3 ] && [ "$$minor" -lt 25 ]; }; then \
		echo "erro: cmake $$v encontrado; este desafio exige cmake >= $(MIN_CMAKE)" >&2; exit 1; \
	fi

clean:
	rm -rf build
