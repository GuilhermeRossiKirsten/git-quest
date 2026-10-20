// Testes de comportamento da biblioteca gq (usos VÁLIDOS da API).
// Os usos inválidos são verificados em tempo de compilação: tests/contracts/.
#include <array>
#include <climits>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gq/metrics.hpp"
#include "support/minitest.hpp"
#include "support/types.hpp"

// ---------------------------------------------------------------- mean

TEST(mean_inteiros_sem_truncar, "mean de inteiros não trunca: {1, 2} -> 1.5") {
  CHECK_NEAR(gq::mean(std::vector<int>{1, 2}), 1.5, 1e-12);
  CHECK_NEAR(gq::mean(std::array<long long, 3>{1, 2, 4}), 7.0 / 3.0, 1e-12);
}

TEST(mean_negativos, "mean de inteiros negativos: {-3, -2} -> -2.5") {
  CHECK_NEAR(gq::mean(std::vector<int>{-3, -2}), -2.5, 1e-12);
}

TEST(mean_sem_overflow, "mean não estoura o tipo do elemento: {INT_MAX, INT_MAX} -> INT_MAX") {
  CHECK_NEAR(gq::mean(std::vector<int>{INT_MAX, INT_MAX}), static_cast<double>(INT_MAX), 1e-6);
}

TEST(mean_outros_ranges, "mean aceita list, span e float") {
  CHECK_NEAR(gq::mean(std::list<float>{1.5F, 2.5F}), 2.0, 1e-12);
  const std::vector<unsigned> raw{10, 20, 40};
  CHECK_NEAR(gq::mean(std::span<const unsigned>(raw)), 70.0 / 3.0, 1e-12);
}

TEST(mean_vazio_double, "mean de range vazio (double) lança std::invalid_argument") {
  CHECK_THROWS(gq::mean(std::vector<double>{}), std::invalid_argument);
}

TEST(mean_vazio_int, "mean de range vazio (int) lança std::invalid_argument") {
  CHECK_THROWS(gq::mean(std::vector<int>{}), std::invalid_argument);
}

// ---------------------------------------------------------------- clamp_all

TEST(clamp_all_basico, "clamp_all limita ao intervalo [lo, hi]") {
  std::vector<int> v{-5, 3, 12};
  gq::clamp_all(v, 0, 10);
  CHECK(v == (std::vector<int>{0, 3, 10}));
}

TEST(clamp_all_limites_inteiros_em_double, "clamp_all(vector<double>, 0, 1) funciona") {
  std::vector<double> v{-0.5, 0.25, 1.5};
  gq::clamp_all(v, 0, 1);
  CHECK(v == (std::vector<double>{0.0, 0.25, 1.0}));
}

TEST(clamp_all_limites_invertidos, "clamp_all com lo > hi lança std::invalid_argument e não altera nada") {
  std::vector<int> v{1, 5, 9};
  CHECK_THROWS(gq::clamp_all(v, 10, 0), std::invalid_argument);
  CHECK(v == (std::vector<int>{1, 5, 9}));
}

// ---------------------------------------------------------------- Histogram

TEST(histogram_conta_por_valor, "Histogram<std::string> conta chaves iguais vindas de buffers diferentes") {
  gq::Histogram<std::string> h;
  const std::string a = "GET /login";
  const std::string b = std::string("GET ") + "/login";
  h.add(a);
  h.add(b);
  h.add("GET /cart", 3);
  CHECK_EQ(h.count("GET /login"), std::size_t{2});
  CHECK_EQ(h.count("GET /cart"), std::size_t{3});
  CHECK_EQ(h.count("GET /nada"), std::size_t{0});
  CHECK_EQ(h.distinct(), std::size_t{2});
  CHECK_EQ(h.total(), std::size_t{5});
}

TEST(histogram_hash_customizado, "Histogram aceita chave com especialização própria de std::hash") {
  gq::Histogram<test_types::Sku> h;
  h.add({"A-1"});
  h.add({"A-1"});
  h.add({"B-7"});
  CHECK_EQ(h.count({"A-1"}), std::size_t{2});
  CHECK_EQ(h.distinct(), std::size_t{2});
}

// ---------------------------------------------------------------- serialize

TEST(serialize_inteiros, "serialize de inteiros") {
  CHECK_EQ(gq::serialize(42), std::string("42"));
  CHECK_EQ(gq::serialize(-7), std::string("-7"));
  CHECK_EQ(gq::serialize(std::numeric_limits<std::uint64_t>::max()),
           std::string("18446744073709551615"));
}

TEST(serialize_double_ida_e_volta, "serialize de double preserva o valor e usa a forma mais curta") {
  CHECK_EQ(gq::serialize(0.1), std::string("0.1"));
  CHECK_EQ(gq::serialize(2.5), std::string("2.5"));
  CHECK_EQ(gq::serialize(3.141592653589793), std::string("3.141592653589793"));
  for (const double x : {1.0 / 3.0, 6.02214076e23, -2.5e-8, 123456.789}) {
    CHECK_EQ(std::stod(gq::serialize(x)), x);
  }
}

TEST(serialize_bool, "serialize de bool produz true/false") {
  CHECK_EQ(gq::serialize(true), std::string("true"));
  CHECK_EQ(gq::serialize(false), std::string("false"));
}

TEST(serialize_texto, "serialize de texto produz string entre aspas, escapando \" e \\") {
  CHECK_EQ(gq::serialize(std::string("GET /")), std::string("\"GET /\""));
  CHECK_EQ(gq::serialize(std::string_view("ok")), std::string("\"ok\""));
  CHECK_EQ(gq::serialize("42"), std::string("\"42\""));
  CHECK_EQ(gq::serialize(std::string("diz \"oi\" \\o/")), std::string("\"diz \\\"oi\\\" \\\\o/\""));
}

// ---------------------------------------------------------------- Registry

TEST(registry_snapshot_preserva, "Registry::snapshot pode ser chamado várias vezes sem esvaziar o registro") {
  gq::Registry<std::string> r;
  r.add("region", "sa-east-1");
  r.add("version", "2.4.1");
  const auto first = r.snapshot();
  const auto second = r.snapshot();
  CHECK_EQ(first.size(), std::size_t{2});
  CHECK(first == second);
  CHECK_EQ(second.at(0).second, std::string("sa-east-1"));
  CHECK(r.find("region") != nullptr);
  if (const auto* region = r.find("region")) CHECK_EQ(*region, std::string("sa-east-1"));
}

TEST(registry_move_only, "Registry continua aceitando tipos move-only em add/find") {
  gq::Registry<std::unique_ptr<int>> r;
  r.add("answer", std::make_unique<int>(42));
  CHECK_EQ(r.size(), std::size_t{1});
  const auto* p = r.find("answer");
  CHECK(p != nullptr && *p != nullptr && **p == 42);
  CHECK(r.find("missing") == nullptr);
}

int main(int argc, char** argv) { return minitest::main(argc, argv); }
