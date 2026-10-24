// log_index.cpp — ingest, absorb e consultas do índice.
#include "log_index.h"

#include <algorithm>

namespace {

constexpr std::size_t kScratchInitial = 4096;

}  // namespace

LogIndex::LogIndex(std::pmr::memory_resource* upstream)
    : arena_(upstream),
      resource_(arena_),
      scratch_(kScratchInitial, upstream),
      messages_(&resource_),
      lines_(&resource_),
      services_(&resource_),
      by_id_(&resource_) {}

LogIndex::ServiceStats& LogIndex::stats_for(std::string_view service) {
    auto it = services_.find(service);
    if (it == services_.end()) {
        ServiceStats* st = arena_.make<ServiceStats>();
        st->id = static_cast<std::uint32_t>(by_id_.size());
        it = services_.emplace(std::string(service), st).first;
        st->name = it->first;
        by_id_.push_back(st);
    }
    return *it->second;
}

const LogIndex::ServiceStats* LogIndex::find_stats(std::string_view service) const {
    auto it = services_.find(service);
    return it == services_.end() ? nullptr : it->second;
}

bool LogIndex::ingest(std::string_view line) {
    ParsedLine parsed;
    if (!parse_line(line, parsed)) {
        ++rejected_;
        return false;
    }
    ServiceStats& st = stats_for(parsed.service);
    ++st.per_level[level_index(parsed.level)];
    ++st.total;
    messages_.push_back(std::string(parsed.message));
    lines_.push_back({st.id, parsed.level});
    return true;
}

void LogIndex::absorb(LogIndex&& shard) {
    if (&shard == this) {
        return;
    }
    // Caminho rápido: um índice vazio adota de uma vez o vetor de mensagens do
    // shard (troca de ponteiros, nenhuma cópia).
    if (messages_.empty()) {
        messages_ = std::move(shard.messages_);
    } else {
        messages_.reserve(messages_.size() + shard.messages_.size());
        for (auto& m : shard.messages_) {
            messages_.push_back(std::move(m));
        }
    }
    lines_.reserve(lines_.size() + shard.lines_.size());
    for (const LineInfo& li : shard.lines_) {
        ServiceStats& mine = stats_for(shard.by_id_[li.service]->name);
        ++mine.per_level[level_index(li.level)];
        ++mine.total;
        lines_.push_back({mine.id, li.level});
    }
    rejected_ += shard.rejected_;

    shard.messages_.clear();
    shard.lines_.clear();
    shard.services_.clear();
    shard.by_id_.clear();
    shard.rejected_ = 0;
}

std::string_view LogIndex::message(std::size_t i) const { return messages_.at(i); }

std::string_view LogIndex::service(std::size_t i) const { return by_id_[lines_.at(i).service]->name; }

Level LogIndex::level(std::size_t i) const { return lines_.at(i).level; }

std::uint64_t LogIndex::count(std::string_view service) const {
    const ServiceStats* st = find_stats(service);
    return st == nullptr ? 0 : st->total;
}

std::uint64_t LogIndex::count(std::string_view service, Level level) const {
    const ServiceStats* st = find_stats(service);
    return st == nullptr ? 0 : st->per_level[level_index(level)];
}

std::pmr::vector<std::pmr::string> LogIndex::top_services(std::size_t k, std::pmr::memory_resource* out) {
    // O rascunho é reaproveitado entre consultas: nenhuma ida ao heap por chamada.
    scratch_.release();

    struct Rank {
        std::uint64_t errors;
        std::uint64_t total;
        std::string_view name;
    };
    std::pmr::vector<Rank> ranking(out);
    ranking.reserve(by_id_.size());
    for (const ServiceStats* st : by_id_) {
        const std::uint64_t errors =
            st->per_level[level_index(Level::Error)] + st->per_level[level_index(Level::Fatal)];
        ranking.push_back({errors, st->total, st->name});
    }
    const std::size_t n = std::min(k, ranking.size());
    std::partial_sort(ranking.begin(), ranking.begin() + static_cast<std::ptrdiff_t>(n), ranking.end(),
                      [](const Rank& a, const Rank& b) {
                          if (a.errors != b.errors) {
                              return a.errors > b.errors;
                          }
                          if (a.total != b.total) {
                              return a.total > b.total;
                          }
                          return a.name < b.name;
                      });

    std::pmr::vector<std::pmr::string> result(&scratch_);
    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        result.emplace_back(ranking[i].name);
    }
    return result;
}
