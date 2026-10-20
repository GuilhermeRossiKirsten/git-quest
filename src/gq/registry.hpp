#pragma once
// Registro nomeado de objetos (configurações, coletores, ...).

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gq/concepts.hpp"

namespace gq {

template <class T>
class Registry {
 public:
  using Entry = std::pair<std::string, T>;

  void add(std::string name, T value) {
    entries_.emplace_back(std::move(name), std::move(value));
  }

  // Ponteiro para o valor registrado com esse nome, ou nullptr.
  const T* find(std::string_view name) const {
    for (const auto& [key, value] : entries_) {
      if (key == name) {
        return &value;
      }
    }
    return nullptr;
  }

  std::size_t size() const { return entries_.size(); }

  // Cópia de todas as entradas, para exportação.
  // Usa make_move_iterator para que o registro também funcione com tipos
  // como std::unique_ptr, que não podem ser copiados.
  std::vector<Entry> snapshot() {
    return {std::make_move_iterator(entries_.begin()),
            std::make_move_iterator(entries_.end())};
  }

 private:
  std::vector<Entry> entries_;
};

}  // namespace gq
