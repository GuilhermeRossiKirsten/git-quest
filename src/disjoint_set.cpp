#include "disjoint_set.h"

#include <numeric>
#include <stdexcept>
#include <string>

namespace routes {

DisjointSet::DisjointSet(std::size_t n) : parent_(n), sets_(n) {
    // Cada elemento começa sendo o representante do próprio conjunto.
    std::iota(parent_.begin(), parent_.end(), std::size_t{0});
}

void DisjointSet::check(std::size_t x) const {
    if (x >= parent_.size()) {
        throw std::out_of_range("DisjointSet: índice " + std::to_string(x) + " fora de [0, " +
                                std::to_string(parent_.size()) + ")");
    }
}

// Sobe pelos pais até achar a raiz (o elemento que é pai de si mesmo).
std::size_t DisjointSet::root_of(std::size_t x) {
    if (parent_[x] == x) {
        return x;
    }
    return root_of(parent_[x]);
}

std::size_t DisjointSet::find(std::size_t x) {
    check(x);
    return root_of(x);
}

bool DisjointSet::unite(std::size_t a, std::size_t b) {
    check(a);
    check(b);
    const std::size_t ra = root_of(a);
    const std::size_t rb = root_of(b);
    // Pendura a árvore de a embaixo da raiz de b.
    parent_[ra] = rb;
    --sets_;
    return ra != rb;
}

bool DisjointSet::same(std::size_t a, std::size_t b) {
    check(a);
    check(b);
    return root_of(a) == root_of(b);
}

}  // namespace routes
