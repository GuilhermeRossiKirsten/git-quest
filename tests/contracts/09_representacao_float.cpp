// CONTRATO: Quantity<D, float> calcula em float e combina dimensões igual a double
#include <type_traits>

#include "units/units.hpp"

using namespace units;
using namespace units::literals;

using FloatMeters = Quantity<Length, float>;
using FloatSeconds = Quantity<Time, float>;
static_assert(std::is_same_v<decltype(FloatMeters{1.0f} / FloatSeconds{2.0f}), Quantity<Velocity, float>>,
              "float m / float s deve ser Quantity<Velocity, float>");
static_assert(std::is_same_v<decltype(FloatMeters{1.0f} * 2.0f), FloatMeters>, "float m * float deve ser float m");
static_assert(std::is_same_v<decltype(FloatMeters{1.0f}.value()), float>, "value() deve devolver a representação");
static_assert(std::is_same_v<Meters::rep, double> && std::is_same_v<Meters::dimension, Length>, "aliases rep/dimension");
static_assert((FloatMeters{3.0f} / FloatSeconds{2.0f}).value() == 1.5f, "float em constexpr");
