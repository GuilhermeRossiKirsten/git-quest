#pragma once
// Conversão de valores para o formato texto do exportador de métricas.

#include <sstream>
#include <string>

#include "gq/concepts.hpp"

namespace gq {

// Transforma um valor em texto para o exportador.
template <class T>
std::string serialize(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

}  // namespace gq
