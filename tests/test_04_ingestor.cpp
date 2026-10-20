// Testes de Ingestor: entrega exatamente uma vez, shutdown rápido, destrutor que
// interrompe, exceção no construtor e submit concorrente com shutdown.
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "../src/ingestor.h"

using namespace std::chrono_literals;

namespace {

constexpr auto kLongHeartbeat = 3000ms;

// Conta quantas vezes cada id passou pelo handler.
struct Tally {
    explicit Tally(std::size_t n) : seen(n) {}
    Ingestor::Handler handler() {
        return [this](const Record& r) {
            if (r.id >= seen.size()) {
                ++out_of_range;
                return;
            }
            seen[r.id].fetch_add(1);
            ++calls;
        };
    }
    std::vector<std::atomic<int>> seen;
    std::atomic<std::size_t> calls{0};
    std::atomic<int> out_of_range{0};
};

Record make_record(std::uint64_t id) {
    return Record{id, "payload-" + std::to_string(id)};
}

}  // namespace

TEST(ingestor_exatamente_uma_vez, "shutdown() processa todos os 12000 registros aceitos, cada um exatamente uma vez") {
    const std::size_t baseline = gq::thread_count();
    constexpr std::size_t kProducers = 3;
    constexpr std::size_t kPerProducer = 4000;
    constexpr std::size_t kTotal = kProducers * kPerProducer;
    Tally tally(kTotal);
    {
        Ingestor ing(4, 50ms, tally.handler());
        {
            std::vector<std::jthread> producers;
            for (std::size_t p = 0; p < kProducers; ++p) {
                producers.emplace_back([&ing, p] {
                    for (std::size_t i = 0; i < kPerProducer; ++i) {
                        CHECK(ing.submit(make_record(p * kPerProducer + i)),
                              "submit() recusou um registro antes do shutdown()");
                    }
                });
            }
        }
        gq::step("shutdown() com 12000 registros enviados");
        ing.shutdown();
        CHECK(ing.processed() == kTotal, "processed() = " << ing.processed() << ", esperava " << kTotal);
        CHECK(!ing.submit(make_record(0)), "submit() depois de shutdown() deveria devolver false");
    }
    CHECK(tally.out_of_range.load() == 0, "o handler recebeu ids que nunca foram enviados");
    std::size_t lost = 0, dup = 0;
    for (std::size_t i = 0; i < kTotal; ++i) {
        const int n = tally.seen[i].load();
        lost += (n == 0);
        dup += (n > 1);
    }
    CHECK(lost == 0 && dup == 0, lost << " registro(s) aceitos nunca foram processados e " << dup
                                      << " foram processados mais de uma vez");
    CHECK_NO_THREADS_LEFT(baseline, "depois de destruir o Ingestor");
}

TEST(ingestor_shutdown_rapido, "shutdown() com workers ociosos e heartbeat de 3 s termina em < 200 ms") {
    const std::size_t baseline = gq::thread_count();
    Tally tally(16);
    Ingestor ing(3, kLongHeartbeat, tally.handler());
    for (std::uint64_t i = 0; i < 16; ++i) {
        ing.submit(make_record(i));
    }
    CHECK(gq::wait_until([&] { return ing.processed() == 16; }, 2s), "os 16 registros não foram processados em 2 s");
    std::this_thread::sleep_for(50ms);  // workers bloqueados na fila vazia, heartbeat dormindo
    gq::step("shutdown() com workers ociosos e heartbeat de 3 s");
    const double ms = gq::elapsed_ms([&] { ing.shutdown(); });
    CHECK(ms < 200.0, "shutdown() levou " << static_cast<long>(ms) << " ms (limite: 200 ms)");
    CHECK_NO_THREADS_LEFT(baseline, "logo depois de shutdown() (antes do destrutor)");
    gq::step("segundo shutdown()");
    const double again = gq::elapsed_ms([&] { ing.shutdown(); });
    CHECK(again < 50.0, "o segundo shutdown() levou " << static_cast<long>(again) << " ms (deveria ser imediato)");
}

TEST(ingestor_destrutor_interrompe, "destrutor sem shutdown(): < 200 ms, descarta a fila, nenhum handler depois dele") {
    const std::size_t baseline = gq::thread_count();
    constexpr std::size_t kTotal = 3000;
    Tally tally(kTotal);
    auto slow_handler = [h = tally.handler()](const Record& r) {
        h(r);
        std::this_thread::sleep_for(2ms);  // 3000 registros levariam ~2 s para esvaziar
    };
    auto ing = std::make_unique<Ingestor>(3, kLongHeartbeat, slow_handler);
    for (std::uint64_t i = 0; i < kTotal; ++i) {
        ing->submit(make_record(i));
    }
    std::this_thread::sleep_for(30ms);
    gq::step("destrutor do Ingestor com a fila cheia e handler lento");
    const double ms = gq::elapsed_ms([&] { ing.reset(); });
    const std::size_t at_destruction = tally.calls.load();
    CHECK(ms < 200.0, "o destrutor levou " << static_cast<long>(ms) << " ms (limite: 200 ms)");
    CHECK_NO_THREADS_LEFT(baseline, "depois do destrutor do Ingestor");
    std::this_thread::sleep_for(50ms);
    CHECK(tally.calls.load() == at_destruction, "o handler foi chamado " << (tally.calls.load() - at_destruction)
                                                                         << " vez(es) DEPOIS que o destrutor retornou");
    CHECK(at_destruction < kTotal, "o destrutor deveria descartar a fila, mas todos os registros foram processados");
    for (std::size_t i = 0; i < kTotal; ++i) {
        CHECK(tally.seen[i].load() <= 1, "o registro " << i << " foi processado " << tally.seen[i].load() << " vezes");
    }

    // Um Ingestor ocioso destruído sem shutdown() também precisa ser rápido.
    auto idle = std::make_unique<Ingestor>(4, kLongHeartbeat, tally.handler());
    std::this_thread::sleep_for(30ms);
    gq::step("destrutor de um Ingestor ocioso");
    const double idle_ms = gq::elapsed_ms([&] { idle.reset(); });
    CHECK(idle_ms < 200.0, "o destrutor de um Ingestor ocioso levou " << static_cast<long>(idle_ms) << " ms");
    CHECK_NO_THREADS_LEFT(baseline, "depois do destrutor de um Ingestor ocioso");
}

TEST(ingestor_construtor_lanca, "argumentos inválidos lançam invalid_argument sem std::terminate nem threads órfãs") {
    const std::size_t baseline = gq::thread_count();
    Tally tally(1);
    gq::step("Ingestor com 0 workers");
    CHECK_THROWS(Ingestor(0, 50ms, tally.handler()), std::invalid_argument, "0 workers deveria ser recusado");
    gq::step("Ingestor com handler vazio");
    CHECK_THROWS(Ingestor(2, 50ms, Ingestor::Handler{}), std::invalid_argument, "handler vazio deveria ser recusado");
    gq::step("Ingestor com intervalo de heartbeat 0 (o processo não pode abortar)");
    CHECK_THROWS(Ingestor(4, 0ms, tally.handler()), std::invalid_argument,
                 "intervalo de heartbeat 0 deveria ser recusado");
    CHECK_THROWS(Ingestor(1, -1ms, tally.handler()), std::invalid_argument,
                 "intervalo de heartbeat negativo deveria ser recusado");
    CHECK_NO_THREADS_LEFT(baseline, "depois de construções recusadas");
    CHECK(tally.calls.load() == 0, "o handler não deveria ter sido chamado");
}

TEST(ingestor_heartbeat, "o heartbeat informa os registros pendentes e para junto com shutdown()") {
    std::atomic<bool> release{false};
    std::atomic<std::size_t> max_pending{0};
    std::atomic<int> reports{0};
    Ingestor ing(
        1, 10ms,
        [&](const Record&) {
            while (!release.load()) {
                std::this_thread::sleep_for(1ms);
            }
        },
        [&](std::size_t pending) {
            ++reports;
            std::size_t prev = max_pending.load();
            while (pending > prev && !max_pending.compare_exchange_weak(prev, pending)) {
            }
        });
    for (std::uint64_t i = 0; i < 6; ++i) {
        ing.submit(make_record(i));
    }
    gq::step("esperando o heartbeat relatar a fila");
    CHECK(gq::wait_until([&] { return max_pending.load() >= 5; }, 2s),
          "o heartbeat deveria ter relatado >= 5 pendentes, o maior valor foi " << max_pending.load());
    release = true;
    gq::step("shutdown() depois de liberar o handler");
    ing.shutdown();
    CHECK(ing.processed() == 6, "processed() = " << ing.processed() << ", esperava 6");
    const int at_shutdown = reports.load();
    std::this_thread::sleep_for(50ms);
    CHECK(reports.load() == at_shutdown, "o heartbeat continuou relatando depois de shutdown()");
}

TEST(ingestor_submit_concorrente, "submit() concorrente com shutdown(): todo registro aceito é processado exatamente uma vez") {
    const std::size_t baseline = gq::thread_count();
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kMaxPerProducer = 200000;
    Tally tally(kProducers * kMaxPerProducer);
    std::vector<std::atomic<std::size_t>> accepted(kProducers);
    {
        Ingestor ing(3, 5ms, tally.handler());
        std::vector<std::jthread> producers;
        for (std::size_t p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p] {
                for (std::size_t i = 0; i < kMaxPerProducer; ++i) {
                    if (!ing.submit(make_record(p * kMaxPerProducer + i))) {
                        return;
                    }
                    accepted[p].store(i + 1);
                }
            });
        }
        std::this_thread::sleep_for(30ms);
        gq::step("shutdown() enquanto 4 produtores ainda chamam submit()");
        ing.shutdown();
        producers.clear();  // join: todos já receberam false (ou terminaram)
        std::size_t total = 0;
        for (std::size_t p = 0; p < kProducers; ++p) {
            for (std::size_t i = 0; i < kMaxPerProducer; ++i) {
                const int n = tally.seen[p * kMaxPerProducer + i].load();
                const bool was_accepted = i < accepted[p].load();
                CHECK(n == (was_accepted ? 1 : 0), "o registro " << i << " do produtor " << p << " foi "
                                                                  << (was_accepted ? "aceito" : "recusado")
                                                                  << " e processado " << n << " vez(es)");
            }
            total += accepted[p].load();
        }
        CHECK(ing.processed() == total, "processed() = " << ing.processed() << ", mas " << total
                                                         << " registros foram aceitos");
    }
    CHECK_NO_THREADS_LEFT(baseline, "depois de destruir o Ingestor");
}
