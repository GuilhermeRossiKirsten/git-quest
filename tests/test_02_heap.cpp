// Testes de IndexedMinHeap: comportamento observável (sequência de pops, top, priority),
// comparado com um modelo de referência baseado em std::set.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "support/reference_pq.h"
#include "support/rng.h"
#include "../src/indexed_min_heap.h"

namespace {

using Heap = gq::IndexedMinHeap<int, int>;
using Ref = gq::testing::ReferencePQ<int, int>;
using gq::testing::Rng;

// top() do heap e do modelo precisam coincidir (chave e prioridade).
void check_top(const Heap& heap, const Ref& ref, const std::string& ctx) {
    CHECK(heap.size() == ref.size(),
          ctx << ": size() = " << heap.size() << ", esperado " << ref.size());
    if (ref.empty()) {
        CHECK(heap.empty(), ctx << ": o heap deveria estar vazio");
        return;
    }
    CHECK(heap.top_priority() == ref.top_priority() && heap.top_key() == ref.top_key(),
          ctx << ": topo = (chave " << heap.top_key() << ", prioridade " << heap.top_priority()
              << "), esperado (chave " << ref.top_key() << ", prioridade " << ref.top_priority() << ")");
}

// priority(k) consulta o nó pelo mapa de posições: se o mapa estiver dessincronizado,
// a resposta vem de outro nó.
void check_priorities(const Heap& heap, const Ref& ref, const std::string& ctx) {
    ref.for_each([&](int key, int priority) {
        CHECK(heap.contains(key), ctx << ": contains(" << key << ") deveria ser true");
        CHECK(heap.priority(key) == priority,
              ctx << ": priority(" << key << ") = " << heap.priority(key) << ", esperado " << priority
                  << " (a chave aponta para o nó errado?)");
    });
}

// Esvazia os dois e compara pop a pop.
void drain(Heap& heap, Ref& ref, const std::string& ctx) {
    std::size_t n = 0;
    while (!ref.empty()) {
        CHECK(!heap.empty(), ctx << ": o heap esvaziou depois de " << n << " pops; faltavam " << ref.size());
        const auto got = heap.pop();
        const auto want = ref.pop();
        CHECK(got == want, ctx << ": o pop nº " << n + 1 << " devolveu (chave " << got.first << ", prioridade "
                               << got.second << "), esperado (chave " << want.first << ", prioridade "
                               << want.second << ")");
        ++n;
    }
    CHECK(heap.empty() && heap.size() == 0, ctx << ": sobraram " << heap.size() << " elementos no heap");
}

}  // namespace

TEST(heap_basico, "push, top, pop, contains, priority e clear num heap pequeno") {
    Heap h;
    CHECK(h.empty() && h.size() == 0, "heap novo deveria estar vazio");
    CHECK_THROWS(h.top_key(), std::out_of_range, "top_key() em heap vazio");
    CHECK_THROWS(h.top_priority(), std::out_of_range, "top_priority() em heap vazio");
    CHECK_THROWS(h.pop(), std::out_of_range, "pop() em heap vazio");
    CHECK_THROWS(h.priority(7), std::out_of_range, "priority() de chave ausente");
    CHECK(!h.update_priority(7, 1) && !h.erase(7) && !h.contains(7), "operações em chave ausente devolvem false");

    CHECK(h.push(10, 30) && h.push(20, 10) && h.push(30, 20), "push de chaves novas deveria devolver true");
    CHECK(!h.push(20, 99), "push de chave repetida deveria devolver false");
    CHECK(h.priority(20) == 10, "priority(20) = " << h.priority(20) << ", esperado 10 (push repetido não altera nada)");
    CHECK(h.size() == 3 && h.contains(10) && h.contains(30) && !h.contains(40), "size/contains inesperados");
    CHECK(h.top_key() == 20 && h.top_priority() == 10, "topo deveria ser a chave 20 (prioridade 10)");

    const std::pair<int, int> expected[] = {{20, 10}, {30, 20}, {10, 30}};
    for (const auto& e : expected) {
        const auto got = h.pop();
        CHECK(got == e, "pop devolveu (" << got.first << ", " << got.second << "), esperado (" << e.first
                                         << ", " << e.second << ")");
        CHECK(!h.contains(e.first), "a chave " << e.first << " continua no heap depois do pop");
    }
    CHECK(h.empty(), "o heap deveria estar vazio depois de 3 pops");

    h.push(1, 1);
    h.push(2, 2);
    h.clear();
    CHECK(h.empty() && !h.contains(1), "clear() deveria esvaziar o heap");
    CHECK(h.push(1, 5) && h.top_key() == 1, "o heap deveria funcionar depois de clear()");
}

TEST(heap_ordem_pops, "2000 prioridades embaralhadas saem em ordem crescente") {
    Rng rng(15001);
    std::vector<int> priorities(2000);
    for (int i = 0; i < 2000; ++i) {
        priorities[i] = i * 3;
    }
    for (std::size_t i = priorities.size() - 1; i > 0; --i) {  // Fisher-Yates
        std::swap(priorities[i], priorities[rng.below(i + 1)]);
    }
    Heap heap;
    Ref ref;
    for (int key = 0; key < 2000; ++key) {
        heap.push(key, priorities[key]);
        ref.push(key, priorities[key]);
        check_top(heap, ref, "após push(" + std::to_string(key) + ")");
    }
    drain(heap, ref, "esvaziando o heap");
}

TEST(heap_diminuir_prioridade, "update_priority para uma prioridade MENOR (decrease-key)") {
    Heap heap;
    Ref ref;
    for (int key = 0; key < 500; ++key) {
        heap.push(key, 1000 + key);
        ref.push(key, 1000 + key);
    }
    Rng rng(15002);
    for (int round = 0; round < 300; ++round) {
        const int key = static_cast<int>(rng.below(500));
        const int p = ref.priority(key) - 1 - static_cast<int>(rng.below(400));
        CHECK(heap.update_priority(key, p), "update_priority(" << key << ") devolveu false");
        ref.update_priority(key, p);
        check_top(heap, ref, "após diminuir a prioridade da chave " + std::to_string(key));
    }
    check_priorities(heap, ref, "após 300 decrease-key");
    drain(heap, ref, "esvaziando depois de decrease-key");
}

TEST(heap_aumentar_prioridade, "update_priority para uma prioridade MAIOR (increase-key)") {
    Heap small;
    for (int key = 1; key <= 7; ++key) {
        small.push(key, key);
    }
    small.update_priority(1, 100);  // o topo fica "pesado" e precisa afundar
    CHECK(small.top_key() == 2 && small.top_priority() == 2,
          "depois de aumentar a prioridade do topo (1 -> 100), o topo deveria ser a chave 2; é a chave "
              << small.top_key() << " com prioridade " << small.top_priority());

    Heap heap;
    Ref ref;
    for (int key = 0; key < 500; ++key) {
        heap.push(key, key);
        ref.push(key, key);
    }
    Rng rng(15003);
    for (int round = 0; round < 300; ++round) {
        const int key = static_cast<int>(rng.below(500));
        const int p = ref.priority(key) + 1 + static_cast<int>(rng.below(400));
        heap.update_priority(key, p);
        ref.update_priority(key, p);
        check_top(heap, ref, "após aumentar a prioridade da chave " + std::to_string(key));
    }
    check_priorities(heap, ref, "após 300 increase-key");
    drain(heap, ref, "esvaziando depois de increase-key");
}

TEST(heap_posicoes, "chave -> posição continua certa depois de muitas trocas") {
    Heap heap;
    Ref ref;
    Rng rng(15004);
    for (int key = 0; key < 300; ++key) {
        const int p = static_cast<int>(rng.below(100000));
        heap.push(key, p);
        ref.push(key, p);
    }
    check_priorities(heap, ref, "depois de 300 push");
    for (int round = 0; round < 200; ++round) {
        heap.pop();
        ref.pop();
        const int key = 300 + round;
        const int p = static_cast<int>(rng.below(100000));
        heap.push(key, p);
        ref.push(key, p);
        // Mexe numa chave que já trocou de lugar várias vezes.
        const int target = static_cast<int>(rng.below(static_cast<std::uint64_t>(key + 1)));
        if (ref.contains(target)) {
            const int np = static_cast<int>(rng.below(100000));
            heap.update_priority(target, np);
            ref.update_priority(target, np);
        }
        check_priorities(heap, ref, "rodada " + std::to_string(round));
    }
    drain(heap, ref, "esvaziando depois das trocas");
}

TEST(heap_erase, "erase remove de qualquer posição e o heap continua válido") {
    {
        // Remover um nó interno cujo substituto (o último) é menor que o novo pai.
        Heap h;
        Ref r;
        const int priorities[] = {1, 10, 2, 11, 12, 3, 4, 13, 14, 15, 16, 5, 6, 7, 8};
        for (int key = 0; key < 15; ++key) {
            h.push(key, priorities[key]);
            r.push(key, priorities[key]);
        }
        CHECK(h.erase(3) && r.erase(3), "erase(3) deveria devolver true");
        CHECK(!h.contains(3) && !h.erase(3), "a chave 3 continua no heap depois do erase");
        CHECK(!h.erase(99), "erase de chave ausente deveria devolver false");
        check_priorities(h, r, "após erase(3)");
        CHECK(h.erase(1) && r.erase(1), "erase(1) deveria devolver true");
        check_priorities(h, r, "após erase(1)");
        drain(h, r, "pops depois de remover nós internos");
    }

    Heap heap;
    Ref ref;
    Rng rng(15005);
    for (int key = 0; key < 1000; ++key) {
        const int p = static_cast<int>(rng.below(1000000));
        heap.push(key, p);
        ref.push(key, p);
    }
    CHECK(heap.erase(ref.top_key()) && ref.erase(ref.top_key()), "erase do topo deveria devolver true");
    check_top(heap, ref, "após erase do topo");
    for (int round = 0; round < 600; ++round) {
        const int key = static_cast<int>(rng.below(1000));
        const bool want = ref.erase(key);
        const bool got = heap.erase(key);
        CHECK(got == want, "erase(" << key << ") devolveu " << got << ", esperado " << want);
        check_top(heap, ref, "após erase(" + std::to_string(key) + ")");
    }
    check_priorities(heap, ref, "depois de 600 erase");
    drain(heap, ref, "esvaziando depois de 600 erase");
}

TEST(heap_empates_fifo, "empates de prioridade saem em ordem FIFO (push ou último update)") {
    Heap h;
    for (int key = 1; key <= 5; ++key) {
        h.push(key, 7);
    }
    for (int key = 1; key <= 5; ++key) {
        const auto got = h.pop();
        CHECK(got.first == key, "com prioridades iguais, o pop nº " << key << " devolveu a chave " << got.first
                                                                     << "; esperado " << key
                                                                     << " (ordem de inserção)");
    }

    // update_priority conta como nova definição: vai para o fim dos empatados.
    h.push(1, 5);
    h.push(2, 3);
    h.push(3, 5);
    h.push(4, 3);
    h.update_priority(1, 5);  // mesmo valor, mas redefinido: agora sai depois do 3
    h.update_priority(4, 5);  // 4 entra nos empatados com 5, depois do 1
    const int expected[] = {2, 3, 1, 4};
    for (int e : expected) {
        const auto got = h.pop();
        CHECK(got.first == e, "ordem com empates e updates: veio a chave " << got.first << ", esperado " << e
                                                                           << " (esperado 2, 3, 1, 4)");
    }

    // Muitos empates misturados com reorganizações do heap.
    Heap heap;
    Ref ref;
    for (int key = 0; key < 3000; ++key) {
        heap.push(key, key % 4);
        ref.push(key, key % 4);
        if (key % 5 == 4) {
            CHECK(heap.pop() == ref.pop(), "pop intermediário com empates divergiu (após push(" << key << "))");
        }
    }
    drain(heap, ref, "esvaziando um heap cheio de empates");
}

TEST(heap_diferencial, "200 mil operações aleatórias idênticas a um modelo std::set (semente fixa)") {
    Heap heap;
    Ref ref;
    Rng rng(15015);
    const int kOps = 200000;
    for (int op = 0; op < kOps; ++op) {
        const int key = static_cast<int>(rng.below(4096));
        const int p = static_cast<int>(rng.below(64));  // faixa pequena: muitos empates
        const std::uint64_t kind = rng.below(100);
        const std::string ctx = "operação nº " + std::to_string(op + 1);
        if (kind < 35) {
            const bool got = heap.push(key, p);
            CHECK(got == ref.push(key, p), ctx << ": push(" << key << ", " << p << ") devolveu " << got);
        } else if (kind < 55) {
            if (ref.empty()) {
                CHECK_THROWS(heap.pop(), std::out_of_range, ctx << ": pop() em heap vazio");
            } else {
                CHECK(!heap.empty(), ctx << ": o heap está vazio, mas o modelo tem " << ref.size());
                const auto got = heap.pop();
                const auto want = ref.pop();
                CHECK(got == want, ctx << ": pop() devolveu (chave " << got.first << ", prioridade " << got.second
                                       << "), esperado (chave " << want.first << ", prioridade " << want.second
                                       << ")");
            }
        } else if (kind < 75) {
            const bool got = heap.update_priority(key, p);
            CHECK(got == ref.update_priority(key, p),
                  ctx << ": update_priority(" << key << ", " << p << ") devolveu " << got);
        } else if (kind < 88) {
            const bool got = heap.erase(key);
            CHECK(got == ref.erase(key), ctx << ": erase(" << key << ") devolveu " << got);
        } else if (kind < 94) {
            CHECK(heap.contains(key) == ref.contains(key), ctx << ": contains(" << key << ") divergiu");
            if (ref.contains(key)) {
                CHECK(heap.priority(key) == ref.priority(key),
                      ctx << ": priority(" << key << ") = " << heap.priority(key) << ", esperado "
                          << ref.priority(key));
            }
        } else {
            check_top(heap, ref, ctx);
        }
    }
    check_priorities(heap, ref, "ao final das 200 mil operações");
    drain(heap, ref, "esvaziando ao final");
}
