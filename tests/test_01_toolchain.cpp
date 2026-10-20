// Confere o padrão e o compilador exigidos pelo desafio.
#include "check.h"

TEST(toolchain_cpp17, "o projeto é compilado em C++17 com g++ >= 14") {
    CHECK(__cplusplus == 201703L, "__cplusplus = " << __cplusplus << "; esperado 201703L (C++17)");
#if defined(__GNUC__) && !defined(__clang__)
    CHECK(__GNUC__ >= 14, "g++ " << __GNUC__ << " encontrado; o desafio exige g++ >= 14");
#else
    CHECK(false, "o desafio exige g++ (imagem gcc:14.2.0)");
#endif
}
