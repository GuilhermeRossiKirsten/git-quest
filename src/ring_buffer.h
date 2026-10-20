// ring_buffer.h — fila circular (FIFO) de capacidade fixa, usada pelo log de telemetria.
//
// Contrato público (usado por src/telemetry_log.h e pelos testes) — mantenha nomes,
// parâmetros e retornos; o funcionamento interno é seu:
//
//   gq::RingBuffer<T, N>      fila de até N elementos do tipo T (N >= 1, fixo na compilação)
//   capacity()                N (constexpr)
//   size() / empty() / full() quantos elementos há agora; full() <=> size() == capacity()
//   push(v)                   insere no fim; se a fila está cheia devolve false e não altera nada
//   push_overwrite(v)         insere no fim; se a fila está cheia, descarta o elemento mais antigo
//   pop()                     remove e devolve o mais antigo; std::nullopt se a fila está vazia
//   front() / back()          o mais antigo / o mais novo            (pré-condição: !empty())
//   operator[](i)             i-ésimo a partir do mais antigo, 0 == front() (pré-condição: i < size())
//   clear()                   remove todos os elementos
//   begin() / end()           percorre do mais antigo ao mais novo (serve para range-for)
//
// Requisitos de projeto — este código também roda no firmware do sensor, onde não há heap:
//   1. nenhuma alocação dinâmica: os N lugares ficam dentro do próprio objeto RingBuffer;
//   2. T não precisa ter construtor padrão (T() pode nem existir);
//   3. um elemento só existe (está construído) enquanto está na fila: pop(), clear(),
//      push_overwrite() e o destrutor da fila destroem os objetos que removem;
//   4. se a fila puder ser copiada, a cópia é independente da original (proibir a cópia
//      com `= delete` também é aceito).
#ifndef GQ_RING_BUFFER_H
#define GQ_RING_BUFFER_H

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace gq {

template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N >= 1, "RingBuffer precisa de capacidade >= 1");

public:
    // Iterador somente leitura, do mais antigo (posição 0) ao mais novo (posição size()-1).
    class const_iterator {
    public:
        const_iterator(const RingBuffer* buffer, std::size_t pos) : buffer_(buffer), pos_(pos) {}
        const T& operator*() const { return (*buffer_)[pos_]; }
        const T* operator->() const { return &(*buffer_)[pos_]; }
        const_iterator& operator++() {
            ++pos_;
            return *this;
        }
        bool operator==(const const_iterator& other) const { return pos_ == other.pos_; }
        bool operator!=(const const_iterator& other) const { return pos_ != other.pos_; }

    private:
        const RingBuffer* buffer_;
        std::size_t pos_;
    };

    RingBuffer() : slots_(N) {}

    static constexpr std::size_t capacity() noexcept { return N; }

    // head_ aponta para o elemento mais antigo; tail_ para a próxima posição livre.
    bool empty() const noexcept { return head_ == tail_; }
    bool full() const noexcept { return (tail_ + 1) % N == head_; }
    std::size_t size() const noexcept { return (tail_ + N - head_) % N; }

    bool push(T value) {
        if (full()) {
            return false;
        }
        slots_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % N;
        return true;
    }

    void push_overwrite(T value) {
        slots_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % N;
    }

    std::optional<T> pop() {
        if (empty()) {
            return std::nullopt;
        }
        T value = slots_[head_];
        head_ = (head_ + 1) % N;
        return value;
    }

    const T& front() const { return slots_[head_]; }
    const T& back() const { return slots_[(tail_ - 1) % N]; }
    const T& operator[](std::size_t i) const { return slots_[i % N]; }

    void clear() noexcept { head_ = tail_ = 0; }

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }

private:
    std::vector<T> slots_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

}  // namespace gq

#endif
