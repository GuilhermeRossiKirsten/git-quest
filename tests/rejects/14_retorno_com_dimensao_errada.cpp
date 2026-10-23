// REJEITA: devolver distância × tempo onde se espera velocidade
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static MetersPerSecond speed(Meters d, Seconds t) {
#ifdef GQ_VALID
  return d / t;
#else
  return d * t;  // ← não deve compilar
#endif
}

int main() {
  [[maybe_unused]] auto v = speed(10.0_m, 2.0_s);
  return 0;
}
