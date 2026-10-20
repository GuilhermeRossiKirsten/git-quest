// bfs_hops: menor número de ruas entre src e cada cruzamento (pesos ignorados).
#include <string>
#include <vector>

#include "check.h"
#include "reference.h"

namespace {

std::string show(const std::vector<int>& v) {
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "}";
}

void expect_hops(const ref::Map& m, routes::Vertex src, const std::vector<int>& expected) {
    const routes::RoutePlanner planner = m.build();
    const std::vector<int> got = planner.bfs_hops(src);
    CHECK(got == expected, "bfs_hops(" << src << ") devolveu " << show(got) << "; esperava " << show(expected));
}

}  // namespace

TEST(bfs_linha, "bfs_hops numa rua reta conta as ruas e marca -1 no que é inalcançável") {
    ref::Map m{6, {{0, 1, 5}, {1, 2, 5}, {2, 3, 5}, {3, 4, 5}}};
    expect_hops(m, 0, {0, 1, 2, 3, 4, -1});
    expect_hops(m, 2, {2, 1, 0, 1, 2, -1});
    expect_hops(m, 5, {-1, -1, -1, -1, -1, 0});
}

TEST(bfs_ignora_pesos, "bfs_hops conta ruas, não tempo: uma rua lenta direta vale 1") {
    ref::Map m{4, {{0, 1, 1000000}, {0, 2, 1}, {2, 3, 1}, {3, 1, 1}}};
    expect_hops(m, 0, {0, 1, 1, 2});
}

TEST(bfs_ordem_das_ruas, "bfs_hops não depende da ordem em que as ruas foram abertas") {
    // 0 -> 2 -> 4 tem 2 ruas; 0 -> 1 -> 3 -> 4 tem 3.
    ref::Map m{5, {{0, 2, 1}, {0, 1, 1}, {1, 3, 1}, {3, 4, 1}, {2, 4, 1}}};
    expect_hops(m, 0, {0, 1, 1, 2, 2});
    ref::Map reversed{5, {{2, 4, 1}, {3, 4, 1}, {1, 3, 1}, {0, 1, 1}, {0, 2, 1}}};
    expect_hops(reversed, 0, {0, 1, 1, 2, 2});
}

TEST(bfs_grade, "bfs_hops numa grade 30x30 dá a distância de Manhattan") {
    const std::size_t side = 30;
    ref::Map m{side * side, {}};
    for (std::size_t r = 0; r < side; ++r)
        for (std::size_t c = 0; c < side; ++c) {
            if (c + 1 < side) m.roads.push_back({r * side + c, r * side + c + 1, 7});
            if (r + 1 < side) m.roads.push_back({r * side + c, (r + 1) * side + c, 7});
        }
    const routes::RoutePlanner planner = m.build();
    for (std::size_t src : {std::size_t{0}, side * side / 2 + side / 2, side * side - 1}) {
        const std::vector<int> hops = planner.bfs_hops(src);
        CHECK(hops.size() == side * side, "bfs_hops devolveu " << hops.size() << " posições");
        for (std::size_t v = 0; v < side * side; ++v) {
            const int expected = static_cast<int>((src / side > v / side ? src / side - v / side : v / side - src / side) +
                                                  (src % side > v % side ? src % side - v % side : v % side - src % side));
            CHECK(hops[v] == expected, "bfs_hops(" << src << ")[" << v << "] = " << hops[v] << "; esperava " << expected
                                                   << " (distância de Manhattan na grade)");
        }
    }
}
