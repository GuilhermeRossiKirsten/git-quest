#include "promo/checkout.hpp"

#include <algorithm>
#include <utility>

namespace promo {

Checkout::Checkout(const Checkout& other) {
  promotions_.reserve(other.promotions_.size());
  for (const auto& p : other.promotions_) promotions_.push_back(clone_promotion(*p));
}

Checkout& Checkout::operator=(const Checkout& other) {
  if (this != &other) {
    Checkout copy(other);
    promotions_ = std::move(copy.promotions_);
  }
  return *this;
}

std::vector<Promotion> Checkout::promotions() const {
  std::vector<Promotion> out;
  out.reserve(promotions_.size());
  for (const auto& p : promotions_) out.push_back(*p);
  return out;
}

std::int64_t Checkout::total(const Cart& cart) const {
  std::int64_t sum = cart.subtotal() + cart.shipping_cents;
  for (const auto& p : promotions_) sum -= discount(*p, cart);
  return std::max<std::int64_t>(sum, 0);
}

std::string Checkout::receipt(const Cart& cart) const {
  std::string out = "Subtotal: " + format_brl(cart.subtotal()) + "\n";
  if (cart.shipping_cents > 0) out += "Frete: " + format_brl(cart.shipping_cents) + "\n";
  for (const auto& p : promotions_) {
    const std::int64_t d = discount(*p, cart);
    if (d > 0) out += "  " + label(*p) + ": " + format_brl(-d) + "\n";
  }
  out += "Total: " + format_brl(total(cart)) + "\n";
  return out;
}

std::string Checkout::export_json() const {
  std::string out = "[";
  for (std::size_t i = 0; i < promotions_.size(); ++i) {
    if (i > 0) out += ",";
    out += to_json(*promotions_[i]);
  }
  out += "]";
  return out;
}

}  // namespace promo
