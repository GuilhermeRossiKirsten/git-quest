// REJEITA: chamar de força o produto massa × distância
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  [[maybe_unused]] Newtons f = 2.0_kg * 3.0_m / square(1.0_s);
#else
  [[maybe_unused]] Newtons f = 2.0_kg * 3.0_m;  // ← não deve compilar
#endif
  return 0;
}
