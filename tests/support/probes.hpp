#pragma once
// "Sondas" usadas pelos testes de contrato: cada uma responde, em tempo de
// compilação, se um uso da API promo é válido para um tipo.
//
// Elas precisam ser templates: uma requires-expression fora de um template
// não "substitui" nada, e um uso inválido nela vira erro de compilação em vez
// de resultar em false.

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "promo/checkout.hpp"
#include "promo/promotion.hpp"

namespace probe {

// Promotion p(regra);  (construção explícita)
template <class T>
concept CanBuildPromotion = requires(T rule) { promo::Promotion(std::move(rule)); };

// Promotion p = regra;  (conversão implícita)
template <class T>
concept ConvertsToPromotion = std::is_convertible_v<T, promo::Promotion>;

// checkout.add(regra);
template <class T>
concept CanAdd = requires(promo::Checkout& checkout, T rule) { checkout.add(std::move(rule)); };

// O tipo é rejeitado por TODAS as portas de entrada.
template <class T>
concept Rejected = !promo::PromotionRule<T> && !CanBuildPromotion<T> && !ConvertsToPromotion<T> && !CanAdd<T>;

// O tipo é aceito por todas elas.
template <class T>
concept Accepted = promo::PromotionRule<T> && CanBuildPromotion<T> && ConvertsToPromotion<T> && CanAdd<T>;

}  // namespace probe

// Tipos de apoio: uma regra válida e variações que quebram o contrato.
namespace samples {

struct GoodRule {
  std::int64_t discount(const promo::Cart&) const { return 1; }
  std::string label() const { return "boa"; }
  std::string to_json() const { return "{}"; }
};

}  // namespace samples
