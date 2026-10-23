// CONTRATO: tipo sem label() é rejeitado em tempo de compilação
#include "support/probes.hpp"

namespace {
struct NoLabel {
  std::int64_t discount(const promo::Cart&) const { return 1; }
  std::string to_json() const { return "{}"; }
};
struct NoJson {
  std::int64_t discount(const promo::Cart&) const { return 1; }
  std::string label() const { return "x"; }
};
struct NoDiscount {
  std::string label() const { return "x"; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

static_assert(probe::Rejected<NoLabel>, "regra sem label() deve ser rejeitada (PromotionRule, Promotion e Checkout::add)");
static_assert(probe::Rejected<NoJson>, "regra sem to_json() deve ser rejeitada");
static_assert(probe::Rejected<NoDiscount>, "regra sem discount() deve ser rejeitada");
