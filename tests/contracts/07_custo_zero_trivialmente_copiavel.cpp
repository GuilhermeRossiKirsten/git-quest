// CONTRATO: custo zero: Quantity é trivialmente copiável e trivialmente destrutível
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

static_assert(std::is_trivially_copyable_v<Meters>, "Meters deve ser trivialmente copiável");
static_assert(std::is_trivially_copyable_v<NewtonSeconds>, "NewtonSeconds deve ser trivialmente copiável");
static_assert(std::is_trivially_copyable_v<Quantity<Length, float>>, "Quantity<Length, float> deve ser trivialmente copiável");
static_assert(std::is_trivially_copy_constructible_v<Seconds>, "a cópia de Seconds deve ser trivial");
static_assert(std::is_trivially_copy_assignable_v<Seconds>, "a atribuição de Seconds deve ser trivial");
static_assert(std::is_trivially_destructible_v<Seconds>, "Seconds deve ser trivialmente destrutível");
static_assert(std::is_standard_layout_v<Meters>, "Meters deve ter layout padrão");
