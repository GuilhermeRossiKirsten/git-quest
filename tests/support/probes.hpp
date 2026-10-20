#pragma once
// "Sondas" usadas pelos testes de contrato: cada uma responde, em tempo de
// compilação, se uma chamada da API gq é válida para certos tipos.
//
// Elas precisam ser templates: uma requires-expression fora de um template
// não "substitui" nada, e uma chamada inválida nela vira erro de compilação
// em vez de resultar em false.

#include <memory>
#include <string>
#include <utility>

#include "gq/metrics.hpp"

namespace probe {

template <class R>
concept CanMean = requires(const R& values) { gq::mean(values); };

template <class R, class T>
concept CanClampAll = requires(R& values, T bound) { gq::clamp_all(values, bound, bound); };

template <class Key>
concept CanMakeHistogram = requires { typename gq::Histogram<Key>; };

template <class T>
concept CanSerialize = requires(const T& value) { gq::serialize(value); };

template <class Reg>
concept CanSnapshot = requires(Reg& registry) { registry.snapshot(); };

template <class T>
concept CanAddAndFind = requires(gq::Registry<T>& registry, T value) {
  registry.add(std::string("x"), std::move(value));
  registry.find("x");
};

}  // namespace probe
