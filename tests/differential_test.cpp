// Teste diferencial: a mesma sequência pseudoaleatória de operações (semente fixa)
// é aplicada a gq::FlatMap e a std::unordered_map; qualquer divergência é um bug.
#include <cstdint>
#include <string>
#include <unordered_map>

#include "flat_map.hpp"
#include "mini_test.hpp"

namespace {

// Gerador splitmix64: determinístico e idêntico em qualquer compilador
// (std::uniform_int_distribution não garante isso).
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t n) { return next() % n; }
};

const char* const kOpNames[] = {"insert_or_assign", "erase", "find", "contains"};

template <class K, class V, class MakeKey, class MakeValue>
void run_differential(std::uint64_t seed, int operations, std::uint64_t key_space, MakeKey make_key,
                      MakeValue make_value) {
    gq::FlatMap<K, V> actual;
    std::unordered_map<K, V> expected;
    Rng rng{seed};

    for (int op = 0; op < operations; ++op) {
        const std::uint64_t dice = rng.below(100);
        // 45% insere, 25% apaga, 20% busca, 10% contains; o espaço de chaves é
        // pequeno para forçar sobrescritas, remoções e clusters.
        const int kind = dice < 45 ? 0 : dice < 70 ? 1 : dice < 90 ? 2 : 3;
        const K key = make_key(rng.below(key_space));
        auto where = [&] {
            return std::string("operação #") + std::to_string(op) + " (" + kOpNames[kind] + "): ";
        };

        switch (kind) {
            case 0: {
                const V value = make_value(rng.next());
                const bool inserted = actual.insert_or_assign(key, value);
                const bool expected_inserted = expected.insert_or_assign(key, value).second;
                CHECK_MSG(inserted == expected_inserted,
                          where() << "insert_or_assign devolveu " << inserted << ", esperado "
                                  << expected_inserted);
                break;
            }
            case 1: {
                const bool erased = actual.erase(key);
                const bool expected_erased = expected.erase(key) == 1;
                CHECK_MSG(erased == expected_erased,
                          where() << "erase devolveu " << erased << ", esperado " << expected_erased);
                break;
            }
            case 2: {
                const V* got = actual.find(key);
                const auto it = expected.find(key);
                if (it == expected.end()) {
                    CHECK_MSG(got == nullptr, where() << "find achou uma chave que não deveria existir");
                } else {
                    CHECK_MSG(got != nullptr, where() << "find não achou uma chave que existe");
                    CHECK_MSG(*got == it->second, where() << "find devolveu o valor errado");
                }
                break;
            }
            default: {
                const bool has = actual.contains(key);
                const bool expected_has = expected.count(key) == 1;
                CHECK_MSG(has == expected_has,
                          where() << "contains devolveu " << has << ", esperado " << expected_has);
                break;
            }
        }
        CHECK_MSG(actual.size() == expected.size(),
                  where() << "size() = " << actual.size() << ", esperado " << expected.size());
    }

    // Conteúdo final: cada elemento visitado existe na referência, sem repetição.
    std::size_t visited = 0;
    actual.for_each([&](const K& k, const V& v) {
        ++visited;
        const auto it = expected.find(k);
        CHECK_MSG(it != expected.end(), "for_each visitou uma chave que não deveria existir");
        CHECK_MSG(it->second == v, "for_each visitou um valor desatualizado");
    });
    CHECK_MSG(visited == expected.size(),
              "for_each visitou " << visited << " elementos, esperado " << expected.size());
}

}  // namespace

TEST(differential_int_keys_vs_unordered_map) {
    run_differential<std::uint64_t, std::uint64_t>(
        20251019, 100000, 2000, [](std::uint64_t k) { return k; }, [](std::uint64_t v) { return v; });
}

TEST(differential_spread_keys_vs_unordered_map) {
    // Chaves espalhadas (múltiplos de 1000) e mais distintas: a tabela cresce várias vezes.
    run_differential<std::uint64_t, std::uint64_t>(
        7, 100000, 30000, [](std::uint64_t k) { return k * 1000; }, [](std::uint64_t v) { return v; });
}

TEST(differential_string_keys_vs_unordered_map) {
    run_differential<std::string, std::string>(
        424242, 30000, 1500, [](std::uint64_t k) { return "sessao-" + std::to_string(k); },
        [](std::uint64_t v) { return std::to_string(v); });
}
