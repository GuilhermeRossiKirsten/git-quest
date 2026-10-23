#pragma once
// Promoções do caixa.
//
// Como acrescentar um tipo de promoção (wiki interna, revisão 7):
//   1. acrescente um valor em promo::Kind;
//   2. crie a classe derivada de Promotion, passando o Kind no construtor;
//   3. trate o novo caso em discount(), label(), to_json() e clone_promotion()
//      (promotion.cpp) e, se for o caso, em qualquer outro switch que encontrar.
//   4. recompile tudo (todo mundo inclui este header).

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "promo/cart.hpp"

namespace promo {

enum class Kind { PercentOff, FixedOff, BuyXPayY, FreeShipping };

class Promotion {
 public:
  virtual ~Promotion() = default;

  Kind kind() const { return kind_; }

 protected:
  explicit Promotion(Kind kind) : kind_(kind) {}

 private:
  Kind kind_;
};

// "10% em bebidas": percentual sobre o subtotal de uma categoria.
class PercentOff : public Promotion {
 public:
  PercentOff(int percent, std::string category)
      : Promotion(Kind::PercentOff), percent_(percent), category_(std::move(category)) {}

  int percent() const { return percent_; }
  const std::string& category() const { return category_; }

 private:
  int percent_;
  std::string category_;
};

// "R$ 15,00 de desconto acima de R$ 100,00": valor fixo a partir de um subtotal mínimo.
class FixedOff : public Promotion {
 public:
  FixedOff(std::int64_t cents, std::int64_t min_subtotal)
      : Promotion(Kind::FixedOff), cents_(cents), min_subtotal_(min_subtotal) {}

  std::int64_t cents() const { return cents_; }
  std::int64_t min_subtotal() const { return min_subtotal_; }

 private:
  std::int64_t cents_;
  std::int64_t min_subtotal_;
};

// "Leve 3 pague 2: cafe": a cada x unidades do SKU, só y são cobradas.
class BuyXPayY : public Promotion {
 public:
  BuyXPayY(std::string sku, int x, int y) : Promotion(Kind::BuyXPayY), sku_(std::move(sku)), x_(x), y_(y) {}

  const std::string& sku() const { return sku_; }
  int x() const { return x_; }
  int y() const { return y_; }

 private:
  std::string sku_;
  int x_;
  int y_;
};

// "Frete grátis acima de R$ 150,00".
class FreeShipping : public Promotion {
 public:
  explicit FreeShipping(std::int64_t min_subtotal) : Promotion(Kind::FreeShipping), min_subtotal_(min_subtotal) {}

  std::int64_t min_subtotal() const { return min_subtotal_; }

 private:
  std::int64_t min_subtotal_;
};

// Quanto a promoção desconta deste carrinho, em centavos (>= 0).
std::int64_t discount(const Promotion& promotion, const Cart& cart);
// Texto que aparece no cupom fiscal.
std::string label(const Promotion& promotion);
// Representação JSON (uma linha, sem espaços) usada pela exportação.
std::string to_json(const Promotion& promotion);
// Cópia "polimórfica" usada pelo Checkout.
std::unique_ptr<Promotion> clone_promotion(const Promotion& promotion);

}  // namespace promo
