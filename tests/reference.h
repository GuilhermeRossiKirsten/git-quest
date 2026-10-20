// reference.h — implementações de referência usadas SÓ pelos testes.
//
// São deliberadamente simples (Floyd–Warshall, BFS com fila, Dijkstra com
// "lazy deletion", Union-Find iterativo) e servem de oráculo: os testes comparam
// o RoutePlanner do participante com elas em grafos aleatórios e medem o tempo
// delas na mesma máquina para calibrar os limites de desempenho.
#ifndef GITQUEST_REFERENCE_H
#define GITQUEST_REFERENCE_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../src/route_planner.h"

namespace ref {

using I64 = std::int64_t;
constexpr I64 kInf = std::numeric_limits<I64>::max();

struct Road {
    std::size_t a;
    std::size_t b;
    I64 t;
};

// Mapa descrito por uma lista de ruas: alimenta o RoutePlanner e as referências.
struct Map {
    std::size_t n = 0;
    std::vector<Road> roads;

    routes::RoutePlanner build() const {
        routes::RoutePlanner planner(n);
        for (const Road& r : roads) planner.add_road(r.a, r.b, r.t);
        return planner;
    }

    // Menor tempo entre todas as ruas que ligam u e v (std::nullopt se não há rua).
    std::vector<std::vector<I64>> min_road_matrix() const {
        std::vector<std::vector<I64>> w(n, std::vector<I64>(n, kInf));
        for (const Road& r : roads) {
            w[r.a][r.b] = std::min(w[r.a][r.b], r.t);
            w[r.b][r.a] = std::min(w[r.b][r.a], r.t);
        }
        return w;
    }
};

// Floyd–Warshall: d[i][j] = menor custo de i a j (kInf = inalcançável).
inline std::vector<std::vector<I64>> floyd_warshall(const Map& m) {
    std::vector<std::vector<I64>> d = m.min_road_matrix();
    for (std::size_t i = 0; i < m.n; ++i) d[i][i] = 0;
    for (std::size_t k = 0; k < m.n; ++k)
        for (std::size_t i = 0; i < m.n; ++i) {
            if (d[i][k] == kInf) continue;
            for (std::size_t j = 0; j < m.n; ++j)
                if (d[k][j] != kInf && d[i][k] + d[k][j] < d[i][j]) d[i][j] = d[i][k] + d[k][j];
        }
    return d;
}

// Grafo compacto (CSR) para as referências rápidas dos testes de desempenho.
struct Csr {
    std::vector<std::size_t> start;
    std::vector<std::size_t> to;
    std::vector<I64> w;

    explicit Csr(const Map& m) : start(m.n + 1, 0), to(2 * m.roads.size()), w(2 * m.roads.size()) {
        for (const Road& r : m.roads) {
            ++start[r.a + 1];
            ++start[r.b + 1];
        }
        std::partial_sum(start.begin(), start.end(), start.begin());
        std::vector<std::size_t> fill(start.begin(), start.end() - 1);
        for (const Road& r : m.roads) {
            to[fill[r.a]] = r.b;
            w[fill[r.a]++] = r.t;
            to[fill[r.b]] = r.a;
            w[fill[r.b]++] = r.t;
        }
    }
    std::size_t size() const { return start.size() - 1; }

    std::optional<I64> min_road(std::size_t u, std::size_t v) const {
        std::optional<I64> best;
        for (std::size_t i = start[u]; i < start[u + 1]; ++i)
            if (to[i] == v && (!best || w[i] < *best)) best = w[i];
        return best;
    }
};

inline std::vector<int> bfs(const Csr& g, std::size_t src) {
    std::vector<int> hops(g.size(), -1);
    std::queue<std::size_t> q;
    hops[src] = 0;
    q.push(src);
    while (!q.empty()) {
        const std::size_t u = q.front();
        q.pop();
        for (std::size_t i = g.start[u]; i < g.start[u + 1]; ++i)
            if (hops[g.to[i]] == -1) {
                hops[g.to[i]] = hops[u] + 1;
                q.push(g.to[i]);
            }
    }
    return hops;
}

inline I64 dijkstra(const Csr& g, std::size_t src, std::size_t dst) {
    std::vector<I64> d(g.size(), kInf);
    using Entry = std::pair<I64, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    d[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        const auto [du, u] = pq.top();
        pq.pop();
        if (du != d[u]) continue;
        if (u == dst) break;
        for (std::size_t i = g.start[u]; i < g.start[u + 1]; ++i)
            if (du + g.w[i] < d[g.to[i]]) {
                d[g.to[i]] = du + g.w[i];
                pq.push({d[g.to[i]], g.to[i]});
            }
    }
    return d[dst];
}

// Union-Find iterativo com compressão de caminho e união por tamanho.
class UnionFind {
public:
    explicit UnionFind(std::size_t n) : parent_(n), size_(n, 1), sets_(n) {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }
    std::size_t find(std::size_t x) {
        std::size_t r = x;
        while (parent_[r] != r) r = parent_[r];
        while (parent_[x] != r) {
            const std::size_t next = parent_[x];
            parent_[x] = r;
            x = next;
        }
        return r;
    }
    bool unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        if (size_[a] > size_[b]) std::swap(a, b);
        parent_[a] = b;
        size_[b] += size_[a];
        --sets_;
        return true;
    }
    bool same(std::size_t a, std::size_t b) { return find(a) == find(b); }
    std::size_t count() const { return sets_; }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::size_t> size_;
    std::size_t sets_;
};

inline std::string show_stops(const std::vector<routes::Vertex>& stops) {
    std::ostringstream os;
    os << '{';
    const std::size_t shown = std::min<std::size_t>(stops.size(), 12);
    for (std::size_t i = 0; i < shown; ++i) os << (i ? ", " : "") << stops[i];
    if (shown < stops.size()) os << ", ... (" << stops.size() << " cruzamentos)";
    os << '}';
    return os.str();
}

// Confere uma rota: começa em src, termina em dst, cada passo usa uma rua que
// existe e a soma dos tempos (pela rua mais rápida de cada passo) é o custo
// anunciado. Devolve "" se estiver tudo certo, ou a descrição do problema.
inline std::string route_problem(const std::function<std::optional<I64>(std::size_t, std::size_t)>& min_road,
                                 std::size_t src, std::size_t dst, const routes::Route& r) {
    std::ostringstream os;
    if (r.stops.empty()) return "a rota não tem nenhum cruzamento (deveria começar em src e terminar em dst)";
    if (r.stops.front() != src) {
        os << "a rota deveria começar em " << src << ", mas começa em " << r.stops.front()
           << ": " << show_stops(r.stops);
        return os.str();
    }
    if (r.stops.back() != dst) {
        os << "a rota deveria terminar em " << dst << ", mas termina em " << r.stops.back() << ": "
           << show_stops(r.stops);
        return os.str();
    }
    I64 sum = 0;
    for (std::size_t i = 0; i + 1 < r.stops.size(); ++i) {
        const auto t = min_road(r.stops[i], r.stops[i + 1]);
        if (!t) {
            os << "a rota usa a rua " << r.stops[i] << " -> " << r.stops[i + 1]
               << ", que não existe: " << show_stops(r.stops);
            return os.str();
        }
        sum += *t;
    }
    if (sum != r.cost) {
        os << "o custo anunciado é " << r.cost << " µs, mas as ruas da rota somam " << sum << " µs";
        return os.str();
    }
    return "";
}

template <typename F>
double millis(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Gerador determinístico (SplitMix64): mesma sequência em qualquer plataforma.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (s_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    // inteiro uniforme em [lo, hi]
    I64 between(I64 lo, I64 hi) { return lo + static_cast<I64>(next() % static_cast<std::uint64_t>(hi - lo + 1)); }

private:
    std::uint64_t s_;
};

}  // namespace ref

#endif
