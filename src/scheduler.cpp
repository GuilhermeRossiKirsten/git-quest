#include "scheduler.h"

#include <utility>

namespace gq {

TaskId Scheduler::schedule(std::string name, Tick deadline) {
    const TaskId id = next_id_++;
    names_.emplace(id, std::move(name));
    queue_.push(id, deadline);
    return id;
}

bool Scheduler::reschedule(TaskId id, Tick deadline) {
    return queue_.update_priority(id, deadline);
}

bool Scheduler::cancel(TaskId id) {
    if (!queue_.erase(id)) {
        return false;
    }
    names_.erase(id);
    return true;
}

std::optional<Task> Scheduler::peek() const {
    if (queue_.empty()) {
        return std::nullopt;
    }
    const TaskId id = queue_.top_key();
    return Task{id, names_.at(id), queue_.top_priority()};
}

std::vector<Task> Scheduler::run_until(Tick now) {
    std::vector<Task> due;
    while (!queue_.empty() && queue_.top_priority() <= now) {
        auto [id, deadline] = queue_.pop();
        auto it = names_.find(id);
        due.push_back(Task{id, std::move(it->second), deadline});
        names_.erase(it);
    }
    return due;
}

}  // namespace gq
