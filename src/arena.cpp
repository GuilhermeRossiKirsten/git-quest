// arena.cpp — implementação da arena sequencial.
#include "arena.h"

#include <algorithm>

namespace {

// Tudo o que o indexador guarda (ponteiros, contadores, strings) se contenta
// com 8 bytes de alinhamento, então a arena trabalha em grânulos de 8 bytes.
constexpr std::size_t kGranule = 8;

constexpr std::size_t round_up(std::size_t n, std::size_t to) { return (n + to - 1) / to * to; }

}  // namespace

Arena::Arena(std::pmr::memory_resource* upstream, std::size_t chunk_size)
    : upstream_(upstream), chunk_size_(chunk_size) {}

Arena::~Arena() { reset(); }

void* Arena::allocate(std::size_t bytes, std::size_t /*alignment*/) {
    bytes = round_up(bytes == 0 ? 1 : bytes, kGranule);
    if (static_cast<std::size_t>(end_ - cur_) < bytes) {
        add_chunk(bytes);
    }
    void* p = cur_;
    cur_ += bytes;
    return p;
}

void Arena::add_chunk(std::size_t min_payload) {
    constexpr std::size_t header = round_up(sizeof(Chunk), alignof(std::max_align_t));
    const std::size_t total = header + std::max(min_payload, chunk_size_);
    void* raw = upstream_->allocate(total, alignof(std::max_align_t));
    chunks_ = ::new (raw) Chunk{chunks_, total};
    cur_ = static_cast<std::byte*>(raw) + header;
    end_ = static_cast<std::byte*>(raw) + total;
}

void Arena::reset() noexcept {
    while (chunks_ != nullptr) {
        Chunk* next = chunks_->next;
        upstream_->deallocate(chunks_, chunks_->size, alignof(std::max_align_t));
        chunks_ = next;
    }
    cur_ = nullptr;
    end_ = nullptr;
}
