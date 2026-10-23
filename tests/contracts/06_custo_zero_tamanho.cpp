// CONTRATO: custo zero: uma Quantity ocupa exatamente o espaço da sua representação
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(sizeof(Meters) == sizeof(double), "sizeof(Meters) deve ser sizeof(double)");
static_assert(sizeof(MetersPerSecond) == sizeof(double), "sizeof(MetersPerSecond) deve ser sizeof(double)");
static_assert(alignof(Meters) == alignof(double), "alignof(Meters) deve ser alignof(double)");
static_assert(sizeof(Quantity<Length, float>) == sizeof(float), "sizeof(Quantity<Length, float>) deve ser sizeof(float)");
static_assert(sizeof(Meters[8]) == sizeof(double[8]), "um array de Meters deve ocupar o mesmo que um array de double");
