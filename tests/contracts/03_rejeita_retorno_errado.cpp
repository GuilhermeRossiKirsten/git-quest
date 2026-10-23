// CONTRATO: discount() precisa devolver exatamente std::int64_t (centavos)
#include "support/probes.hpp"

namespace {
struct DoubleDiscount {
  double discount(const promo::Cart&) const { return 1.5; }
  std::string label() const { return "x"; }
  std::string to_json() const { return "{}"; }
};
struct IntLabel {
  std::int64_t discount(const promo::Cart&) const { return 1; }
  int label() const { return 1; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

static_assert(probe::Rejected<DoubleDiscount>, "discount() devolvendo double deve ser rejeitado (dinheiro em centavos inteiros)");
static_assert(probe::Rejected<IntLabel>, "label() que não converte para std::string deve ser rejeitado");
