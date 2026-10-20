// Garante que o código foi compilado com o padrão e a toolchain do desafio.
#include "check.h"

TEST(toolchain_cpp11, "compilado em C++11 (__cplusplus == 201103L) com g++ >= 14") {
    CHECK(__cplusplus == 201103L,
          "__cplusplus é " << __cplusplus << "; este desafio exige -std=c++11 (201103L)");
#if defined(__clang__) || !defined(__GNUC__)
    CHECK(false, "compilador inesperado: este desafio usa g++ (gcc:14.2.0)");
#else
    CHECK(__GNUC__ >= 14, "g++ " << __GNUC__ << "." << __GNUC_MINOR__ << " é antigo; use g++ >= 14");
#endif
}
