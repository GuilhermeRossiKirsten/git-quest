// tracked.h — tipo não trivial que conta construções e destruições.
//
// Serve para verificar o ciclo de vida dos elementos dentro da fila: quantos T
// estão vivos, se algum foi destruído duas vezes ou se a fila "destruiu" uma
// memória onde nunca construiu nada (o campo `magic_` deixa de valer kAlive).
#ifndef GITQUEST_TRACKED_H
#define GITQUEST_TRACKED_H

#include <atomic>
#include <cstdint>
#include <stdexcept>

class Tracked {
public:
    static constexpr std::uint64_t kMovedFrom = ~std::uint64_t{0};

    static inline std::atomic<long> live{0};
    static inline std::atomic<long> constructions{0};
    static inline std::atomic<long> destructions{0};
    static inline std::atomic<long> bad_destructions{0};  // destruição dupla ou de memória crua
    static inline std::atomic<bool> throw_on_copy{false};

    static void reset() {
        live = 0;
        constructions = 0;
        destructions = 0;
        bad_destructions = 0;
        throw_on_copy = false;
    }

    explicit Tracked(std::uint64_t value) : value_(value) { born(); }

    Tracked(const Tracked& other) : value_(other.value_) {
        if (throw_on_copy.load()) {
            throw std::runtime_error("Tracked: cópia recusada de propósito pelo teste");
        }
        born();
    }

    Tracked(Tracked&& other) noexcept : value_(other.value_) {
        other.value_ = kMovedFrom;
        born();
    }

    Tracked& operator=(const Tracked& other) {
        value_ = other.value_;
        return *this;
    }

    Tracked& operator=(Tracked&& other) noexcept {
        value_ = other.value_;
        other.value_ = kMovedFrom;
        return *this;
    }

    ~Tracked() {
        // volatile: o compilador não pode descartar a escrita "morta" no fim da vida.
        volatile std::uint32_t* magic = &magic_;
        if (*magic != kAlive) {
            ++bad_destructions;
            return;
        }
        *magic = kDead;
        --live;
        ++destructions;
    }

    std::uint64_t value() const { return value_; }

private:
    static constexpr std::uint32_t kAlive = 0x7a11c0deU;
    static constexpr std::uint32_t kDead = 0xdeadbeefU;

    void born() {
        volatile std::uint32_t* magic = &magic_;
        *magic = kAlive;
        ++live;
        ++constructions;
    }

    std::uint64_t value_;
    std::uint32_t magic_;
};

#endif
