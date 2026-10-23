// REJEITA: somar um número puro a uma velocidade
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  const auto speed = 10.0_m / 2.0_s; [[maybe_unused]] auto faster = 2.3 * speed;
#else
  const auto speed = 10.0_m / 2.0_s; [[maybe_unused]] auto faster = 2.3 + speed;  // ← não deve compilar
#endif
  return 0;
}
