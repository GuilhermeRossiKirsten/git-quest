// Desempenho (-O2, sem sanitizers). Os limites são relativos a implementações de
// referência medidas no MESMO processo, então valem em máquinas lentas ou rápidas:
// o código do participante pode ser até ~10x mais lento que a referência.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "check.h"
#include "reference.h"

namespace {

std::string fmt(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f ms", ms);
    return buf;
}

// limite = fator x referência + folga fixa (absorve ruído de medição)
void check_time(const char* what, double mine, double reference, double factor, double slack_ms) {
    const double limit = factor * reference + slack_ms;
    gq::step(std::string(what) + ": " + fmt(mine) + " (referência " + fmt(reference) + ", limite " + fmt(limit) + ")");
    CHECK(mine <= limit, what << " levou " << fmt(mine) << "; a referência levou " << fmt(reference)
                              << " e o limite é " << fmt(limit));
}

ref::Map grid(std::size_t side) {
    ref::Rng rng(1000);
    ref::Map m{side * side, {}};
    m.roads.reserve(2 * side * side);
    for (std::size_t r = 0; r < side; ++r)
        for (std::size_t c = 0; c < side; ++c) {
            const std::size_t v = r * side + c;
            if (c + 1 < side) m.roads.push_back({v, v + 1, rng.between(1, 1000)});
            if (r + 1 < side) m.roads.push_back({v, v + side, rng.between(1, 1000)});
        }
    return m;
}

}  // namespace

TEST(desempenho_grade, "grade 1000x1000: construção, Dijkstra e BFS em tempo de referência") {
    const std::size_t side = 1000;
    const std::size_t n = side * side;
    gq::step("gerando a grade 1000x1000 (~2 milhões de ruas)");
    const ref::Map m = grid(side);

    gq::step("construindo a referência (CSR + Union-Find)");
    std::optional<ref::Csr> g;
    const double ref_build = ref::millis([&] {
        g.emplace(m);
        ref::UnionFind uf(n);
        for (const ref::Road& r : m.roads) uf.unite(r.a, r.b);
    });
    gq::step("construindo o RoutePlanner com add_road");
    std::optional<routes::RoutePlanner> planner;
    const double build = ref::millis([&] {
        planner.emplace(n);
        for (const ref::Road& r : m.roads) planner->add_road(r.a, r.b, r.t);
    });
    check_time("construção (add_road x 2 milhões)", build, ref_build, 20, 1500);
    CHECK(planner->components() == 1, "a grade é um bairro só; components() = " << planner->components());

    const std::size_t pairs[][2] = {{0, n - 1}, {side - 1, n - side}, {n / 2 + side / 2, 0}};
    for (const auto& p : pairs) {
        gq::step("Dijkstra de " + std::to_string(p[0]) + " até " + std::to_string(p[1]));
        ref::I64 expected = 0;
        const double ref_ms = ref::millis([&] { expected = ref::dijkstra(*g, p[0], p[1]); });
        std::optional<routes::Route> r;
        const double ms = ref::millis([&] { r = planner->shortest_path(p[0], p[1]); });
        CHECK(r.has_value(), "shortest_path(" << p[0] << ", " << p[1] << ") devolveu std::nullopt na grade conexa");
        CHECK(r->cost == expected, "shortest_path(" << p[0] << ", " << p[1] << ") custou " << r->cost
                                                    << " µs; o menor custo é " << expected << " µs");
        const std::string problem =
            ref::route_problem([&](std::size_t u, std::size_t v) { return g->min_road(u, v); }, p[0], p[1], *r);
        CHECK(problem.empty(), "shortest_path(" << p[0] << ", " << p[1] << "): " << problem);
        check_time("shortest_path na grade", ms, ref_ms, 10, 300);
    }

    gq::step("BFS a partir do canto");
    std::vector<int> expected_hops;
    const double ref_bfs = ref::millis([&] { expected_hops = ref::bfs(*g, 0); });
    std::vector<int> hops;
    const double bfs = ref::millis([&] { hops = planner->bfs_hops(0); });
    CHECK(hops == expected_hops, "bfs_hops(0) na grade difere da referência (ex.: hops[n-1] = "
                                     << (hops.size() == n ? hops[n - 1] : -999) << ", esperava " << expected_hops[n - 1]
                                     << ")");
    check_time("bfs_hops na grade", bfs, ref_bfs, 10, 300);
}

TEST(desempenho_atalhos_tardios, "cruzamento muito movimentado melhorado 100 mil vezes não trava o Dijkstra") {
    // A central h recebe ruas de k bairros a_i; cada a_i que sai da fila oferece um
    // tempo MELHOR para h. Depois h tem 200 mil ruas para olhar. O destino fica
    // longe, então tudo o que está na fila é processado antes de chegar nele.
    const std::size_t k = 100000;
    const std::size_t leaves = 100000;
    const std::size_t src = 0;
    const std::size_t hub = k + 1;
    const std::size_t dst = k + 2 + leaves;
    ref::Map m{dst + 1, {}};
    for (std::size_t i = 1; i <= k; ++i) {
        m.roads.push_back({src, i, static_cast<ref::I64>(i)});
        m.roads.push_back({i, hub, static_cast<ref::I64>(2 * k - 2 * i)});
    }
    for (std::size_t l = 0; l < leaves; ++l) m.roads.push_back({hub, k + 2 + l, 1});
    m.roads.push_back({hub, dst, static_cast<ref::I64>(2 * k)});

    gq::step("construindo o mapa da central (" + std::to_string(m.roads.size()) + " ruas)");
    const routes::RoutePlanner planner = m.build();
    const ref::Csr g(m);
    gq::step("Dijkstra de referência");
    ref::I64 expected = 0;
    const double ref_ms = ref::millis([&] { expected = ref::dijkstra(g, src, dst); });
    gq::step("shortest_path do participante");
    std::optional<routes::Route> r;
    const double ms = ref::millis([&] { r = planner.shortest_path(src, dst); });
    CHECK(r.has_value(), "shortest_path devolveu std::nullopt, mas o destino é alcançável");
    CHECK(r->cost == expected, "shortest_path custou " << r->cost << " µs; o menor custo é " << expected << " µs");
    CHECK(r->stops.size() == 4, "a melhor rota tem 4 cruzamentos (0, " << k << ", central, destino); veio "
                                                                         << ref::show_stops(r->stops));
    check_time("shortest_path com a central", ms, ref_ms, 10, 300);
}
