// Testes do modelo de navegação (src/mission/burns.hpp). Os valores esperados
// são calculados aqui com double puro, a partir da planilha original do
// software de solo (durações em segundos, empuxo em libra-força).
#include <array>

#include "mission/burns.hpp"
#include "support/minitest.hpp"

namespace {

constexpr double lbf = 4.4482216152605;  // N
constexpr std::array<double, 4> seconds{12.0, 8.5, 15.0, 10.0};
constexpr std::array<double, 4> pounds_force{0.20, 0.20, 0.18, 0.22};
constexpr double mass_kg = 338.0;

double expected_impulse() {
  double total = 0.0;
  for (std::size_t i = 0; i < seconds.size(); ++i) total += seconds[i] * pounds_force[i] * lbf;
  return total;
}

}  // namespace

TEST(empuxo_em_newtons, "cada queima guarda duração em s e empuxo em N (a planilha está em lbf)") {
  CHECK_EQ(mission::desaturation_burns.size(), seconds.size());
  for (std::size_t i = 0; i < seconds.size(); ++i) {
    CHECK_NEAR(mission::desaturation_burns[i].duration.value(), seconds[i], 1e-12);
    CHECK_NEAR(mission::desaturation_burns[i].thrust.value(), pounds_force[i] * lbf, 1e-12);
  }
}

TEST(impulso_total, "total_impulse soma empuxo × duração: ≈ 40.034 N·s") {
  CHECK_NEAR(mission::total_impulse(mission::desaturation_burns).value(), expected_impulse(), 1e-9);
}

TEST(delta_v, "delta_v = impulso / massa: ≈ 0.11844 m/s para a sonda de 338 kg") {
  const auto dv = mission::delta_v(mission::total_impulse(mission::desaturation_burns), mission::spacecraft_mass);
  CHECK_NEAR(dv.value(), expected_impulse() / mass_kg, 1e-12);
}

TEST(desvio_de_trajetoria, "drift(delta-v, 90 dias) ≈ 921 km") {
  using namespace units::literals;
  const auto dv = mission::delta_v(mission::total_impulse(mission::desaturation_burns), mission::spacecraft_mass);
  const units::Meters offset = mission::drift(dv, 90.0 * 86400.0_s);
  CHECK_NEAR(offset.value(), expected_impulse() / mass_kg * 90.0 * 86400.0, 1e-6);
  CHECK_NEAR(mission::drift(units::MetersPerSecond{2.0}, 3_s).value(), 6.0, 1e-12);
}

int main(int argc, char** argv) { return minitest::main(argc, argv); }
