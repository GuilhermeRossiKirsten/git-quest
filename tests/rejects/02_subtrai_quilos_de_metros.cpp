// REJEITA: subtrair quilogramas de metros
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  [[maybe_unused]] auto rest = 10.0_m - 2.0_m;
#else
  [[maybe_unused]] auto rest = 10.0_m - 2.0_kg;  // ← não deve compilar
#endif
  return 0;
}
