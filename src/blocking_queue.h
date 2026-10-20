// blocking_queue.h — fila FIFO bloqueante, várias threads produtoras e consumidoras.
//
// Contrato público (usado pelos testes e por outras equipes — mantenha):
//
//   bool push(T value)
//       Enfileira e acorda um consumidor. Devolve false (e descarta o valor)
//       se a fila já foi fechada.
//
//   std::optional<T> pop(std::stop_token stop = {})
//       Bloqueia até haver um item e o devolve. Devolve std::nullopt somente se
//         (a) a fila foi fechada E está vazia, ou
//         (b) foi pedida parada em `stop` enquanto não havia item.
//       Enquanto a fila estiver aberta e ninguém pedir parada, pop() NUNCA devolve
//       std::nullopt. Fechar a fila não descarta itens: eles continuam saindo até
//       a fila esvaziar.
//
//   void close()
//       Fecha a fila: novos push() são recusados e TODOS os consumidores
//       bloqueados em pop() acordam. Idempotente.
//
//   std::size_t size() const
//
// Histórico: a fila nasceu em C++11 e alguém começou a migração para C++20
// (o parâmetro std::stop_token de pop()), mas não terminou.
#ifndef GITQUEST_BLOCKING_QUEUE_H
#define GITQUEST_BLOCKING_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

template <typename T>
class BlockingQueue {
public:
    BlockingQueue() = default;
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    bool push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }
            items_.push_back(std::move(value));
        }
        ready_.notify_one();
        return true;
    }

    std::optional<T> pop(std::stop_token stop = {}) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (items_.empty() && !closed_ && !stop.stop_requested()) {
            ready_.wait(lock);  // acordado por push() ou por close()
        }
        if (items_.empty()) {
            return std::nullopt;
        }
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_one();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> items_;
    bool closed_ = false;
};

#endif
