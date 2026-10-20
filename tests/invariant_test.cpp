// Invariantes estruturais declarados no README:
//   - capacity() é potência de 2;
//   - size() / capacity() <= 0,75 depois de qualquer inserção;
//   - size() é exatamente o número de elementos visitados por for_each;
//   - sob ciclos de insere/apaga com poucos elementos vivos, a memória não cresce sem limite.
#include <cstdint>

#include "flat_map.hpp"
#include "mini_test.hpp"

namespace {

bool is_power_of_two(std::size_t x) { return x != 0 && (x & (x - 1)) == 0; }

template <class Map>
std::size_t count_elements(const Map& m) {
    std::size_t n = 0;
    m.for_each([&](const auto&, const auto&) { ++n; });
    return n;
}

template <class Map>
void check_load(const Map& m, std::uint64_t step) {
    CHECK_MSG(is_power_of_two(m.capacity()),
              "passo " << step << ": capacity() = " << m.capacity() << " não é potência de 2");
    CHECK_MSG(m.size() * 4 <= m.capacity() * 3,
              "passo " << step << ": fator de carga " << m.size() << "/" << m.capacity() << " = "
                       << static_cast<double>(m.size()) / static_cast<double>(m.capacity())
                       << " passou de 0,75");
}

}  // namespace

TEST(load_factor_never_exceeds_limit) {
    gq::FlatMap<std::uint64_t, int> m;
    check_load(m, 0);
    for (std::uint64_t k = 1; k <= 50000; ++k) {
        m.insert_or_assign(k * 31, 1);
        check_load(m, k);
    }
}

TEST(load_factor_holds_with_erases_and_overwrites) {
    gq::FlatMap<std::uint64_t, int> m;
    std::uint64_t step = 0;
    for (std::uint64_t round = 0; round < 20; ++round) {
        for (std::uint64_t k = 0; k < 1000; ++k) {
            m.insert_or_assign(round * 500 + k, static_cast<int>(k));
            check_load(m, ++step);
        }
        for (std::uint64_t k = 0; k < 400; ++k) {
            m.erase(round * 500 + k * 2);
        }
    }
}

TEST(size_matches_elements) {
    gq::FlatMap<int, int> m;
    for (int round = 0; round < 3; ++round) {
        for (int k = 0; k < 300; ++k) {
            m.insert_or_assign(k, round);  // da segunda rodada em diante, só sobrescreve
        }
        CHECK_EQ(m.size(), 300u);
        CHECK_EQ(count_elements(m), m.size());
    }
    for (int k = 0; k < 300; k += 3) {
        m.erase(k);
    }
    CHECK_EQ(m.size(), 200u);
    CHECK_EQ(count_elements(m), m.size());
}

// Simula o uso real (sessões que entram e saem): no máximo 16 chaves vivas,
// mas 200 mil chaves diferentes passam pela tabela.
TEST(churn_keeps_capacity_bounded) {
    gq::FlatMap<std::uint64_t, std::uint64_t> m;
    const std::uint64_t live = 16;
    const std::size_t max_capacity = 128;
    for (std::uint64_t k = 0; k < 200000; ++k) {
        m.insert_or_assign(k, k);
        if (k >= live) {
            CHECK_MSG(m.erase(k - live), "chave " << k - live << " não encontrada para apagar");
        }
        CHECK_MSG(m.size() <= live + 1, "size() = " << m.size() << " no passo " << k);
        CHECK_MSG(m.capacity() <= max_capacity,
                  "capacity() = " << m.capacity() << " com no máximo " << live + 1
                                  << " elementos vivos (passo " << k << "): a memória cresce sem limite");
    }
    for (std::uint64_t k = 200000 - live; k < 200000; ++k) {
        CHECK(m.contains(k));
    }
    CHECK(!m.contains(0));
}
