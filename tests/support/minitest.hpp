#pragma once
// Mini framework de testes do desafio. Cada teste roda em um processo
// separado (tests/run.sh chama o binário com o id do teste), assim uma falha
// grave — divisão por zero, erro do sanitizer — derruba só aquele teste.
//
//   ./runtime_tests --list     lista "id<TAB>descrição"
//   ./runtime_tests <id>       executa um teste; código 0 = passou

#include <cstdio>
#include <exception>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace minitest {

struct Case {
  const char* id;
  const char* title;
  void (*fn)();
};

inline std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

struct Registrar {
  Registrar(const char* id, const char* title, void (*fn)()) { cases().push_back({id, title, fn}); }
};

inline int& failures() {
  static int count = 0;
  return count;
}

inline void fail(int line, const std::string& message) {
  std::printf("linha %d: %s\n", line, message.c_str());
  ++failures();
}

template <class T>
std::string show(const T& value) {
  if constexpr (requires(std::ostream& os) { os << value; }) {
    std::ostringstream os;
    os << std::setprecision(17) << std::boolalpha << value;
    return os.str();
  } else {
    return "<valor>";
  }
}

inline int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--list") {
    for (const auto& c : cases()) std::printf("%s\t%s\n", c.id, c.title);
    return 0;
  }
  if (argc != 2) {
    std::printf("uso: %s --list | <id-do-teste>\n", argv[0]);
    return 2;
  }
  for (const auto& c : cases()) {
    if (std::string_view(argv[1]) != c.id) continue;
    try {
      c.fn();
    } catch (const std::exception& e) {
      fail(0, std::string("exceção inesperada: ") + e.what());
    } catch (...) {
      fail(0, "exceção inesperada de tipo desconhecido");
    }
    return failures() == 0 ? 0 : 1;
  }
  std::printf("teste desconhecido: %s\n", argv[1]);
  return 2;
}

}  // namespace minitest

#define TEST(id, title)                                         \
  static void id();                                             \
  static const ::minitest::Registrar id##_registrar{#id, title, &id}; \
  static void id()

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) ::minitest::fail(__LINE__, "falhou: " #cond); \
  } while (0)

#define CHECK_EQ(actual, expected)                                                   \
  do {                                                                               \
    const auto& gq_a_ = (actual);                                                    \
    const auto& gq_e_ = (expected);                                                  \
    if (!(gq_a_ == gq_e_))                                                           \
      ::minitest::fail(__LINE__, std::string(#actual) + ": esperado " +              \
                                     ::minitest::show(gq_e_) + ", obtido " +         \
                                     ::minitest::show(gq_a_));                       \
  } while (0)

#define CHECK_NEAR(actual, expected, eps)                                            \
  do {                                                                               \
    const double gq_a_ = static_cast<double>(actual);                                \
    const double gq_e_ = static_cast<double>(expected);                              \
    if (!(gq_a_ - gq_e_ <= (eps) && gq_e_ - gq_a_ <= (eps)))                         \
      ::minitest::fail(__LINE__, std::string(#actual) + ": esperado " +              \
                                     ::minitest::show(gq_e_) + ", obtido " +         \
                                     ::minitest::show(gq_a_));                       \
  } while (0)

#define CHECK_THROWS(expr, Exception)                                                \
  do {                                                                               \
    try {                                                                            \
      static_cast<void>(expr);                                                       \
      ::minitest::fail(__LINE__, #expr ": esperado lançar " #Exception ", mas retornou normalmente"); \
    } catch (const Exception&) {                                                     \
    } catch (...) {                                                                  \
      ::minitest::fail(__LINE__, #expr ": lançou uma exceção que não é " #Exception); \
    }                                                                                \
  } while (0)
