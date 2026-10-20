// Teste diferencial: o RoutePlanner contra Floyd–Warshall, BFS e Union-Find de
// referência em centenas de mapas aleatórios pequenos (semente fixa).
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "reference.h"

namespace {

// Mapa aleatório com ruas de tempo zero, retornos e ruas repetidas.
ref::Map random_map(ref::Rng& rng, std::size_t max_n, ref::I64 min_t, ref::I64 max_t) {
    ref::Map m;
    m.n = static_cast<std::size_t>(rng.between(1, static_cast<ref::I64>(max_n)));
    const ref::I64 roads = rng.between(0, static_cast<ref::I64>(3 * m.n));
    for (ref::I64 i = 0; i < roads; ++i) {
        const ref::I64 kind = rng.between(0, 99);
        if (kind < 15 && !m.roads.empty()) {  // rua repetida com outro tempo
            ref::Road r = m.roads[static_cast<std::size_t>(rng.between(0, static_cast<ref::I64>(m.roads.size()) - 1))];
            r.t = rng.between(min_t, max_t);
            m.roads.push_back(r);
            continue;
        }
        const auto a = static_cast<std::size_t>(rng.between(0, static_cast<ref::I64>(m.n) - 1));
        const auto b = kind < 25 ? a : static_cast<std::size_t>(rng.between(0, static_cast<ref::I64>(m.n) - 1));
        const ref::I64 t = rng.between(0, 99) < 20 ? 0 : rng.between(min_t, max_t);
        m.roads.push_back({a, b, t});
    }
    return m;
}

std::string describe(const ref::Map& m, int id) {
    std::ostringstream os;
    os << "[mapa #" << id << ": n=" << m.n << ", ruas:";
    for (std::size_t i = 0; i < m.roads.size() && i < 40; ++i)
        os << ' ' << m.roads[i].a << '-' << m.roads[i].b << ':' << m.roads[i].t;
    if (m.roads.size() > 40) os << " ...";
    return os.str() + "]";
}

void compare_all(const ref::Map& m, int id) {
    routes::RoutePlanner planner = m.build();
    const auto d = ref::floyd_warshall(m);
    const auto w = m.min_road_matrix();
    const ref::Csr g(m);
    auto min_road = [&](std::size_t u, std::size_t v) -> std::optional<ref::I64> {
        if (w[u][v] == ref::kInf) return std::nullopt;
        return w[u][v];
    };
    ref::UnionFind uf(m.n);
    for (const ref::Road& r : m.roads) uf.unite(r.a, r.b);
    CHECK(planner.components() == uf.count(),
          "components() = " << planner.components() << ", esperava " << uf.count() << ' ' << describe(m, id));

    for (std::size_t s = 0; s < m.n; ++s) {
        const std::vector<int> hops = planner.bfs_hops(s);
        const std::vector<int> expected_hops = ref::bfs(g, s);
        for (std::size_t v = 0; v < m.n; ++v) {
            CHECK(hops.size() == m.n && hops[v] == expected_hops[v],
                  "bfs_hops(" << s << ")[" << v << "] = " << (hops.size() == m.n ? hops[v] : -999) << ", esperava "
                              << expected_hops[v] << ' ' << describe(m, id));
        }
        for (std::size_t t = 0; t < m.n; ++t) {
            const bool reach = d[s][t] != ref::kInf;
            CHECK(planner.connected(s, t) == reach, "connected(" << s << ", " << t << ") = " << planner.connected(s, t)
                                                                 << ", esperava " << reach << ' ' << describe(m, id));
            const std::optional<routes::Route> r = planner.shortest_path(s, t);
            if (!reach) {
                CHECK(!r.has_value(), "shortest_path(" << s << ", " << t << ") deveria ser std::nullopt, veio custo "
                                                       << r->cost << ' ' << describe(m, id));
                continue;
            }
            CHECK(r.has_value(), "shortest_path(" << s << ", " << t << ") devolveu std::nullopt, mas há rota de "
                                                  << d[s][t] << " µs " << describe(m, id));
            CHECK(r->cost == d[s][t], "shortest_path(" << s << ", " << t << ") custou " << r->cost
                                                       << " µs; Floyd–Warshall diz " << d[s][t] << " µs "
                                                       << describe(m, id));
            const std::string problem = ref::route_problem(min_road, s, t, *r);
            CHECK(problem.empty(), "shortest_path(" << s << ", " << t << "): " << problem << ' ' << describe(m, id));
        }
    }
}

}  // namespace

TEST(diferencial_floyd, "300 mapas aleatórios: custos, rotas, bfs_hops e bairros batem com a referência") {
    ref::Rng rng(16);
    for (int id = 0; id < 300; ++id) compare_all(random_map(rng, 24, 1, 40), id);
}

TEST(diferencial_tempos_grandes, "60 mapas aleatórios com ruas de 10^9 a 10^12 µs") {
    ref::Rng rng(1616);
    for (int id = 0; id < 60; ++id) compare_all(random_map(rng, 16, 1000000000LL, 1000000000000LL), id);
}

TEST(diferencial_dinamico, "ruas abertas uma a uma: consultas sempre refletem o mapa atual") {
    ref::Rng rng(2016);
    ref::Map m{40, {}};
    routes::RoutePlanner planner(m.n);
    ref::UnionFind uf(m.n);
    for (int step = 0; step < 120; ++step) {
        const auto a = static_cast<std::size_t>(rng.between(0, 39));
        const auto b = static_cast<std::size_t>(rng.between(0, 39));
        const ref::I64 t = rng.between(0, 50);
        m.roads.push_back({a, b, t});
        planner.add_road(a, b, t);
        uf.unite(a, b);
        CHECK(planner.components() == uf.count(), "depois da rua #" << step << " (" << a << '-' << b
                                                                    << "), components() = " << planner.components()
                                                                    << ", esperava " << uf.count());
        const auto d = ref::floyd_warshall(m);
        for (int q = 0; q < 10; ++q) {
            const auto s = static_cast<std::size_t>(rng.between(0, 39));
            const auto e = static_cast<std::size_t>(rng.between(0, 39));
            CHECK(planner.connected(s, e) == uf.same(s, e),
                  "depois da rua #" << step << ", connected(" << s << ", " << e << ") = " << planner.connected(s, e));
            const auto r = planner.shortest_path(s, e);
            if (d[s][e] == ref::kInf) {
                CHECK(!r.has_value(), "depois da rua #" << step << ", shortest_path(" << s << ", " << e
                                                        << ") deveria ser std::nullopt");
            } else {
                CHECK(r.has_value() && r->cost == d[s][e],
                      "depois da rua #" << step << ", shortest_path(" << s << ", " << e << ") custou "
                                        << (r ? r->cost : -1) << " µs, esperava " << d[s][e] << " µs");
            }
        }
    }
}
