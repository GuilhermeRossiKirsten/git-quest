// CONTRATO: regras precisam ser copiáveis (Promotion é um tipo valor)
#include <memory>

#include "support/probes.hpp"

namespace {
struct MoveOnly {
  std::unique_ptr<int> state;
  std::int64_t discount(const promo::Cart&) const { return 1; }
  std::string label() const { return "x"; }
  std::string to_json() const { return "{}"; }
};
}  // namespace

static_assert(probe::Rejected<MoveOnly>, "regra não copiável deve ser rejeitada");
