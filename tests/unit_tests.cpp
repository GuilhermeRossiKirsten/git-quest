// Testes de comportamento da biblioteca units: valores calculados em tempo de
// execução. Os contratos de tipo ficam em tests/contracts e tests/rejects.
#include <sstream>
#include <string>

#include "support/minitest.hpp"
#include "units/units.hpp"

using namespace units;
using namespace units::literals;

namespace {

template <class Q>
std::string text(const Q& q) {
  std::ostringstream os;
  os << q;
  return os.str();
}

}  // namespace

TEST(literais_de_ponto_flutuante, "literais _m, _s, _kg com ponto flutuante guardam o valor em unidades SI") {
  CHECK_EQ((3.5_m).value(), 3.5);
  CHECK_EQ((42.0_s).value(), 42.0);
  CHECK_EQ((2.25_kg).value(), 2.25);
}

TEST(literais_inteiros, "literais inteiros (10_m, 10_s, 3_km, 2_kg) valem o mesmo que os de ponto flutuante") {
  CHECK_EQ((10_m).value(), 10.0);
  CHECK_EQ((10_s).value(), 10.0);
  CHECK_EQ((3_km).value(), 3000.0);
  CHECK_EQ((2_kg).value(), 2.0);
  CHECK_EQ((7_s).value(), (7.0_s).value());
}

TEST(quilometros, "_km converte para metros: 1.5_km vale 1500 m") {
  CHECK_EQ((1.5_km).value(), 1500.0);
  CHECK_EQ((0.001_km).value(), 1.0);
}

TEST(soma_km_com_m, "somar e subtrair km com m respeita a razão 1 km = 1000 m (nos dois sentidos)") {
  CHECK_EQ((1.0_km + 500.0_m).value(), 1500.0);
  CHECK_EQ((500.0_m + 1.0_km).value(), 1500.0);
  CHECK_EQ((2.0_km - 1.0_m).value(), 1999.0);
  CHECK_EQ((1.0_m - 2.0_km).value(), -1999.0);
}

TEST(escala_por_numero, "multiplicar/dividir por número puro escala o valor (q * 2.0, 2.0 * q, q / 2.0)") {
  CHECK_EQ((2.5_km * 2.0).value(), 5000.0);
  CHECK_EQ((2.0 * 1.5_km).value(), 3000.0);
  CHECK_EQ((3.0_km / 2.0).value(), 1500.0);
  CHECK_EQ((1.0 / 4.0_s).value(), 0.25);
}

TEST(produto_e_quociente, "produto e quociente de grandezas usam os valores SI (1 km / 4 s = 250 m/s)") {
  CHECK_EQ((2.0_m * 3.0_m).value(), 6.0);
  CHECK_EQ((1.0_km * 2.0_s).value(), 2000.0);
  CHECK_EQ((100.0_m / 20.0_s).value(), 5.0);
  CHECK_EQ((1.0_km / 4.0_s).value(), 250.0);
  CHECK_EQ(square(3.0_s).value(), 9.0);
  CHECK_EQ(square(2.0_km).value(), 4.0e6);
}

TEST(comparacoes, "comparações entre grandezas da mesma dimensão (==, !=, <, >, <=) consideram a unidade") {
  CHECK(1.0_km == 1000.0_m);
  CHECK(!(1.0_km != 1000.0_m));
  CHECK(1.0_km > 999.0_m);
  CHECK(999.0_m < 1.0_km);
  CHECK(2.0_s <= 2.0_s);
  CHECK(100.0_m / 20.0_s == 5.0_m / 1.0_s);
}

TEST(atribuicao_composta, "+=, -=, *= e /= mantêm a dimensão e o valor certo") {
  Meters d = 1.0_km;
  d += 250.0_m;
  CHECK_EQ(d.value(), 1250.0);
  d -= 0.25_km;
  CHECK_EQ(d.value(), 1000.0);
  d *= 3.0;
  CHECK_EQ(d.value(), 3000.0);
  d /= 2.0;
  CHECK_EQ(d.value(), 1500.0);
}

TEST(negacao_e_copia, "negação unária e cópia preservam o valor") {
  CHECK_EQ((-(2.0_km)).value(), -2000.0);
  const Meters a = 2.0_km;
  Meters b = a;
  CHECK(b == a);
  b = 3.0_m;
  CHECK_EQ(b.value(), 3.0);
  CHECK_EQ(Meters{}.value(), 0.0);
  CHECK_EQ(Meters{2.5}.value(), 2.5);
}

TEST(libra_forca, "_lbf converte libra-força em newtons (1 lbf = 4.4482216152605 N)") {
  CHECK_NEAR((1.0_lbf).value(), 4.4482216152605, 1e-12);
  CHECK_NEAR((2_lbf).value(), 8.896443230521, 1e-12);
  CHECK_NEAR((2.0_lbf * 3.0_s).value(), 6.0 * 4.4482216152605, 1e-12);
  CHECK_NEAR((1.0_lbf + Newtons{1.0}).value(), 5.4482216152605, 1e-12);
}

TEST(impressao_da_dimensao, "operator<< mostra a dimensão resultante (m/s → \"m s^-1\", m*m → \"m^2\")") {
  CHECK_EQ(text(100.0_m / 20.0_s), std::string("5 m s^-1"));
  CHECK_EQ(text(2.0_m * 3.0_m), std::string("6 m^2"));
  CHECK_EQ(text(2.0_kg * 3.0_m / square(1.0_s)), std::string("6 m kg s^-2"));
  CHECK_EQ(text(1.0 / 4.0_s), std::string("0.25 s^-1"));
  CHECK_EQ(text(1.5_km), std::string("1500 m"));
}

TEST(representacao_float, "Quantity<Length, float> calcula em float") {
  const Quantity<Length, float> a{1.5f};
  const Quantity<Time, float> t{0.5f};
  CHECK_EQ((a * 2.0f).value(), 3.0f);
  CHECK_EQ((a / t).value(), 3.0f);
  CHECK_EQ((a + a).value(), 3.0f);
}

int main(int argc, char** argv) { return minitest::main(argc, argv); }
