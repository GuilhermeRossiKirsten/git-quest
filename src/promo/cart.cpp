#include "promo/cart.hpp"

#include <string>

namespace promo {

std::int64_t Cart::subtotal() const {
  std::int64_t sum = 0;
  for (const auto& item : items) sum += item.unit_cents * item.qty;
  return sum;
}

std::int64_t Cart::category_subtotal(const std::string& category) const {
  std::int64_t sum = 0;
  for (const auto& item : items) {
    if (item.category == category) sum += item.unit_cents * item.qty;
  }
  return sum;
}

int Cart::quantity_of(const std::string& sku) const {
  int qty = 0;
  for (const auto& item : items) {
    if (item.sku == sku) qty += item.qty;
  }
  return qty;
}

std::int64_t Cart::unit_price_of(const std::string& sku) const {
  for (const auto& item : items) {
    if (item.sku == sku) return item.unit_cents;
  }
  return 0;
}

std::string format_brl(std::int64_t cents) {
  const bool negative = cents < 0;
  if (negative) cents = -cents;
  std::string reais = std::to_string(cents / 100);
  // separador de milhar: "1234567" -> "1.234.567"
  for (int pos = static_cast<int>(reais.size()) - 3; pos > 0; pos -= 3) {
    reais.insert(static_cast<std::string::size_type>(pos), ".");
  }
  const std::int64_t centavos = cents % 100;
  std::string out = negative ? "-R$ " : "R$ ";
  out += reais;
  out += centavos < 10 ? ",0" : ",";
  out += std::to_string(centavos);
  return out;
}

}  // namespace promo
