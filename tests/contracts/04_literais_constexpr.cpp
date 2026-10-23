// CONTRATO: literais são constexpr e já entregam o valor SI (1.5_km == 1500 m, 10_s == 10 s)
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_same_v<decltype(1.5_km), Meters>, "_km deve produzir Meters");
static_assert(std::is_same_v<decltype(10_s), Seconds>, "_s deve produzir Seconds");
static_assert(std::is_same_v<decltype(2_kg), Kilograms>, "_kg deve produzir Kilograms");
static_assert(std::is_same_v<decltype(1.0_lbf), Newtons>, "_lbf deve produzir Newtons");

constexpr Meters distance = 1.5_km;
static_assert(distance.value() == 1500.0, "1.5_km deve valer 1500 m em tempo de compilação");
static_assert((3_km).value() == 3000.0, "3_km deve valer 3000 m");
static_assert((10_s).value() == 10.0, "10_s deve valer 10 s");
static_assert((10.0_s).value() == 10.0, "10.0_s deve valer 10 s");
static_assert((7_m).value() == 7.0, "7_m deve valer 7 m");
static_assert((2.5_kg).value() == 2.5, "2.5_kg deve valer 2.5 kg");
