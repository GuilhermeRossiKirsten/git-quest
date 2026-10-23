# Toolchain do desafio-30: g++ 14.2 (C++20) + Valgrind com versão fixada.
# Nenhum código do desafio é copiado: o diretório do desafio é montado em /work.
#
# O Valgrind vem do Debian 12 (bookworm), base da imagem gcc:14.2.0. A versão é
# fixada para que o simulador de cache (callgrind --cache-sim=yes, o mesmo modelo
# do cachegrind) conte exatamente os mesmos acessos e misses em qualquer máquina.
FROM gcc:14.2.0
RUN apt-get update \
 && apt-get install -y --no-install-recommends valgrind=1:3.19.0-1 \
 && rm -rf /var/lib/apt/lists/* \
 && valgrind --version
