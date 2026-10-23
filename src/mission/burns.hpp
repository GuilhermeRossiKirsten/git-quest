#pragma once
// Modelo de navegação da sonda: queimas de dessaturação dos volantes de
// reação (AMD) e o efeito delas na trajetória.

#include <array>
#include <span>

#include "units/units.hpp"

namespace mission {

using namespace units::literals;

struct Burn {
  const char* label;
  units::Seconds duration;
  units::Newtons thrust;
};

// Queimas registradas pelo software de solo SM_FORCES. A planilha de origem
// informa o empuxo dos propulsores em libra-força.
inline const std::array<Burn, 4> desaturation_burns{{
    {"AMD-01", 12.0_s, 0.20},  // lbf
    {"AMD-02", 8.5_s, 0.20},  // lbf
    {"AMD-03", 15.0_s, 0.18},  // lbf
    {"AMD-04", 10.0_s, 0.22},  // lbf
}};

inline const units::Kilograms spacecraft_mass = 338.0_kg;

// Impulso total: soma de empuxo × duração de cada queima.
inline units::NewtonSeconds total_impulse(std::span<const Burn> burns) {
  units::NewtonSeconds total{};
  for (const Burn& burn : burns) total += burn.thrust * burn.duration;
  return total;
}

// Variação de velocidade causada por um impulso em uma sonda de massa m.
inline units::MetersPerSecond delta_v(units::NewtonSeconds impulse, units::Kilograms mass) {
  return impulse / mass;
}

// Desvio de posição acumulado por um erro de velocidade durante um intervalo.
inline units::Meters drift(units::MetersPerSecond velocity_error, units::Seconds elapsed) {
  return velocity_error * elapsed;
}

}  // namespace mission
