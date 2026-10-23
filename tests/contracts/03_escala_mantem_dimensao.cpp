// CONTRATO: multiplicar ou dividir por número puro mantém a dimensão
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_same_v<decltype(2.0_m * 3.0), Meters>, "Meters * double deve ser Meters");
static_assert(std::is_same_v<decltype(3.0 * 2.0_m), Meters>, "double * Meters deve ser Meters");
static_assert(std::is_same_v<decltype(2.0_m / 3.0), Meters>, "Meters / double deve ser Meters");
static_assert(std::is_same_v<decltype(-(2.0_m)), Meters>, "-Meters deve ser Meters");
static_assert(std::is_same_v<decltype(1.0_km + 2.0_m), Meters>, "km + m deve ser Meters");
