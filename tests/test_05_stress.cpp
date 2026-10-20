// Estresse: o mesmo ciclo de vida repetido 50 vezes para expor falhas intermitentes.
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "check.h"
#include "../src/ingestor.h"

using namespace std::chrono_literals;

TEST(estresse_50_ciclos, "50 ciclos criar/enviar/encerrar (alternando shutdown() e destrutor) sem perda, duplicata ou thread órfã") {
    const std::size_t baseline = gq::thread_count();
    constexpr int kCycles = 50;
    constexpr std::size_t kProducers = 2;
    constexpr std::size_t kPerProducer = 300;
    constexpr std::size_t kTotal = kProducers * kPerProducer;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        std::vector<std::atomic<int>> seen(kTotal);
        std::atomic<std::size_t> calls{0};
        auto ing = std::make_unique<Ingestor>(3, 2ms, [&](const Record& r) {
            seen[r.id].fetch_add(1);
            ++calls;
        });
        {
            std::vector<std::jthread> producers;
            for (std::size_t p = 0; p < kProducers; ++p) {
                producers.emplace_back([&, p] {
                    for (std::size_t i = 0; i < kPerProducer; ++i) {
                        ing->submit(Record{p * kPerProducer + i, {}});
                    }
                });
            }
        }
        const bool graceful = cycle % 2 == 0;
        gq::step(graceful ? "ciclo de estresse: shutdown()" : "ciclo de estresse: destrutor sem shutdown()");
        if (graceful) {
            ing->shutdown();
            CHECK(ing->processed() == kTotal,
                  "ciclo " << cycle << ": processed() = " << ing->processed() << ", esperava " << kTotal);
        }
        ing.reset();
        const std::size_t at_destruction = calls.load();
        CHECK_NO_THREADS_LEFT(baseline, "ciclo " << cycle);
        for (std::size_t i = 0; i < kTotal; ++i) {
            const int n = seen[i].load();
            CHECK(n <= 1, "ciclo " << cycle << ": o registro " << i << " foi processado " << n << " vezes");
            CHECK(!graceful || n == 1, "ciclo " << cycle << ": o registro " << i << " se perdeu no shutdown()");
        }
        CHECK(calls.load() == at_destruction, "ciclo " << cycle << ": handler chamado depois do destrutor");
    }
}
