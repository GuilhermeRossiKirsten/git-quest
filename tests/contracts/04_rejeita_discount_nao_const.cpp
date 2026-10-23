// CONTRATO: as operações precisam ser chamáveis em uma regra const
#include "support/probes.hpp"

namespace {
struct MutatingDiscount {
  std::int64_t discount(const promo::Cart&) { return 1; }  // falta const
  std::string label() const { return "x"; }
  std::string to_json() const { return "{}"; }
};
struct WrongCartParam {
  std::int64_t discount(promo::Cart&) const { return 1; }  // exige carrinho mutável
  std::string label() const { return "x"; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

static_assert(probe::Rejected<MutatingDiscount>, "discount() não-const deve ser rejeitado");
static_assert(probe::Rejected<WrongCartParam>, "discount() precisa aceitar const Cart&");
