// REJEITA: converter uma distância em double implicitamente
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  [[maybe_unused]] double x = (3.0_m).value();
#else
  [[maybe_unused]] double x = 3.0_m;  // ← não deve compilar
#endif
  return 0;
}
