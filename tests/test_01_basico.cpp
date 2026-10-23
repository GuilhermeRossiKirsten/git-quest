// Comportamento funcional numa única thread: FIFO, capacidade exata (inclusive
// capacidades que não são potência de 2), casos de borda e tipos só-movíveis.
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "check.h"
#include "../src/market_feed.h"
#include "../src/spsc_queue.h"

TEST(fifo_basico, "FIFO numa thread: ordem preservada, fila vazia devolve nullopt, size_approx exato") {
    SpscQueue<int> q(8);
    CHECK(q.capacity() == 8, "capacity() deveria ser 8, é " << q.capacity());
    CHECK(q.size_approx() == 0, "fila nova deveria ter tamanho 0, tem " << q.size_approx());
    CHECK(!q.try_pop().has_value(), "try_pop() numa fila vazia deveria devolver std::nullopt");
    for (int i = 1; i <= 5; ++i) {
        CHECK(q.try_push(i), "try_push(" << i << ") falhou numa fila de capacidade 8 com " << i - 1 << " itens");
    }
    CHECK(q.size_approx() == 5, "size_approx() deveria ser 5, é " << q.size_approx());
    for (int i = 1; i <= 5; ++i) {
        std::optional<int> v = q.try_pop();
        CHECK(v.has_value(), "try_pop() devolveu nullopt com " << 6 - i << " itens na fila");
        CHECK(*v == i, "esperava " << i << " (FIFO), veio " << *v);
    }
    CHECK(!q.try_pop().has_value(), "a fila deveria estar vazia depois de tirar tudo");
    CHECK(q.size_approx() == 0, "size_approx() deveria voltar a 0, é " << q.size_approx());
}

namespace {

void check_capacity(std::size_t cap) {
    SpscQueue<std::size_t> q(cap);
    CHECK(q.capacity() == cap, "capacity() deveria devolver " << cap << ", devolveu " << q.capacity());
    // Três voltas completas: enche até o limite, esvazia, e o anel dá a volta.
    std::size_t next_in = 0;
    std::size_t next_out = 0;
    for (int round = 0; round < 3; ++round) {
        for (std::size_t i = 0; i < cap; ++i) {
            CHECK(q.try_push(next_in), "uma fila de capacidade " << cap << " aceitou só " << i
                                                                 << " itens (volta " << round + 1 << ")");
            ++next_in;
        }
        CHECK(!q.try_push(999999), "uma fila de capacidade " << cap << " aceitou um item a mais (" << cap + 1
                                                             << " itens)");
        CHECK(q.size_approx() == cap,
              "cheia, uma fila de capacidade " << cap << " deveria ter size_approx() == " << cap << ", tem "
                                               << q.size_approx());
        for (std::size_t i = 0; i < cap; ++i) {
            std::optional<std::size_t> v = q.try_pop();
            CHECK(v.has_value(), "capacidade " << cap << ": try_pop() devolveu nullopt com " << cap - i
                                               << " itens na fila");
            CHECK(*v == next_out, "capacidade " << cap << ": esperava o item " << next_out << " (FIFO), veio "
                                                << *v);
            ++next_out;
        }
        CHECK(!q.try_pop().has_value(), "capacidade " << cap << ": try_pop() inventou um item numa fila vazia");
    }
    // Alternando push/pop: o índice passa por todas as posições do anel várias vezes.
    for (std::size_t i = 0; i < 5 * cap + 7; ++i) {
        CHECK(q.try_push(next_in), "capacidade " << cap << ": try_push() falhou numa fila vazia");
        std::optional<std::size_t> v = q.try_pop();
        CHECK(v && *v == next_in, "capacidade " << cap << ": alternando push/pop, esperava " << next_in);
        ++next_in;
    }
}

}  // namespace

TEST(capacidade_exata, "aceita exatamente `capacity` itens para 1, 2, 3, 5, 7, 8, 64, 100, 1000 e 1024 (não só potências de 2)") {
    for (std::size_t cap : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5},
                            std::size_t{7}, std::size_t{8}, std::size_t{64}, std::size_t{100}, std::size_t{1000},
                            std::size_t{1024}}) {
        check_capacity(cap);
    }
}

TEST(capacidade_zero, "capacidade 0 é recusada com std::invalid_argument") {
    CHECK_THROWS(SpscQueue<int>(0), std::invalid_argument, "SpscQueue(0) deveria lançar std::invalid_argument");
}

TEST(tipo_so_movivel, "aceita tipos só-movíveis (std::unique_ptr); try_push numa fila cheia não consome o argumento") {
    SpscQueue<std::unique_ptr<int>> q(3);
    for (int i = 0; i < 3; ++i) {
        CHECK(q.try_push(std::make_unique<int>(i)), "try_push(unique_ptr) falhou com a fila não cheia");
    }
    auto extra = std::make_unique<int>(42);
    CHECK(!q.try_push(std::move(extra)), "a fila de capacidade 3 aceitou um 4º item");
    CHECK(extra != nullptr && *extra == 42,
          "try_push numa fila CHEIA moveu (e perdeu) o argumento; ele deveria ficar intacto com quem chamou");
    for (int i = 0; i < 3; ++i) {
        std::optional<std::unique_ptr<int>> v = q.try_pop();
        CHECK(v && *v && **v == i, "esperava o unique_ptr com valor " << i);
    }
}

TEST(emplace_cotacao, "try_emplace constrói a cotação dentro da fila; strings longas sobrevivem intactas") {
    SpscQueue<Quote> q(4);
    const std::string symbol = "PETR4.SA.LOTE-PADRAO.BOVESPA";  // maior que o SSO: aloca no heap
    CHECK(q.try_emplace(Quote{1, symbol, 3712, 100}), "try_emplace falhou numa fila vazia");
    CHECK(q.try_emplace(Quote{2, symbol + "-B", 3713, 200}), "try_emplace falhou numa fila com 1 item");
    std::optional<Quote> a = q.try_pop();
    std::optional<Quote> b = q.try_pop();
    CHECK(a && a->seq == 1 && a->symbol == symbol && a->price_ticks == 3712 && a->quantity == 100,
          "a primeira cotação saiu corrompida");
    CHECK(b && b->seq == 2 && b->symbol == symbol + "-B", "a segunda cotação saiu corrompida");
}
