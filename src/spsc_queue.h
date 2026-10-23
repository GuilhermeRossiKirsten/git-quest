// spsc_queue.h — fila sem trava (lock-free) de capacidade fixa, para UM produtor
// e UM consumidor (SPSC).
//
// É a peça central do feed de mercado (src/market_feed.cpp): a thread de rede
// decodifica cotações e as entrega por esta fila à thread da estratégia, sem
// mutex, sem alocação por mensagem e sem chamadas ao kernel.
//
// CONTRATO — os testes dependem dele; mantenha a API pública:
//
//  * SpscQueue<T, IndexT>(capacity) guarda até `capacity` elementos. Qualquer
//    capacidade >= 1 é válida (não só potências de 2): quem configura o feed
//    escolhe o número. capacity == 0 lança std::invalid_argument.
//  * try_push / try_emplace: chamadas SÓ pela thread produtora. Devolvem false
//    se a fila está cheia (e então não tocam no argumento). Se o construtor de
//    T lançar, a exceção propaga e a fila continua como estava.
//  * try_pop: chamada SÓ pela thread consumidora. std::nullopt se vazia.
//  * capacity(): a capacidade pedida no construtor.
//    size_approx(): pode ser chamada de qualquer thread; é exata quando
//    nenhuma outra thread está mexendo na fila.
//  * Ciclo de vida: a fila só cria um T dentro de try_push/try_emplace, e cada
//    T criado é destruído exatamente uma vez (em try_pop ou no destrutor da
//    fila). Uma fila vazia não contém nenhum T vivo. T não precisa ter
//    construtor padrão nem ser copiável (std::unique_ptr<int> é válido).
//  * IndexT é o tipo dos DOIS índices compartilhados entre as threads. O padrão
//    é std::atomic<std::size_t>; os testes usam uma versão instrumentada com a
//    mesma interface mínima: construção a partir de std::size_t,
//    load(std::memory_order) e store(std::size_t, std::memory_order). Use só
//    essas operações nos índices compartilhados (nada de ++, = ou fetch_add).
//  * layout_info() descreve onde ficam os campos quentes (src/queue_layout.h).
#ifndef MARKETFEED_SPSC_QUEUE_H
#define MARKETFEED_SPSC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <utility>

#include "queue_layout.h"

template <typename T, typename IndexT = std::atomic<std::size_t>>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : capacity_(capacity),
          mask_(capacity - 1),
          slots_(static_cast<T*>(::operator new(capacity * sizeof(T), std::align_val_t{alignof(T)}))) {}

    ~SpscQueue() {
        ::operator delete(slots_, std::align_val_t{alignof(T)});
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // --- thread produtora -------------------------------------------------

    template <typename... Args>
    bool try_emplace(Args&&... args) {
        // relaxed basta: cada índice tem um único escritor, e o x86 não
        // reordena stores com stores nem loads com loads.
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_relaxed)) {
            return false;  // cheia
        }
        ::new (static_cast<void*>(slots_ + tail)) T(std::forward<Args>(args)...);
        tail_.store(next, std::memory_order_relaxed);
        return true;
    }

    bool try_push(const T& value) { return try_emplace(value); }
    bool try_push(T&& value) { return try_emplace(std::move(value)); }

    // --- thread consumidora -----------------------------------------------

    std::optional<T> try_pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_relaxed)) {
            return std::nullopt;  // vazia
        }
        std::optional<T> value{std::move(slots_[head])};
        head_.store((head + 1) & mask_, std::memory_order_relaxed);
        return value;
    }

    // --- qualquer thread --------------------------------------------------

    std::size_t capacity() const noexcept { return capacity_; }

    std::size_t size_approx() const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        return (tail - head) & mask_;
    }

    QueueLayout layout_info() const noexcept {
        QueueLayout info;
        info.object_size = sizeof(SpscQueue);
        info.object_align = alignof(SpscQueue);
        info.producer_index = byte_offset(this, &tail_);
        info.consumer_index = byte_offset(this, &head_);
        return info;  // esta versão não guarda cópias locais dos índices
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    T* const slots_;
    IndexT head_{0};  // próximo slot a ler; escrito pelo consumidor
    IndexT tail_{0};  // próximo slot a escrever; escrito pelo produtor
};

#endif
