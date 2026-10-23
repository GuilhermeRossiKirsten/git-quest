// DIAGNÓSTICO: o erro ao usar uma regra incompleta cita o concept PromotionRule
#include "promo/promotion.hpp"

namespace {
struct NoLabel {
  std::int64_t discount(const promo::Cart&) const { return 1; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

int main() {
  promo::Promotion p{NoLabel{}};
  return static_cast<int>(promo::discount(p, promo::Cart{}));
}
