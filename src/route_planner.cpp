#include "route_planner.h"

#include <functional>
#include <limits>
#include <queue>
#include <stack>
#include <stdexcept>
#include <string>
#include <utility>

namespace routes {

RoutePlanner::RoutePlanner(std::size_t intersections)
    : adjacency_(intersections), districts_(intersections) {}

void RoutePlanner::check_vertex(Vertex v) const {
    if (v >= adjacency_.size()) {
        throw std::out_of_range("RoutePlanner: cruzamento " + std::to_string(v) +
                                " não existe (o mapa tem " + std::to_string(adjacency_.size()) +
                                ")");
    }
}

void RoutePlanner::add_road(Vertex a, Vertex b, Micros travel_time) {
    check_vertex(a);
    check_vertex(b);
    if (travel_time < 0) {
        throw std::invalid_argument("RoutePlanner: tempo de travessia negativo");
    }
    const int t = static_cast<int>(travel_time);
    adjacency_[a].push_back({b, t});
    adjacency_[b].push_back({a, t});
    ++roads_;
    districts_.unite(a, b);
}

std::vector<int> RoutePlanner::bfs_hops(Vertex src) const {
    check_vertex(src);
    std::vector<int> hops(size(), -1);
    std::stack<Vertex> pending;
    hops[src] = 0;
    pending.push(src);
    while (!pending.empty()) {
        const Vertex u = pending.top();
        pending.pop();
        for (const Edge& e : adjacency_[u]) {
            if (hops[e.to] == -1) {
                hops[e.to] = hops[u] + 1;
                pending.push(e.to);
            }
        }
    }
    return hops;
}

std::optional<Route> RoutePlanner::shortest_path(Vertex src, Vertex dst) const {
    check_vertex(src);
    check_vertex(dst);

    const int kInfinity = std::numeric_limits<int>::max();
    std::vector<int> best(size(), kInfinity);
    std::vector<Vertex> came_from(size(), src);
    std::vector<bool> settled(size(), false);

    // (tempo acumulado, cruzamento) — o menor tempo fica no topo.
    using Entry = std::pair<int, Vertex>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;

    best[src] = 0;
    settled[src] = true;
    frontier.push({0, src});

    while (!frontier.empty()) {
        const auto [elapsed, u] = frontier.top();
        frontier.pop();
        if (u == dst) {
            break;
        }
        for (const Edge& e : adjacency_[u]) {
            if (settled[e.to]) {
                continue;  // esse cruzamento já tem rota, não precisa olhar de novo
            }
            const int candidate = elapsed + e.travel_us;
            if (candidate < best[e.to]) {
                best[e.to] = candidate;
                came_from[e.to] = u;
                settled[e.to] = true;
                frontier.push({candidate, e.to});
            }
        }
    }

    Route route;
    route.cost = best[dst];
    for (Vertex v = dst; v != src; v = came_from[v]) {
        route.stops.push_back(v);
    }
    return route;
}

bool RoutePlanner::connected(Vertex a, Vertex b) {
    check_vertex(a);
    check_vertex(b);
    return districts_.same(a, b);
}

}  // namespace routes
