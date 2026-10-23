# textstats — biblioteca de estatísticas de texto + ferramenta wordfreq.
#
# Build (é assim que eu sempre fiz, funciona):
#   cd textstats && cmake . && make
cmake_minimum_required(VERSION 3.16)
project(textstats VERSION 1.4.2 LANGUAGES CXX)

# Flags do projeto inteiro: C++20, otimização e warnings tratados como erro.
# O utf8 fica em src/utf8, então já deixo no include path de todo mundo.
set(CMAKE_CXX_FLAGS "-std=c++20 -O2 -g -Wall -Wextra -Werror -I${CMAKE_SOURCE_DIR}/src/utf8")

include_directories(include src)

enable_testing()

add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(test)
