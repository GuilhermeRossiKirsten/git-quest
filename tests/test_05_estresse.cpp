// Estresse com duas threads de verdade: um produtor e um consumidor trocando
// milhões de itens. O consumidor confere a ORDEM item a item e um checksum no
// fim. No build [tsan] o ThreadSanitizer acusa qualquer acesso ao slot que não
// esteja ordenado por acquire/release (ele não depende da sorte do agendamento:
// sem relação happens-before entre escrever e ler o slot, há relato).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "support/tracked.h"
#include "../src/market_feed.h"
#include "../src/spsc_queue.h"

namespace {

// Espera ativa educada: o container tem 2 CPUs e o TSan deixa tudo mais lento.
void backoff(unsigned& spins) {
    if (++spins % 64 == 0) {
        std::this_thread::yield();
    }
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

}  // namespace

TEST(estresse_sequencia, "2 threads, milhões de inteiros por uma fila de capacidade 1000: ordem exata e checksum") {
    const std::uint64_t total = gq::kThreadSanitizer ? 1'000'000 : 4'000'000;
    SpscQueue<std::uint64_t> q(1000);
    std::uint64_t expected_hash = 0;
    for (std::uint64_t i = 0; i < total; ++i) {
        expected_hash = mix(expected_hash, i * 2654435761ULL);
    }

    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> first_bad{~std::uint64_t{0}};
    std::atomic<std::uint64_t> bad_value{0};
    std::uint64_t hash = 0;
    std::thread consumer([&] {
        unsigned spins = 0;
        for (std::uint64_t i = 0; i < total;) {
            std::optional<std::uint64_t> v = q.try_pop();
            if (!v) {
                backoff(spins);
                continue;
            }
            if (*v != i * 2654435761ULL && first_bad.load() == ~std::uint64_t{0}) {
                first_bad = i;
                bad_value = *v;
            }
            hash = mix(hash, *v);
            ++i;
            received.store(i, std::memory_order_relaxed);
        }
    });
    gq::step("produtor empurrando itens (o consumidor pode ter parado de receber)");
    unsigned spins = 0;
    for (std::uint64_t i = 0; i < total; ++i) {
        while (!q.try_push(i * 2654435761ULL)) {
            backoff(spins);
        }
    }
    gq::step("esperando o consumidor receber todos os itens");
    consumer.join();
    CHECK(first_bad.load() == ~std::uint64_t{0},
          "o " << first_bad.load() << "º item recebido não é o que foi enviado nessa posição (veio " << bad_value.load()
               << ", esperava " << first_bad.load() * 2654435761ULL << "): ordem quebrada ou item corrompido");
    CHECK(hash == expected_hash, "checksum diferente: algum item se perdeu, duplicou ou foi corrompido");
    CHECK(q.size_approx() == 0, "a fila deveria terminar vazia, tem " << q.size_approx());
    gq::note(std::to_string(received.load()) + " itens conferidos");
}

TEST(estresse_objetos, "2 threads trocando objetos não triviais: nenhum perdido, duplicado ou destruído duas vezes") {
    const std::uint64_t total = gq::kThreadSanitizer ? 200'000 : 1'000'000;
    Tracked::reset();
    {
        SpscQueue<Tracked> q(48);
        std::atomic<bool> order_ok{true};
        std::thread consumer([&] {
            unsigned spins = 0;
            for (std::uint64_t i = 0; i < total;) {
                std::optional<Tracked> v = q.try_pop();
                if (!v) {
                    backoff(spins);
                    continue;
                }
                if (v->value() != i) {
                    order_ok = false;
                }
                ++i;
            }
        });
        gq::step("produtor empurrando objetos (try_emplace devolve sempre false?)");
        unsigned spins = 0;
        for (std::uint64_t i = 0; i < total; ++i) {
            while (!q.try_emplace(i)) {
                backoff(spins);
            }
        }
        gq::step("esperando o consumidor receber todos os objetos");
        consumer.join();
        gq::step("fim do teste");
        CHECK(order_ok.load(), "algum objeto chegou fora de ordem ou com valor errado");
        // Sobram itens na fila para o destrutor cuidar.
        for (std::uint64_t i = 0; i < 20; ++i) {
            CHECK(q.try_emplace(total + i), "try_emplace falhou numa fila vazia de capacidade 48");
        }
    }
    CHECK(Tracked::bad_destructions.load() == 0,
          Tracked::bad_destructions.load() << " destruição(ões) dupla(s) ou de memória nunca construída");
    CHECK(Tracked::live.load() == 0, Tracked::live.load() << " objeto(s) continuam vivos depois que a fila morreu");
}

TEST(feed_mercado, "replay do feed de mercado: todas as cotações chegam à estratégia, em ordem e intactas") {
    const std::uint64_t total = gq::kThreadSanitizer ? 60'000 : 300'000;
    std::vector<Quote> quotes;
    quotes.reserve(total);
    const char* symbols[] = {"PETR4.SA.LOTE-PADRAO", "VALE3.SA.LOTE-PADRAO", "ITUB4.SA.LOTE-PADRAO",
                             "WINZ25.BMF.MINI-INDICE"};
    for (std::uint64_t i = 0; i < total; ++i) {
        quotes.push_back(Quote{i + 1, std::string(symbols[i % 4]) + "#" + std::to_string(i),
                               static_cast<std::int64_t>(1000 + i % 97), static_cast<std::uint32_t>(i % 500)});
    }
    std::uint64_t expected_seq = 1;
    std::uint64_t corrupted = 0;
    gq::step("replay_feed (a fila pode ter travado)");
    const FeedStats stats = replay_feed(quotes, 500, [&](const Quote& q) {
        const Quote& sent = quotes[expected_seq - 1];
        if (q.seq != expected_seq || q.symbol != sent.symbol || q.price_ticks != sent.price_ticks ||
            q.quantity != sent.quantity) {
            ++corrupted;
        }
        ++expected_seq;
    });
    CHECK(stats.delivered == total, "o feed entregou " << stats.delivered << " de " << total << " cotações");
    CHECK(corrupted == 0, corrupted << " cotações chegaram fora de ordem ou com campos corrompidos");
    gq::note("feed: " + std::to_string(stats.delivered) + " cotações, produtor encontrou a fila cheia " +
             std::to_string(stats.producer_stalls) + " vezes");
}
