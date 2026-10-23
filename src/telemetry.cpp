// Relatório de telemetria de navegação: quanto as queimas de dessaturação
// mudaram a velocidade da sonda e quanto isso desloca a chegada a Marte.
#include <cstdio>

#include "mission/burns.hpp"
#include "units/units.hpp"

int main() {
  using namespace units::literals;

  std::printf("%-8s %10s %12s\n", "queima", "duração", "empuxo");
  for (const mission::Burn& burn : mission::desaturation_burns) {
    const double seconds = burn.duration;
    const double newtons = burn.thrust;
    std::printf("%-8s %8.1f s %10.4f N\n", burn.label, seconds, newtons);
  }

  const double impulse = mission::total_impulse(mission::desaturation_burns);
  const double dv = mission::delta_v(impulse, mission::spacecraft_mass);
  const double offset = mission::drift(dv, 90.0 * 86400.0_s);

  std::printf("\nimpulso total          %12.4f N·s\n", impulse);
  std::printf("delta-v                %12.6f m/s\n", dv);
  std::printf("desvio em 90 dias      %12.1f km\n", offset / 1000.0);
  return 0;
}
