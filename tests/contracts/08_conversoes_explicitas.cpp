// CONTRATO: construção a partir de double só explícita; nenhuma conversão implícita entre dimensões
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_constructible_v<Meters, double>, "Meters{5.0} deve ser permitido");
static_assert(std::is_default_constructible_v<Meters>, "Meters{} deve ser permitido");
static_assert(!std::is_convertible_v<double, Meters>, "double NÃO deve virar Meters implicitamente");
static_assert(!std::is_convertible_v<Meters, double>, "Meters NÃO deve virar double implicitamente");
static_assert(!std::is_convertible_v<MetersPerSecond, Meters>, "velocidade NÃO deve virar distância");
static_assert(!std::is_constructible_v<Meters, MetersPerSecond>, "nem explicitamente: Meters{velocidade} deve ser recusado");
static_assert(!std::is_convertible_v<Seconds, Meters> && !std::is_convertible_v<Meters, Seconds>, "m e s não se convertem");
static_assert(!std::is_assignable_v<Meters&, double>, "d = 5.0 NÃO deve compilar");
static_assert(!std::is_assignable_v<Meters&, Seconds>, "d = tempo NÃO deve compilar");
