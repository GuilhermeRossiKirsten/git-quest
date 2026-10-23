// CONTRATO: grandeza × grandeza soma os expoentes das dimensões (m·m = m², N·s = impulso)
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_same_v<decltype(2.0_m * 3.0_m), SquareMeters>, "m * m deve ser SquareMeters (Dim<2,0,0>)");
static_assert(std::is_same_v<decltype(2.0_kg * 3.0_m), Quantity<Dim<1, 1, 0>>>, "kg * m deve ser Dim<1,1,0>");
static_assert(std::is_same_v<decltype(2.0_s * 3.0_m), Quantity<Dim<1, 0, 1>>>, "s * m deve ser Dim<1,0,1> (a ordem não importa)");
static_assert(std::is_same_v<decltype(square(3.0_s)), Quantity<Dim<0, 0, 2>>>, "square(s) deve ser Dim<0,0,2>");
static_assert(std::is_same_v<decltype(1.0_lbf * 1.0_s), NewtonSeconds>, "N * s deve ser NewtonSeconds (impulso)");
