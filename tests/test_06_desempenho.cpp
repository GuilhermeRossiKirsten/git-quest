// Medição INFORMATIVA (não é critério de conclusão): vazão da SpscQueue contra
// uma fila com std::mutex + std::deque, com as mesmas duas threads.
//
// Só roda no build otimizado sem sanitizers ([perf] em tests/run.sh). Tempo de
// parede num container com CPUs compartilhadas varia muito entre execuções, por
// isso o resultado vira, no máximo, um aviso — os critérios do desafio são todos
// determinísticos (veja test_03_ordem.cpp e test_04_layout.cpp).
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

#include "check.h"
#include "../src/spsc_queue.h"

namespace {

constexpr std::uint64_t kItems = 5'000'000;
constexpr std::size_t kCapacity = 1024;

class MutexQueue {
public:
    bool try_push(std::uint64_t v) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.size() == kCapacity) {
            return false;
        }
        items_.push_back(v);
        return true;
    }
    std::optional<std::uint64_t> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
            return std::nullopt;
        }
        std::uint64_t v = items_.front();
        items_.pop_front();
        return v;
    }

private:
    std::mutex mutex_;
    std::deque<std::uint64_t> items_;
};

template <typename Queue>
double millions_per_second(Queue& q) {
    std::uint64_t sum = 0;
    const auto start = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        for (std::uint64_t i = 0; i < kItems;) {
            if (std::optional<std::uint64_t> v = q.try_pop()) {
                sum += *v;
                ++i;
            }
        }
    });
    for (std::uint64_t i = 0; i < kItems; ++i) {
        while (!q.try_push(i)) {
        }
    }
    consumer.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(sum == kItems * (kItems - 1) / 2, "a soma dos itens recebidos não confere");
    return static_cast<double>(kItems) / seconds / 1e6;
}

}  // namespace

TEST(desempenho_vs_mutex, "vazão da fila sem trava contra mutex + deque (informativo)") {
    MutexQueue locked;
    SpscQueue<std::uint64_t> lock_free(kCapacity);
    const double base = millions_per_second(locked);
    const double ours = millions_per_second(lock_free);
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << "SpscQueue: " << ours << " M itens/s; mutex+deque: " << base << " M itens/s ("
        << ours / base << "x)";
    gq::note(out.str());
    if (ours < base) {
        gq::note("AVISO: a fila sem trava foi mais lenta que a fila com mutex nesta máquina");
    }
}
