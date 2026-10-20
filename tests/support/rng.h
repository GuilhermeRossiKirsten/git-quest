// rng.h — gerador pseudoaleatório determinístico (SplitMix64).
// Mesma semente => mesma sequência em qualquer compilador/plataforma, ao contrário de
// std::uniform_int_distribution, cuja saída depende da implementação da biblioteca.
#ifndef GQ_TESTS_RNG_H
#define GQ_TESTS_RNG_H

#include <cstdint>

namespace gq::testing {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Inteiro uniforme (o viés é desprezível para os tamanhos usados aqui) em [0, n).
    std::uint64_t below(std::uint64_t n) { return next() % n; }

private:
    std::uint64_t state_;
};

}  // namespace gq::testing

#endif
