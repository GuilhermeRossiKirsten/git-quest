#include "batch.h"

#include <utility>

void Batch::add(const Message& msg) { pending_.push_back(msg); }

void Batch::add(const std::string& topic, const Buffer& payload) {
    pending_.push_back(Message(topic, payload));
}

void Batch::transform(const Transform& fn) {
    std::vector<Message> result;
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        result.push_back(fn(pending_[i]));
    }
    pending_ = result;
}

std::size_t Batch::dispatch(const Handler& handler) {
    std::size_t delivered = 0;
    for (std::vector<Message>::const_iterator it = pending_.begin(); it != pending_.end(); ++it) {
        const Message& msg = *it;
        handler(std::move(msg));  // entrega por movimento: o lote não precisa mais dela
        ++delivered;
    }
    pending_.clear();
    return delivered;
}

std::vector<Message> Batch::drain() {
    std::vector<Message> out = pending_;
    pending_.clear();
    return out;
}
