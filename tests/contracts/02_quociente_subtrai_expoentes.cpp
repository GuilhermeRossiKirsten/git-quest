// CONTRATO: grandeza / grandeza subtrai os expoentes (m/s, m/s², N, Hz)
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_same_v<decltype(10.0_m / 2.0_s), MetersPerSecond>, "m / s deve ser MetersPerSecond (Dim<1,0,-1>)");
static_assert(std::is_same_v<decltype(10.0_m / square(2.0_s)), MetersPerSecondSquared>, "m / s² deve ser Dim<1,0,-2>");
static_assert(std::is_same_v<decltype(2.0_kg * 3.0_m / square(1.0_s)), Newtons>, "kg * m / s² deve ser Newtons");
static_assert(std::is_same_v<decltype(10.0_m / 2.0_m), Scalar>, "m / m deve ser adimensional");
static_assert(std::is_same_v<decltype(10.0_s / 2.0_m), Quantity<Dim<-1, 0, 1>>>, "s / m deve ser Dim<-1,0,1>");
static_assert(std::is_same_v<decltype(1.0 / 2.0_s), Hertz>, "número / s deve ser Hertz (Dim<0,0,-1>)");
