// probe_index.h — índice instrumentado para SpscQueue<T, ProbeIndex>.
//
// Tem a mesma interface mínima de std::atomic<std::size_t> exigida pelo contrato
// da fila (construção a partir de size_t, load(order), store(valor, order)) e
// registra, por instância e por "fase" do teste, quantas leituras e escritas
// aconteceram e com qual memory_order. Os testes rodam a fila numa única thread
// alternando fases (produtor / consumidor), o que torna a contagem determinística:
//
//   * a instância escrita na fase do produtor é o índice do produtor (tail);
//   * a escrita na fase do consumidor é o índice do consumidor (head);
//   * leituras do índice "do outro lado" precisam ser acquire (ou seq_cst) e as
//     escritas precisam ser release (ou seq_cst);
//   * com uma cópia local do índice oposto, essas leituras são raras.
//
// Também guarda o endereço real de cada índice, o que permite conferir o layout
// em memória sem confiar no que a fila diz de si mesma.
#ifndef GITQUEST_PROBE_INDEX_H
#define GITQUEST_PROBE_INDEX_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace probe {

enum Phase { kNone = 0, kProducer = 1, kConsumer = 2 };

struct Stats {
    std::uint64_t loads = 0;
    std::uint64_t weak_loads = 0;  // mais fracas que acquire
    std::uint64_t stores = 0;
    std::uint64_t weak_stores = 0;  // mais fracas que release
    const char* weak_load_order = nullptr;
    const char* weak_store_order = nullptr;
};

struct Entry {
    const void* address = nullptr;
    bool alive = true;
    Stats by_phase[3];
};

inline Phase current_phase = kNone;

inline std::vector<Entry>& entries() {
    static std::vector<Entry> all;
    return all;
}

inline Entry& entry_for(const void* address) {
    for (Entry& e : entries()) {
        if (e.alive && e.address == address) {
            return e;
        }
    }
    entries().push_back(Entry{address, true, {}});
    return entries().back();
}

inline const char* order_name(std::memory_order order) {
    switch (order) {
        case std::memory_order_relaxed: return "memory_order_relaxed";
        case std::memory_order_consume: return "memory_order_consume";
        case std::memory_order_acquire: return "memory_order_acquire";
        case std::memory_order_release: return "memory_order_release";
        case std::memory_order_acq_rel: return "memory_order_acq_rel";
        case std::memory_order_seq_cst: return "memory_order_seq_cst";
    }
    return "?";
}

// Instâncias vivas cujo endereço cai dentro de [object, object + size).
inline std::vector<Entry*> inside(const void* object, std::size_t size) {
    std::vector<Entry*> found;
    const auto begin = reinterpret_cast<std::uintptr_t>(object);
    for (Entry& e : entries()) {
        const auto at = reinterpret_cast<std::uintptr_t>(e.address);
        if (e.alive && at >= begin && at < begin + size) {
            found.push_back(&e);
        }
    }
    return found;
}

}  // namespace probe

class ProbeIndex {
public:
    ProbeIndex(std::size_t value) : value_(value) {  // NOLINT: conversão implícita de propósito
        probe::entry_for(this);
    }
    ~ProbeIndex() { probe::entry_for(this).alive = false; }
    ProbeIndex(const ProbeIndex&) = delete;
    ProbeIndex& operator=(const ProbeIndex&) = delete;

    std::size_t load(std::memory_order order) const {
        probe::Stats& s = probe::entry_for(this).by_phase[probe::current_phase];
        ++s.loads;
        if (order != std::memory_order_acquire && order != std::memory_order_seq_cst) {
            ++s.weak_loads;
            s.weak_load_order = probe::order_name(order);
        }
        return value_.load(std::memory_order_seq_cst);
    }

    void store(std::size_t value, std::memory_order order) {
        probe::Stats& s = probe::entry_for(this).by_phase[probe::current_phase];
        ++s.stores;
        if (order != std::memory_order_release && order != std::memory_order_seq_cst) {
            ++s.weak_stores;
            s.weak_store_order = probe::order_name(order);
        }
        value_.store(value, std::memory_order_seq_cst);
    }

private:
    std::atomic<std::size_t> value_;
};

#endif
