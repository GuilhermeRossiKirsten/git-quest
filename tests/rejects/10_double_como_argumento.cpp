// REJEITA: passar um double para um parâmetro Meters
// Este arquivo NÃO deve compilar. Com -DGQ_VALID ele usa a forma correta e
// precisa compilar — assim o avaliador sabe que a recusa vem da regra de
// dimensões, e não de um erro qualquer.
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static void altitude(Meters) {}

int main() {
#ifdef GQ_VALID
  altitude(Meters{5.0});
#else
  altitude(5.0);  // ← não deve compilar
#endif
  return 0;
}
