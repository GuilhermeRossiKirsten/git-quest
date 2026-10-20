#pragma once
// Estatísticas simples sobre sequências de valores.

#include <iterator>

#include "gq/concepts.hpp"

namespace gq {

// Média aritmética dos valores.
template <class Range>
typename Range::value_type mean(const Range& values) {
  typename Range::value_type sum{};
  for (const auto& v : values) {
    sum += v;
  }
  return sum / std::size(values);
}

// Limita cada valor ao intervalo [lo, hi], no próprio container.
template <class Range, class T>
void clamp_all(Range& values, const T& lo, const T& hi) {
  for (auto& v : values) {
    if (v < lo) {
      v = lo;
    } else if (hi < v) {
      v = hi;
    }
  }
}

}  // namespace gq
