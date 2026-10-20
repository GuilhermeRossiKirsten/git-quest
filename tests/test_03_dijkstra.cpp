// shortest_path: custos exatos, rotas válidas e casos de borda em mapas feitos à mão.
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.h"
#include "reference.h"

namespace {

using routes::Route;
using routes::Vertex;

// Confere custo e validade da rota; se `stops` não estiver vazio, exige exatamente essa rota.
void expect_route(const ref::Map& m, Vertex src, Vertex dst, ref::I64 cost, const std::vector<Vertex>& stops = {}) {
    const routes::RoutePlanner planner = m.build();
    const std::optional<Route> r = planner.shortest_path(src, dst);
    CHECK(r.has_value(), "shortest_path(" << src << ", " << dst << ") devolveu std::nullopt, mas existe rota de "
                                          << cost << " µs");
    CHECK(r->cost == cost, "shortest_path(" << src << ", " << dst << ") custou " << r->cost
                                            << " µs; o menor custo é " << cost << " µs");
    const auto w = m.min_road_matrix();
    const std::string problem = ref::route_problem(
        [&](std::size_t u, std::size_t v) -> std::optional<ref::I64> {
            if (w[u][v] == ref::kInf) return std::nullopt;
            return w[u][v];
        },
        src, dst, *r);
    CHECK(problem.empty(), "shortest_path(" << src << ", " << dst << "): " << problem);
    if (!stops.empty()) {
        CHECK(r->stops == stops, "shortest_path(" << src << ", " << dst << ") devolveu a rota "
                                                  << ref::show_stops(r->stops) << "; esperava "
                                                  << ref::show_stops(stops));
    }
}

void expect_unreachable(const ref::Map& m, Vertex src, Vertex dst) {
    const routes::RoutePlanner planner = m.build();
    const std::optional<Route> r = planner.shortest_path(src, dst);
    CHECK(!r.has_value(), "shortest_path(" << src << ", " << dst
                                           << ") deveria ser std::nullopt (sem rota), mas veio custo " << r->cost
                                           << " µs e rota " << ref::show_stops(r->stops));
}

}  // namespace

TEST(dijkstra_rota_completa, "a rota lista os cruzamentos de src até dst, na ordem") {
    ref::Map line{4, {{0, 1, 5}, {1, 2, 5}, {2, 3, 5}}};
    expect_route(line, 0, 3, 15, {0, 1, 2, 3});
    expect_route(line, 3, 1, 10, {3, 2, 1});
    expect_route(line, 1, 2, 5, {1, 2});
}

TEST(dijkstra_atalho, "um caminho com mais ruas, porém mais rápido, vence a rua direta") {
    ref::Map triangle{3, {{0, 2, 10}, {0, 1, 1}, {1, 2, 1}}};
    expect_route(triangle, 0, 2, 2, {0, 1, 2});
    // Clássico: o cruzamento 1 é descoberto primeiro por uma rua cara (4) e só depois
    // pelo atalho 0 -> 2 -> 1 (1 + 2); o destino 3 depende do valor final de 1.
    ref::Map classic{4, {{0, 1, 4}, {0, 2, 1}, {2, 1, 2}, {1, 3, 1}}};
    expect_route(classic, 0, 3, 4, {0, 2, 1, 3});
    expect_route(classic, 3, 0, 4, {3, 1, 2, 0});
}

TEST(dijkstra_origem_igual_destino, "src == dst custa 0 e a rota é só {src}") {
    ref::Map m{4, {{0, 1, 3}, {1, 2, 3}, {3, 3, 7}}};
    expect_route(m, 1, 1, 0, {1});
    expect_route(m, 0, 0, 0, {0});
    expect_route(m, 3, 3, 0, {3});  // só um retorno de 7 µs: ficar parado custa 0
    ref::Map lonely{1, {}};
    expect_route(lonely, 0, 0, 0, {0});
}

TEST(dijkstra_inalcancavel, "destino em outro bairro devolve std::nullopt") {
    ref::Map m{6, {{0, 1, 2}, {1, 2, 2}, {3, 4, 2}}};
    expect_unreachable(m, 0, 3);
    expect_unreachable(m, 4, 2);
    expect_unreachable(m, 0, 5);
    expect_unreachable(m, 5, 0);
    expect_route(m, 3, 4, 2, {3, 4});
}

TEST(dijkstra_multiaresta_laco_zero, "ruas repetidas, retornos e ruas de tempo zero") {
    ref::Map m{4, {{0, 1, 50}, {0, 1, 7}, {0, 1, 30}, {1, 1, 0}, {1, 2, 0}, {2, 3, 0}, {0, 3, 8}}};
    expect_route(m, 0, 1, 7, {0, 1});
    expect_route(m, 0, 2, 7, {0, 1, 2});
    expect_route(m, 0, 3, 7, {0, 1, 2, 3});
    expect_route(m, 3, 1, 0, {3, 2, 1});
    ref::Map zeros{5, {{0, 1, 0}, {1, 2, 0}, {2, 3, 0}, {3, 4, 0}, {0, 4, 1}}};
    expect_route(zeros, 0, 4, 0, {0, 1, 2, 3, 4});
}

TEST(dijkstra_tempos_grandes, "custos acima de 2^31 µs (rodovias de mais de 35 min) são exatos") {
    ref::Map highway{4, {{0, 1, 1500000000}, {1, 2, 1500000000}, {2, 3, 1500000000}}};
    expect_route(highway, 0, 3, 4500000000LL, {0, 1, 2, 3});
    ref::Map single{2, {{0, 1, 5000000000LL}}};
    expect_route(single, 1, 0, 5000000000LL, {1, 0});
    ref::Map huge{3, {{0, 1, 1000000000000LL}, {1, 2, 1000000000000LL}, {0, 2, 2000000000001LL}}};
    expect_route(huge, 0, 2, 2000000000000LL, {0, 1, 2});
}

TEST(entradas_invalidas, "cruzamentos inexistentes e tempos negativos lançam exceção sem alterar o mapa") {
    routes::RoutePlanner p(5);
    p.add_road(0, 1, 3);
    CHECK_THROWS(p.add_road(0, 5, 1), std::out_of_range, "add_road com cruzamento 5 num mapa de 5");
    CHECK_THROWS(p.add_road(9, 0, 1), std::out_of_range, "add_road com cruzamento 9 num mapa de 5");
    CHECK_THROWS(p.add_road(0, 2, -1), std::invalid_argument, "add_road com tempo negativo");
    CHECK(p.road_count() == 1, "chamadas inválidas de add_road não podem abrir ruas (road_count = " << p.road_count() << ")");
    CHECK(!p.connected(0, 2), "add_road inválido não pode ligar bairros");
    CHECK(p.components() == 4, "components() deveria ser 4, é " << p.components());
    CHECK_THROWS(p.shortest_path(0, 5), std::out_of_range, "shortest_path com destino inexistente");
    CHECK_THROWS(p.shortest_path(7, 0), std::out_of_range, "shortest_path com origem inexistente");
    CHECK_THROWS(p.bfs_hops(5), std::out_of_range, "bfs_hops com origem inexistente");
    CHECK_THROWS(p.connected(0, 5), std::out_of_range, "connected com cruzamento inexistente");
    routes::DisjointSet ds(3);
    CHECK_THROWS(ds.find(3), std::out_of_range, "DisjointSet::find fora do intervalo");
    CHECK_THROWS(ds.unite(0, 3), std::out_of_range, "DisjointSet::unite fora do intervalo");
    CHECK_THROWS(ds.same(4, 0), std::out_of_range, "DisjointSet::same fora do intervalo");
    CHECK(ds.count() == 3, "operações inválidas não podem alterar count()");
}
