// disjoint_set.h — conjuntos disjuntos (Union-Find) sobre os inteiros 0..n-1.
//
// Usado pelo planejador para responder "estes dois cruzamentos estão no mesmo
// bairro conectado?" à medida que novas ruas são abertas (add_road).
//
// Contrato público (os testes dependem dele):
//   - DisjointSet(n)   cria n conjuntos unitários {0}, {1}, ..., {n-1};
//   - find(x)          devolve o representante do conjunto de x;
//   - unite(a, b)      junta os conjuntos de a e b; devolve true se eles eram
//                      diferentes e false se a e b já estavam juntos;
//   - same(a, b)       true se a e b estão no mesmo conjunto;
//   - count()          quantos conjuntos existem agora;
//   - size()           n.
//   Índices fora de 0..n-1 lançam std::out_of_range.
//
// Desempenho exigido: qualquer sequência de m operações em n elementos deve custar
// O(m · α(n)) amortizado — na prática, constante por operação — mesmo quando as
// uniões chegam numa ordem adversária (por exemplo, formando uma longa fila).
#ifndef ROUTES_DISJOINT_SET_H
#define ROUTES_DISJOINT_SET_H

#include <cstddef>
#include <vector>

namespace routes {

class DisjointSet {
public:
    explicit DisjointSet(std::size_t n);

    std::size_t find(std::size_t x);
    bool unite(std::size_t a, std::size_t b);
    bool same(std::size_t a, std::size_t b);

    std::size_t count() const { return sets_; }
    std::size_t size() const { return parent_.size(); }

private:
    void check(std::size_t x) const;
    std::size_t root_of(std::size_t x);

    std::vector<std::size_t> parent_;
    std::size_t sets_;
};

}  // namespace routes

#endif
