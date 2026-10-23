// CONTRATO: uma regra comum (sem herança) com discount/label/to_json é aceita
#include "support/probes.hpp"

namespace {
// label() devolvendo const char* também serve: só precisa converter para std::string.
struct CStringLabel {
  std::int64_t discount(const promo::Cart&) const { return 0; }
  const char* label() const { return "c"; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

static_assert(probe::Accepted<samples::GoodRule>, "uma regra válida precisa ser aceita por PromotionRule, Promotion e Checkout::add");
static_assert(probe::Accepted<CStringLabel>, "label()/to_json() só precisam ser conversíveis para std::string");
static_assert(probe::CanBuildPromotion<const samples::GoodRule&>, "deve dar para construir a partir de uma regra const lvalue");
