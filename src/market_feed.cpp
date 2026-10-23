// market_feed.cpp — veja market_feed.h.
#include "market_feed.h"

#include <atomic>
#include <optional>
#include <thread>

#include "spsc_queue.h"

FeedStats replay_feed(const std::vector<Quote>& quotes, std::size_t queue_capacity,
                      const std::function<void(const Quote&)>& handler) {
    SpscQueue<Quote> queue(queue_capacity);
    std::atomic<bool> producer_done{false};
    std::uint64_t delivered = 0;

    std::thread strategy([&] {
        for (;;) {
            if (std::optional<Quote> quote = queue.try_pop()) {
                handler(*quote);
                ++delivered;
                continue;
            }
            // Fila vazia. Se o produtor já terminou, tudo o que ele publicou é
            // visível agora (acquire casa com o release abaixo): esvazia e sai.
            if (producer_done.load(std::memory_order_acquire)) {
                while (std::optional<Quote> quote = queue.try_pop()) {
                    handler(*quote);
                    ++delivered;
                }
                return;
            }
            std::this_thread::yield();
        }
    });

    FeedStats stats;
    for (const Quote& quote : quotes) {
        while (!queue.try_push(quote)) {
            ++stats.producer_stalls;
            std::this_thread::yield();
        }
    }
    producer_done.store(true, std::memory_order_release);
    strategy.join();
    stats.delivered = delivered;
    return stats;
}
