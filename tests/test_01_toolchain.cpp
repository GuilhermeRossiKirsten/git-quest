// Ambiente: o avaliador compila em C++17 com g++ >= 14.
#include "check.h"

TEST(toolchain_cpp17, "o código é compilado em C++17 com g++ >= 14") {
    CHECK(__cplusplus == 201703L, "__cplusplus = " << __cplusplus << "; esperava 201703L (-std=c++17)");
#if defined(__GNUC__) && !defined(__clang__)
    CHECK(__GNUC__ >= 14, "g++ " << __GNUC__ << " encontrado; o desafio exige g++ >= 14 (gcc:14.2.0)");
#else
    CHECK(false, "compilador inesperado: o desafio exige g++ >= 14 (gcc:14.2.0)");
#endif
}
