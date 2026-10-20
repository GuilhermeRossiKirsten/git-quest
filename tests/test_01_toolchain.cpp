// Confere o ambiente: padrão C++17 e g++ >= 14 (imagem gcc:14.2.0).
#include "check.h"
#include "ring_buffer.h"

TEST(toolchain_cpp17, "compilado em C++17 com g++ >= 14") {
    CHECK(__cplusplus == 201703L, "__cplusplus = " << __cplusplus << "; o desafio exige -std=c++17");
#if defined(__GNUC__) && !defined(__clang__)
    CHECK(__GNUC__ >= 14, "g++ " << __GNUC__ << " encontrado; o desafio exige g++ >= 14");
#else
    CHECK(false, "o desafio exige g++ (GCC) >= 14");
#endif
    CHECK((gq::RingBuffer<int, 3>::capacity() == 3), "capacity() deveria ser 3");
}
