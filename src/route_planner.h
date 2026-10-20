// route_planner.h — planejador de rotas de entrega sobre o mapa de ruas da cidade.
//
// O mapa é um grafo NÃO direcionado: cruzamentos são vértices 0..n-1 e cada rua
// (add_road) liga dois cruzamentos nos dois sentidos, com um tempo de travessia em
// MICROSSEGUNDOS (inteiro >= 0). Podem existir várias ruas entre o mesmo par de
// cruzamentos (vale a mais rápida), ruas de tempo zero e "retornos" que saem e
// chegam no mesmo cruzamento.
//
// Contrato público (os testes dependem dele):
//   - add_road(a, b, t)        abre uma rua; t < 0 lança std::invalid_argument;
//   - bfs_hops(src)            para cada cruzamento, o MENOR número de ruas desde src
//                              (ignora os tempos); -1 se for inalcançável;
//   - shortest_path(src, dst)  rota de menor tempo total: custo exato em µs e a lista
//                              de cruzamentos visitados, começando em src e terminando
//                              em dst (src == dst → custo 0 e a lista {src});
//                              std::nullopt se dst for inalcançável a partir de src;
//   - connected(a, b)          a e b estão ligados por alguma sequência de ruas?
//   - components()             quantos "bairros" (componentes conexos) o mapa tem.
//   Cruzamentos fora de 0..n-1 lançam std::out_of_range em todas as operações.
//
// Escala: mapas com milhões de cruzamentos e ruas; tempos de rua de até 10^12 µs e
// custos totais que cabem em std::int64_t. Consultas de rota devem custar
// O((V + E) log V); connected/components, praticamente O(1) amortizado.
#ifndef ROUTES_ROUTE_PLANNER_H
#define ROUTES_ROUTE_PLANNER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "disjoint_set.h"

namespace routes {

using Vertex = std::size_t;
using Micros = std::int64_t;  // tempo de travessia em microssegundos

struct Route {
    Micros cost = 0;
    std::vector<Vertex> stops;  // src, ..., dst
};

class RoutePlanner {
public:
    explicit RoutePlanner(std::size_t intersections);

    std::size_t size() const { return adjacency_.size(); }
    std::size_t road_count() const { return roads_; }

    void add_road(Vertex a, Vertex b, Micros travel_time);

    std::vector<int> bfs_hops(Vertex src) const;
    std::optional<Route> shortest_path(Vertex src, Vertex dst) const;

    bool connected(Vertex a, Vertex b);
    std::size_t components() const { return districts_.count(); }

private:
    struct Edge {
        Vertex to;
        int travel_us;  // µs; um trecho de rua da cidade leva poucos minutos
    };

    void check_vertex(Vertex v) const;

    std::vector<std::vector<Edge>> adjacency_;
    std::size_t roads_ = 0;
    DisjointSet districts_;
};

}  // namespace routes

#endif
