// CONTRATO: aritmética e comparações funcionam em constexpr
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

constexpr auto speed = 100.0_m / 20.0_s;
static_assert(speed.value() == 5.0, "100 m / 20 s deve valer 5 m/s em tempo de compilação");
static_assert(speed == MetersPerSecond{5.0}, "== deve funcionar em constexpr");
static_assert(1.0_km + 500.0_m == 1500.0_m, "1 km + 500 m deve ser 1500 m");
static_assert(2.0_km - 1.0_m == 1999.0_m, "2 km - 1 m deve ser 1999 m");
static_assert(1.0_km > 999.0_m && 999.0_m < 1.0_km, "< e > devem funcionar em constexpr");
static_assert(2.0_s * 3.0 == 6.0_s && 3.0 * 2.0_s == 6.0_s && 6.0_s / 3.0 == 2.0_s, "escala em constexpr");
static_assert(square(3.0_s) == Quantity<Dim<0, 0, 2>>{9.0}, "square em constexpr");
static_assert((1.0 / 4.0_s).value() == 0.25, "número / grandeza em constexpr");
static_assert(-(2.0_m) == Meters{-2.0}, "negação em constexpr");

constexpr Meters accumulate() {
  Meters total{};
  total += 1.0_km;
  total -= 250.0_m;
  total *= 2.0;
  total /= 3.0;
  return total;
}
static_assert(accumulate() == 500.0_m, "+=, -=, *=, /= devem funcionar em constexpr");
