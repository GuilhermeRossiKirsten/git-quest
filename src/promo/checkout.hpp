#pragma once
// O caixa: guarda as promoções ativas e calcula total, cupom e exportação.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "promo/cart.hpp"
#include "promo/promotion.hpp"

namespace promo {

class Checkout {
 public:
  Checkout() = default;
  // Caixas são copiados ao abrir um novo PDV com a mesma configuração.
  Checkout(const Checkout& other);
  Checkout& operator=(const Checkout& other);
  Checkout(Checkout&&) noexcept = default;
  Checkout& operator=(Checkout&&) noexcept = default;
  ~Checkout() = default;

  // Aceita qualquer promoção concreta: c.add(PercentOff(10, "bebidas")).
  template <class P>
  void add(const P& promotion) {
    promotions_.push_back(std::make_unique<P>(promotion));
  }

  std::size_t size() const { return promotions_.size(); }

  // Cópia das promoções ativas, na ordem em que foram adicionadas
  // (quem chama pode mexer à vontade sem afetar o caixa).
  std::vector<Promotion> promotions() const;

  // Subtotal + frete - descontos, nunca abaixo de zero.
  std::int64_t total(const Cart& cart) const;
  // Cupom fiscal (uma linha por promoção que deu desconto).
  std::string receipt(const Cart& cart) const;
  // Array JSON com to_json() de cada promoção, na ordem de inserção.
  std::string export_json() const;

 private:
  std::vector<std::unique_ptr<Promotion>> promotions_;
};

}  // namespace promo
