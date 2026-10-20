#pragma once
// Contagem de ocorrências por chave (ex.: requisições por endpoint).

#include <cstddef>
#include <unordered_map>

#include "gq/concepts.hpp"

namespace gq {

template <class Key>
class Histogram {
 public:
  void add(const Key& key, std::size_t n = 1) {
    counts_[key] += n;
    total_ += n;
  }

  std::size_t count(const Key& key) const {
    const auto it = counts_.find(key);
    return it == counts_.end() ? 0 : it->second;
  }

  std::size_t distinct() const { return counts_.size(); }
  std::size_t total() const { return total_; }

 private:
  std::unordered_map<Key, std::size_t> counts_;
  std::size_t total_ = 0;
};

}  // namespace gq
