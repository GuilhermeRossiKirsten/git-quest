#pragma once
// Tipos auxiliares usados pelos testes de contrato e de comportamento.

#include <cstddef>
#include <functional>
#include <string>

namespace test_types {

// Tem ==, mas nenhuma especialização de std::hash.
struct Point {
  int x = 0;
  int y = 0;
  bool operator==(const Point&) const = default;
};

// Tem == e uma especialização de std::hash: deve ser aceito como chave.
struct Sku {
  std::string code;
  bool operator==(const Sku&) const = default;
};

// Tem std::hash, mas nenhum operador ==.
struct HashWithoutEquality {
  int id = 0;
};

}  // namespace test_types

namespace std {
template <>
struct hash<test_types::Sku> {
  std::size_t operator()(const test_types::Sku& sku) const noexcept {
    return std::hash<std::string>{}(sku.code);
  }
};

template <>
struct hash<test_types::HashWithoutEquality> {
  std::size_t operator()(const test_types::HashWithoutEquality& h) const noexcept {
    return std::hash<int>{}(h.id);
  }
};
}  // namespace std
