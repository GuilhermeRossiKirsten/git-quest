// Union-Find (DisjointSet) e as consultas connected/components do planejador.
#include <cstddef>

#include "check.h"
#include "reference.h"

TEST(uf_basico, "DisjointSet une, compara e conta conjuntos") {
    routes::DisjointSet ds(6);
    CHECK(ds.size() == 6 && ds.count() == 6, "6 elementos deveriam começar em 6 conjuntos (count = " << ds.count() << ")");
    CHECK(ds.unite(0, 1), "unite(0, 1) juntou dois conjuntos diferentes e deveria devolver true");
    CHECK(!ds.unite(1, 0), "unite(1, 0) logo depois de unite(0, 1) deveria devolver false");
    CHECK(ds.count() == 5, "depois de uma união efetiva, count() deveria ser 5, é " << ds.count());
    CHECK(ds.unite(2, 3) && ds.unite(1, 3), "unite de conjuntos diferentes deveria devolver true");
    CHECK(ds.count() == 3, "esperava 3 conjuntos ({0,1,2,3}, {4}, {5}), há " << ds.count());
    CHECK(!ds.unite(4, 4), "unite(4, 4) não junta nada e deveria devolver false");
    CHECK(!ds.unite(0, 2), "unite(0, 2) com 0 e 2 já juntos deveria devolver false");
    CHECK(ds.count() == 3, "uniões redundantes não podem mudar count() (é " << ds.count() << ", esperava 3)");
    CHECK(ds.same(0, 2) && ds.same(3, 1) && !ds.same(0, 4) && !ds.same(4, 5), "same() não reflete as uniões feitas");
    CHECK(ds.find(0) == ds.find(3) && ds.find(4) != ds.find(5),
          "find() deveria dar o mesmo representante ao mesmo conjunto");
}

TEST(uf_bairros_com_ciclos, "components() não se confunde com ciclos, ruas repetidas e retornos") {
    routes::RoutePlanner p(7);
    p.add_road(0, 1, 1);
    p.add_road(1, 2, 1);
    p.add_road(2, 0, 1);  // ciclo
    p.add_road(3, 4, 1);
    p.add_road(3, 4, 2);  // rua repetida
    p.add_road(4, 3, 3);
    p.add_road(5, 5, 9);  // retorno
    CHECK(p.road_count() == 7, "road_count() deveria ser 7, é " << p.road_count());
    CHECK(p.components() == 4,
          "o mapa tem 4 bairros ({0,1,2}, {3,4}, {5}, {6}), mas components() = " << p.components());
    CHECK(p.connected(2, 1) && p.connected(4, 3) && p.connected(5, 5) && p.connected(6, 6),
          "connected() falhou em bairro conexo");
    CHECK(!p.connected(0, 3) && !p.connected(5, 6) && !p.connected(4, 6), "connected() ligou bairros separados");
}

TEST(uf_dinamico, "connected() acompanha a abertura de novas ruas") {
    routes::RoutePlanner p(8);
    CHECK(p.components() == 8, "mapa sem ruas deveria ter 8 bairros, components() = " << p.components());
    CHECK(!p.connected(0, 7), "sem ruas, 0 e 7 não podem estar ligados");
    p.add_road(0, 1, 4);
    p.add_road(6, 7, 4);
    CHECK(!p.connected(1, 6) && p.connected(1, 0) && p.connected(7, 6), "connected() errado após as primeiras ruas");
    p.add_road(2, 3, 4);
    p.add_road(3, 6, 4);
    CHECK(p.connected(2, 7) && !p.connected(0, 7), "connected() errado depois de ligar {2,3} a {6,7}");
    CHECK(p.components() == 4,
          "esperava 4 bairros ({0,1}, {2,3,6,7}, {4}, {5}), components() = " << p.components());
    p.add_road(1, 2, 4);
    CHECK(p.connected(0, 7) && p.connected(7, 0), "depois de 1-2, 0 e 7 deveriam estar ligados (nos dois sentidos)");
    CHECK(p.components() == 3, "esperava 3 bairros, components() = " << p.components());
    p.add_road(0, 7, 4);  // fecha um ciclo: nada muda
    CHECK(p.components() == 3, "fechar um ciclo não muda o número de bairros, components() = " << p.components());
}

TEST(uf_fila_longa, "uma fila de 1 milhão de uniões não estoura a pilha (ASan)") {
    const std::size_t n = 1000000;
    routes::DisjointSet ds(n);
    gq::step("unindo i com i+1 para i = 0 .. n-2 (forma uma fila)");
    for (std::size_t i = 0; i + 1 < n; ++i) ds.unite(i, i + 1);
    CHECK(ds.count() == 1, "depois de unir todos em fila deveria sobrar 1 conjunto, há " << ds.count());
    gq::step("consultando same(0, n-1) e find(0)");
    CHECK(ds.same(0, n - 1), "0 e n-1 deveriam estar no mesmo conjunto");
    CHECK(ds.find(0) == ds.find(n - 1), "find(0) e find(n-1) deveriam coincidir");
    gq::step("consultando pares das duas pontas");
    for (std::size_t i = 0; i < 1000; ++i) {
        CHECK(ds.same(i, n - 1 - i), "same(" << i << ", " << n - 1 - i << ") falhou");
    }
}
