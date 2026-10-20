// Testes unitários de gq::FlatMap: API básica e casos de borda.
#include <cstdint>
#include <string>
#include <vector>

#include "flat_map.hpp"
#include "mini_test.hpp"

namespace {

// Hash degenerado: todas as chaves caem no mesmo slot inicial e formam um único
// cluster, qualquer que seja a mistura de bits que a tabela aplique por cima.
struct ConstantHash {
    std::size_t operator()(int) const { return 42; }
};

using IntMap = gq::FlatMap<int, int>;
using ClusterMap = gq::FlatMap<int, int, ConstantHash>;

}  // namespace

TEST(empty_map_has_no_elements) {
    const IntMap m;
    CHECK(m.empty());
    CHECK_EQ(m.size(), 0u);
    CHECK(m.capacity() >= 8u);
    CHECK(m.find(0) == nullptr);
    CHECK(!m.contains(0));
    std::size_t visited = 0;
    m.for_each([&](const int&, const int&) { ++visited; });
    CHECK_EQ(visited, 0u);
}

TEST(insert_then_find) {
    IntMap m;
    CHECK(m.insert_or_assign(7, 70));
    CHECK(m.insert_or_assign(-3, 30));
    CHECK(m.insert_or_assign(0, 0));
    CHECK_EQ(m.size(), 3u);
    CHECK(!m.empty());
    CHECK(m.find(7) != nullptr);
    CHECK_EQ(*m.find(7), 70);
    CHECK_EQ(*m.find(-3), 30);
    CHECK_EQ(*m.find(0), 0);
    CHECK(m.find(8) == nullptr);
    CHECK(m.contains(-3));
    CHECK(!m.contains(3));
}

TEST(find_returns_mutable_pointer) {
    IntMap m;
    m.insert_or_assign(1, 10);
    *m.find(1) += 5;
    CHECK_EQ(*m.find(1), 15);
    const IntMap& cm = m;
    CHECK_EQ(*cm.find(1), 15);
}

TEST(insert_or_assign_overwrites_without_growing_size) {
    IntMap m;
    CHECK(m.insert_or_assign(1, 10));
    CHECK(!m.insert_or_assign(1, 11));
    CHECK(!m.insert_or_assign(1, 12));
    CHECK_EQ(m.size(), 1u);
    CHECK_EQ(*m.find(1), 12);
    CHECK(m.erase(1));
    CHECK(m.empty());
    CHECK(!m.erase(1));
}

TEST(erase_missing_key_returns_false) {
    IntMap m;
    CHECK(!m.erase(5));
    m.insert_or_assign(1, 1);
    CHECK(!m.erase(5));
    CHECK_EQ(m.size(), 1u);
    CHECK(m.contains(1));
}

TEST(erase_then_reinsert) {
    IntMap m;
    m.insert_or_assign(4, 40);
    CHECK(m.erase(4));
    CHECK(!m.contains(4));
    CHECK(m.insert_or_assign(4, 41));
    CHECK_EQ(*m.find(4), 41);
    CHECK_EQ(m.size(), 1u);
}

TEST(erase_keeps_rest_of_cluster_reachable) {
    ClusterMap m;
    for (int k = 0; k < 6; ++k) {
        m.insert_or_assign(k, k * 10);
    }
    CHECK(m.erase(2));  // no meio do cluster
    CHECK(!m.contains(2));
    for (int k : {0, 1, 3, 4, 5}) {
        CHECK_MSG(m.find(k) != nullptr && *m.find(k) == k * 10,
                  "chave " << k << " sumiu depois de apagar a chave 2 do mesmo cluster");
    }
    CHECK(m.erase(0));  // no começo do cluster
    for (int k : {1, 3, 4, 5}) {
        CHECK_MSG(m.contains(k), "chave " << k << " sumiu depois de apagar a chave 0 do mesmo cluster");
    }
    CHECK_EQ(m.size(), 4u);
    for (int k : {1, 3, 4, 5}) {
        CHECK(m.erase(k));
    }
    CHECK(m.empty());
    for (int k = 0; k < 6; ++k) {
        CHECK(!m.contains(k));
    }
}

TEST(reinsert_after_erase_does_not_duplicate) {
    ClusterMap m;
    m.insert_or_assign(1, 10);
    m.insert_or_assign(2, 20);
    m.insert_or_assign(3, 30);
    CHECK(m.erase(1));
    // A chave 3 continua na tabela, depois do slot que ficou livre.
    CHECK_MSG(!m.insert_or_assign(3, 31), "insert_or_assign(3) disse que a chave era nova, mas ela existia");
    CHECK_EQ(m.size(), 2u);
    CHECK_EQ(*m.find(3), 31);
    std::size_t copies = 0;
    m.for_each([&](const int& k, const int&) { copies += (k == 3); });
    CHECK_MSG(copies == 1, "a chave 3 aparece " << copies << " vezes em for_each");
    CHECK(m.erase(3));
    CHECK_MSG(!m.contains(3), "a chave 3 continua visível depois de apagada (havia uma cópia duplicada)");
    CHECK_EQ(*m.find(2), 20);
}

TEST(erased_keys_stay_erased_after_growth) {
    IntMap m;
    for (int k = 0; k < 6; ++k) {
        m.insert_or_assign(k, k);
    }
    for (int k = 0; k < 3; ++k) {
        CHECK(m.erase(k));
    }
    const std::size_t before = m.capacity();
    for (int k = 100; k < 200; ++k) {
        m.insert_or_assign(k, k);
    }
    CHECK(m.capacity() > before);
    for (int k = 0; k < 3; ++k) {
        CHECK_MSG(!m.contains(k), "a chave apagada " << k << " voltou depois que a tabela cresceu");
    }
    CHECK_EQ(m.size(), 103u);
}

TEST(clear_resets_everything) {
    IntMap m;
    for (int k = 0; k < 100; ++k) {
        m.insert_or_assign(k, k);
    }
    for (int k = 0; k < 50; ++k) {
        m.erase(k);
    }
    m.clear();
    CHECK(m.empty());
    CHECK_EQ(m.size(), 0u);
    for (int k = 0; k < 100; ++k) {
        CHECK(!m.contains(k));
    }
    CHECK(m.insert_or_assign(5, 50));
    CHECK_EQ(m.size(), 1u);
    CHECK_EQ(*m.find(5), 50);
}

TEST(string_keys_and_values) {
    gq::FlatMap<std::string, std::string> m;
    CHECK(m.insert_or_assign("alice", "admin"));
    CHECK(m.insert_or_assign("bob", "dev"));
    CHECK(m.insert_or_assign("", "vazio"));
    CHECK(!m.insert_or_assign("bob", "ops"));
    CHECK_EQ(*m.find("bob"), std::string("ops"));
    CHECK_EQ(*m.find(""), std::string("vazio"));
    CHECK(m.erase("alice"));
    CHECK(m.find("alice") == nullptr);
    CHECK_EQ(m.size(), 2u);
}

TEST(many_keys_all_reachable) {
    gq::FlatMap<std::uint64_t, std::uint64_t> m;
    const std::uint64_t n = 20000;
    for (std::uint64_t k = 0; k < n; ++k) {
        CHECK(m.insert_or_assign(k * 7919, k));
    }
    CHECK_EQ(m.size(), n);
    for (std::uint64_t k = 0; k < n; ++k) {
        const std::uint64_t* v = m.find(k * 7919);
        CHECK_MSG(v != nullptr && *v == k, "chave " << k * 7919 << " não encontrada");
    }
    for (std::uint64_t k = 0; k < n; k += 2) {
        CHECK(m.erase(k * 7919));
    }
    CHECK_EQ(m.size(), n / 2);
    for (std::uint64_t k = 0; k < n; ++k) {
        CHECK_EQ(m.contains(k * 7919), k % 2 == 1);
    }
}
