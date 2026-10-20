#include "ingestor.h"

#include <stdexcept>
#include <utility>

Ingestor::Ingestor(std::size_t workers, std::chrono::milliseconds heartbeat_interval, Handler handler,
                   PendingObserver on_heartbeat)
    : handler_(std::move(handler)), on_heartbeat_(std::move(on_heartbeat)) {
    if (workers == 0) {
        throw std::invalid_argument("Ingestor: é preciso pelo menos um worker");
    }
    if (!handler_) {
        throw std::invalid_argument("Ingestor: handler vazio");
    }
    for (std::size_t i = 0; i < workers; ++i) {
        workers_.emplace_back(&Ingestor::worker_loop, this);
    }
    heartbeat_ = std::make_unique<Heartbeat>(heartbeat_interval, [this] {
        if (on_heartbeat_) {
            on_heartbeat_(queue_.size());
        }
    });
}

Ingestor::~Ingestor() {
    if (stopped_) {
        return;
    }
    // O destrutor não pode travar o serviço inteiro esperando os workers:
    // sinalizamos e deixamos as threads terminarem sozinhas.
    running_ = false;
    queue_.close();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.detach();
        }
    }
}

bool Ingestor::submit(Record record) {
    return queue_.push(std::move(record));
}

void Ingestor::shutdown() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    running_ = false;
    queue_.close();
    for (std::thread& worker : workers_) {
        worker.join();
    }
    heartbeat_->stop();
}

std::size_t Ingestor::processed() const {
    return processed_;
}

void Ingestor::worker_loop() {
    while (running_) {
        std::optional<Record> record = queue_.pop();
        if (!record) {
            continue;
        }
        handler_(*record);
        ++processed_;
    }
}
