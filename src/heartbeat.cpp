#include "heartbeat.h"

#include <stdexcept>
#include <utility>

Heartbeat::Heartbeat(std::chrono::milliseconds interval, std::function<void()> beat)
    : interval_(interval), beat_(std::move(beat)) {
    if (interval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Heartbeat: o intervalo precisa ser positivo");
    }
    if (!beat_) {
        throw std::invalid_argument("Heartbeat: callback vazio");
    }
    thread_ = std::thread(&Heartbeat::run, this);
}

Heartbeat::~Heartbeat() {
    // Não queremos segurar o destrutor por um intervalo inteiro (pode ser de
    // vários segundos): avisamos a thread e seguimos em frente.
    running_ = false;
    if (thread_.joinable()) {
        thread_.detach();
    }
}

void Heartbeat::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::uint64_t Heartbeat::beats() const {
    return beats_;
}

void Heartbeat::run() {
    while (running_) {
        std::this_thread::sleep_for(interval_);
        if (!running_) {
            break;
        }
        beat_();
        ++beats_;
    }
}
