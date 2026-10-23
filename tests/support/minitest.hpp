#pragma once
// Mini framework de testes do desafio. Cada teste roda em um processo
// separado (tests/run.sh chama o binário com o id do teste), assim uma falha
// grave — erro do sanitizer, abort — derruba só aquele teste.
//
//   ./core_tests --list     lista "id<TAB>descrição"
//   ./core_tests <id>       executa um teste; código 0 = passou

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
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
  std::printf("FALHA: linha %d: %s\n", line, message.c_str());
  std::fflush(stdout);
  ++failures();
}

template <class T>
std::string show(const T& value) {
  if constexpr (requires(std::ostream& os) { os << value; }) {
    std::ostringstream os;
    os << std::boolalpha << value;
    std::string text = os.str();
    // Strings com várias linhas (cupons) ficam legíveis no relatório.
    std::string out;
    for (char c : text) out += (c == '\n') ? std::string("\\n") : std::string(1, c);
    return "\"" + out + "\"";
  } else {
    return "<valor>";
  }
}

int main(int argc, char** argv);

}  // namespace minitest

#define TEST(id, title)                                               \
  static void id();                                                   \
  static const ::minitest::Registrar id##_registrar{#id, title, &id}; \
  static void id()

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) ::minitest::fail(__LINE__, "falhou: " #cond);     \
  } while (0)

#define CHECK_EQ(actual, expected)                                                               \
  do {                                                                                           \
    const auto& gq_a_ = (actual);                                                                \
    const auto& gq_e_ = (expected);                                                              \
    if (!(gq_a_ == gq_e_))                                                                       \
      ::minitest::fail(__LINE__, std::string(#actual) + ": esperado " + ::minitest::show(gq_e_) + \
                                     ", obtido " + ::minitest::show(gq_a_));                    \
  } while (0)

#define CHECK_LE(actual, limit)                                                                  \
  do {                                                                                           \
    const auto gq_a_ = (actual);                                                                 \
    const auto gq_l_ = (limit);                                                                  \
    if (!(gq_a_ <= gq_l_))                                                                       \
      ::minitest::fail(__LINE__, std::string(#actual) + ": esperado no máximo " +               \
                                     std::to_string(gq_l_) + ", obtido " + std::to_string(gq_a_)); \
  } while (0)
