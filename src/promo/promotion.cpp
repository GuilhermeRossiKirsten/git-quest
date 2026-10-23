#include "promo/promotion.hpp"

#include <algorithm>
#include <string>

namespace promo {

std::int64_t discount(const Promotion& promotion, const Cart& cart) {
  switch (promotion.kind()) {
    case Kind::PercentOff: {
      const auto& p = static_cast<const PercentOff&>(promotion);
      return cart.category_subtotal(p.category()) * p.percent() / 100;
    }
    case Kind::FixedOff: {
      const auto& p = static_cast<const FixedOff&>(promotion);
      const std::int64_t subtotal = cart.subtotal();
      if (subtotal < p.min_subtotal()) return 0;
      return std::min(p.cents(), subtotal);
    }
    case Kind::BuyXPayY: {
      const auto& p = static_cast<const BuyXPayY&>(promotion);
      if (p.x() <= 0 || p.y() >= p.x()) return 0;
      const int groups = cart.quantity_of(p.sku()) / p.x();
      return static_cast<std::int64_t>(groups) * (p.x() - p.y()) * cart.unit_price_of(p.sku());
    }
    case Kind::FreeShipping: {
      const auto& p = static_cast<const FreeShipping&>(promotion);
      return cart.subtotal() >= p.min_subtotal() ? cart.shipping_cents : 0;
    }
  }
  return 0;
}

std::string label(const Promotion& promotion) {
  switch (promotion.kind()) {
    case Kind::PercentOff: {
      const auto& p = static_cast<const PercentOff&>(promotion);
      return std::to_string(p.percent()) + "% em " + p.category();
    }
    case Kind::FixedOff: {
      const auto& p = static_cast<const FixedOff&>(promotion);
      std::string text = format_brl(p.cents()) + " de desconto";
      if (p.min_subtotal() > 0) text += " acima de " + format_brl(p.min_subtotal());
      return text;
    }
    case Kind::BuyXPayY: {
      const auto& p = static_cast<const BuyXPayY&>(promotion);
      return "Leve " + std::to_string(p.x()) + " pague " + std::to_string(p.y()) + ": " + p.sku();
    }
    case Kind::FreeShipping: {
      const auto& p = static_cast<const FreeShipping&>(promotion);
      return "Frete grátis acima de " + format_brl(p.min_subtotal());
    }
  }
  return "?";
}

// A exportação foi escrita por outra equipe, que preferiu dynamic_cast.
std::string to_json(const Promotion& promotion) {
  if (const auto* p = dynamic_cast<const PercentOff*>(&promotion)) {
    return R"({"type":"percent_off","percent":)" + std::to_string(p->percent()) + R"(,"category":")" +
           p->category() + R"("})";
  }
  if (const auto* p = dynamic_cast<const FixedOff*>(&promotion)) {
    return R"({"type":"fixed_off","cents":)" + std::to_string(p->cents()) + R"(,"min_subtotal":)" +
           std::to_string(p->min_subtotal()) + "}";
  }
  if (const auto* p = dynamic_cast<const BuyXPayY*>(&promotion)) {
    return R"({"type":"buy_x_pay_y","sku":")" + p->sku() + R"(","x":)" + std::to_string(p->x()) +
           R"(,"y":)" + std::to_string(p->y()) + "}";
  }
  return R"({"type":"unknown"})";
}

std::unique_ptr<Promotion> clone_promotion(const Promotion& promotion) {
  switch (promotion.kind()) {
    case Kind::PercentOff:
      return std::make_unique<PercentOff>(static_cast<const PercentOff&>(promotion));
    case Kind::FixedOff:
      return std::make_unique<FixedOff>(static_cast<const FixedOff&>(promotion));
    case Kind::FreeShipping:
      return std::make_unique<FreeShipping>(static_cast<const FreeShipping&>(promotion));
    default:
      // Não deveria acontecer; copia pelo menos a parte comum.
      return std::make_unique<Promotion>(promotion);
  }
}

}  // namespace promo
