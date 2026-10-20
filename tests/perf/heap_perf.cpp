// heap_perf.cpp — teste de desempenho (compilado com -O2, sem sanitizers).
//
// Gera 1 milhão de operações mistas (push, pop, update_priority, erase, contains) com
// semente fixa e as executa duas vezes: primeiro no modelo de referência (std::set +
// std::unordered_map, O(log n) por operação) e depois no IndexedMinHeap.
// O heap precisa produzir exatamente os mesmos resultados e não pode ser mais lento que
// kMaxRatio vezes o modelo de referência medido na mesma máquina, no mesmo processo.
//
// Saída: "OK ..." e código 0, ou "FALHA: ..." e código 1.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../support/reference_pq.h"
#include "../support/rng.h"
#include "../../src/indexed_min_heap.h"

namespace {

using Key = std::uint32_t;
using Priority = std::int64_t;
using Clock = std::chrono::steady_clock;

constexpr int kOps = 1000000;
constexpr int kWarmup = 200000;  // as primeiras operações só inserem: o heap chega a ~200 mil chaves
constexpr double kMaxRatio = 3.0;
constexpr double kMinBudget = 0.25;  // segundos: piso para máquinas muito rápidas

enum class Op : std::uint8_t { Push, Pop, Update, Erase, Contains };

struct Step {
    Op op;
    Key key;
    Priority priority;
};

std::vector<Step> make_steps() {
    gq::testing::Rng rng(15999);
    std::vector<Step> steps;
    steps.reserve(kOps);
    Key next_key = 0;
    for (int i = 0; i < kOps; ++i) {
        const Priority p = static_cast<Priority>(rng.below(1u << 20));
        const std::uint64_t r = i < kWarmup ? 0 : rng.below(100);
        // As chaves-alvo vêm das ~400 mil mais recentes: cerca de metade ainda está no heap.
        const Key window = next_key < 400000 ? next_key : 400000;
        const Key target = window == 0 ? 0 : next_key - 1 - static_cast<Key>(rng.below(window));
        if (r < 30) {
            steps.push_back({Op::Push, next_key++, p});
        } else if (r < 45) {
            steps.push_back({Op::Pop, 0, 0});
        } else if (r < 75) {
            steps.push_back({Op::Update, target, p});
        } else if (r < 90) {
            steps.push_back({Op::Erase, target, 0});
        } else {
            steps.push_back({Op::Contains, target, 0});
        }
    }
    return steps;
}

struct Outcome {
    std::uint64_t checksum = 0;
    int done = 0;
    double seconds = 0;
};

// Executa os passos; desiste (done < kOps) se ultrapassar o orçamento de tempo.
template <typename Queue>
Outcome run(const std::vector<Step>& steps, double budget) {
    Queue q;
    Outcome out;
    const auto start = Clock::now();
    auto mix = [&out](std::uint64_t v) { out.checksum = (out.checksum ^ v) * 0x100000001B3ULL; };
    for (const Step& s : steps) {
        switch (s.op) {
            case Op::Push:
                mix(q.push(s.key, s.priority));
                break;
            case Op::Pop:
                if (!q.empty()) {
                    const auto top = q.pop();
                    mix(top.first);
                    mix(static_cast<std::uint64_t>(top.second));
                }
                break;
            case Op::Update:
                mix(q.update_priority(s.key, s.priority));
                break;
            case Op::Erase:
                mix(q.erase(s.key));
                break;
            case Op::Contains:
                mix(q.contains(s.key));
                break;
        }
        ++out.done;
        if ((out.done & 1023) == 0 &&
            std::chrono::duration<double>(Clock::now() - start).count() > budget) {
            break;
        }
    }
    mix(q.size());
    out.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return out;
}

}  // namespace

int main() {
    const std::vector<Step> steps = make_steps();
    const Outcome ref = run<gq::testing::ReferencePQ<Key, Priority>>(steps, 1e9);
    const double budget = ref.seconds * kMaxRatio > kMinBudget ? ref.seconds * kMaxRatio : kMinBudget;
    const Outcome got = run<gq::IndexedMinHeap<Key, Priority>>(steps, budget);

    if (got.done < kOps) {
        std::printf("FALHA: lento demais: em %.2f s o heap fez só %d de %d operações (%.1f%%); "
                    "o modelo std::set fez todas em %.2f s (limite: %.1fx = %.2f s). "
                    "Alguma operação não é O(log n)?\n",
                    got.seconds, got.done, kOps, 100.0 * got.done / kOps, ref.seconds, kMaxRatio, budget);
        return 1;
    }
    if (got.checksum != ref.checksum) {
        std::printf("FALHA: o heap terminou as %d operações em %.2f s, mas os resultados (pops/retornos) "
                    "divergem do modelo de referência — corrija primeiro os testes funcionais\n",
                    kOps, got.seconds);
        return 1;
    }
    if (got.seconds > budget) {
        std::printf("FALHA: lento demais: %.2f s para %d operações; limite %.2f s (%.1fx o modelo std::set, "
                    "%.2f s)\n", got.seconds, kOps, budget, kMaxRatio, ref.seconds);
        return 1;
    }
    std::printf("OK heap %.3f s, modelo std::set %.3f s (%.2fx; limite %.1fx)\n", got.seconds, ref.seconds,
                got.seconds / ref.seconds, kMaxRatio);
    return 0;
}
