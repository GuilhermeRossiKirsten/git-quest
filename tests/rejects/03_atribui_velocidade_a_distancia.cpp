// REJEITA: atribuir uma velocidade a uma distância existente
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

int main() {
#ifdef GQ_VALID
  Meters d = 1.0_m; d = 10.0_m / 2.0_s * 1.0_s; static_cast<void>(d);
#else
  Meters d = 1.0_m; d = 10.0_m / 2.0_s; static_cast<void>(d);  // ← não deve compilar
#endif
  return 0;
}
