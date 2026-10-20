// Desempenho do DisjointSet (-O2, sem sanitizers) contra sequências adversárias.
// O limite é relativo a um Union-Find de referência medido no mesmo processo.
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "reference.h"

namespace {

// Executa a mesma sequência de operações em qualquer DS com find/unite/same/count
// e devolve um "resumo" (contagens e respostas) para comparar com a referência.
template <typename DS>
std::vector<std::size_t> workload(std::size_t n, bool announce) {
    auto say = [&](const char* s) {
        if (announce) gq::step(s);
    };
    std::vector<std::size_t> digest;
    ref::Rng rng(77);
    {
        say("fase 1: fila — unite(i, i+1) e depois 1 milhão de consultas same(0, x)");
        DS ds(n);
        for (std::size_t i = 0; i + 1 < n; ++i) ds.unite(i, i + 1);
        std::size_t yes = 0;
        for (std::size_t q = 0; q < n; ++q) yes += ds.same(0, static_cast<std::size_t>(rng.next() % n));
        digest.push_back(ds.count());
        digest.push_back(yes);
    }
    {
        say("fase 2: estrela invertida — unite(0, i) para todo i");
        DS ds(n);
        for (std::size_t i = 1; i < n; ++i) ds.unite(0, i);
        digest.push_back(ds.count());
        digest.push_back(ds.same(n - 1, n / 2));
    }
    {
        say("fase 3: 1 milhão de operações aleatórias misturadas");
        DS ds(n);
        std::size_t yes = 0;
        for (std::size_t q = 0; q < n; ++q) {
            const auto a = static_cast<std::size_t>(rng.next() % n);
            const auto b = static_cast<std::size_t>(rng.next() % n);
            if (q % 3 == 2) {
                yes += ds.same(a, b);
            } else {
                ds.unite(a, b);
            }
        }
        digest.push_back(ds.count());
        digest.push_back(yes);
    }
    return digest;
}

std::string fmt(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f ms", ms);
    return buf;
}

}  // namespace

TEST(desempenho_union_find, "3 milhões de operações adversárias de Union-Find em tempo de referência") {
    const std::size_t n = 1000000;
    gq::step("medindo o Union-Find de referência");
    std::vector<std::size_t> expected;
    const double ref_ms = ref::millis([&] { expected = workload<ref::UnionFind>(n, false); });
    std::vector<std::size_t> got;
    const double ms = ref::millis([&] { got = workload<routes::DisjointSet>(n, true); });
    CHECK(got == expected, "as respostas do DisjointSet (contagens/same) diferem das da referência: count() da fila = "
                               << got[0] << " (esperava " << expected[0] << "), count() aleatório = " << got[4]
                               << " (esperava " << expected[4] << ")");
    const double limit = 10 * ref_ms + 300;
    gq::step("DisjointSet: " + fmt(ms) + " (referência " + fmt(ref_ms) + ", limite " + fmt(limit) + ")");
    CHECK(ms <= limit, "o DisjointSet levou " << fmt(ms) << "; a referência levou " << fmt(ref_ms)
                                              << " e o limite é " << fmt(limit));
}
