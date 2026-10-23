// REJEITA: comparar uma velocidade com um número puro
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  const auto speed = 10.0_m / 2.0_s; [[maybe_unused]] bool fast = speed == MetersPerSecond{20.0};
#else
  const auto speed = 10.0_m / 2.0_s; [[maybe_unused]] bool fast = speed == 20.0;  // ← não deve compilar
#endif
  return 0;
}
