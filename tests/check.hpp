// check.hpp — mini framework de testes do desafio (sem dependências externas).
//
//   GQ_TEST("grupo", "nome") { GQ_CHECK(cond); GQ_CHECK_EQ(atual, esperado); }
//
// O executável recebe o grupo a rodar (parse, analyze, report, exceptions) e
// termina com código 0 somente se todos os testes do grupo passarem.
#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gq {

struct TestCase {
    std::string_view group;
    std::string_view name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();
void report_failure(std::string_view file, int line, std::string message);

struct Registrar {
    Registrar(std::string_view group, std::string_view name, std::function<void()> body) {
        registry().push_back({group, name, std::move(body)});
    }
};

// Representação legível de um valor, para mensagens de falha.
template <class T>
std::string show(const T& value) {
    if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        std::string out = "\"";
        for (unsigned char c : std::string_view(value)) {
            if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) out += std::format("\\x{:02x}", c);
            else out += static_cast<char>(c);
        }
        return out + "\"";
    } else if constexpr (std::is_enum_v<T>) {
        return std::format("{}", static_cast<long long>(value));
    } else if constexpr (requires { std::format("{}", value); }) {
        return std::format("{}", value);
    } else {
        return "<valor>";
    }
}

}  // namespace gq

#define GQ_CONCAT_(a, b) a##b
#define GQ_CONCAT(a, b) GQ_CONCAT_(a, b)
#define GQ_TEST(group, name)                                                        \
    static void GQ_CONCAT(gq_test_, __LINE__)();                                    \
    static const ::gq::Registrar GQ_CONCAT(gq_reg_, __LINE__)(group, name,          \
                                                              GQ_CONCAT(gq_test_, __LINE__)); \
    static void GQ_CONCAT(gq_test_, __LINE__)()

#define GQ_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) ::gq::report_failure(__FILE__, __LINE__, "esperado: " #cond);  \
    } while (0)

#define GQ_CHECK_EQ(actual, expected)                                               \
    do {                                                                            \
        const auto& gq_a_ = (actual);                                               \
        const auto& gq_e_ = (expected);                                             \
        if (!(gq_a_ == gq_e_))                                                      \
            ::gq::report_failure(__FILE__, __LINE__,                                \
                                 std::format("{}\n        obtido:   {}\n        esperado: {}", \
                                             #actual, ::gq::show(gq_a_), ::gq::show(gq_e_))); \
    } while (0)
