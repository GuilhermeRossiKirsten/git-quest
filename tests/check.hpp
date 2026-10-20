// check.hpp — mini framework de testes do desafio (sem dependências externas).
//
//   TEST("grupo", "descrição") { CHECK_EQ(a, b); }
//
// O executável recebe o nome de um grupo e roda apenas os casos dele, para que
// um erro fatal (ex.: relatório do AddressSanitizer) afete só aquele grupo.
#pragma once

#include <cxxabi.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <variant>
#include <vector>

namespace check {

struct Failure {
    std::string message;
};

struct Case {
    const char* group;
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* group, const char* name, void (*fn)()) {
        registry().push_back({group, name, fn});
    }
};

// ---- formatação de valores para mensagens de erro --------------------------

template <typename T>
std::string show(const T& v);

inline std::string show_one(std::string_view s) { return "\"" + std::string(s) + "\""; }
inline std::string show_one(const std::string& s) { return show_one(std::string_view(s)); }
inline std::string show_one(const char* s) { return show_one(std::string_view(s)); }
inline std::string show_one(bool b) { return b ? "true" : "false"; }
inline std::string show_one(std::monostate) { return "std::monostate"; }
inline std::string show_one(long v) { return std::to_string(v) + "L"; }
inline std::string show_one(int v) { return std::to_string(v); }
inline std::string show_one(std::size_t v) { return std::to_string(v); }
inline std::string show_one(double v) {
    std::ostringstream out;
    out.precision(17);
    out << v;
    return out.str();
}
inline std::string show_one(std::nullopt_t) { return "std::nullopt"; }

template <typename T>
std::string show_one(const std::optional<T>& o) {
    return o ? "optional(" + show(*o) + ")" : std::string("std::nullopt");
}

template <typename... Ts>
std::string show_one(const std::variant<Ts...>& v) {
    static const char* const names[] = {"monostate", "long", "double", "bool", "string"};
    const std::size_t i = v.index();
    const std::string kind = i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
    return std::visit([&](const auto& x) { return kind + "{" + show(x) + "}"; }, v);
}

template <typename T>
std::string show(const T& v) {
    return show_one(v);
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)

#define TEST(group, name)                                                         \
    static void CHECK_CAT(test_fn_, __LINE__)();                                  \
    static const ::check::Registrar CHECK_CAT(test_reg_, __LINE__){               \
        group, name, &CHECK_CAT(test_fn_, __LINE__)};                             \
    static void CHECK_CAT(test_fn_, __LINE__)()

#define CHECK_EQ(actual, expected)                                                \
    do {                                                                          \
        const auto check_a_ = (actual);                                           \
        const auto check_e_ = (expected);                                         \
        if (!(check_a_ == check_e_)) {                                            \
            throw ::check::Failure{std::string(#actual) + " devolveu " +          \
                                   ::check::show(check_a_) + ", esperado " +      \
                                   ::check::show(check_e_)};                      \
        }                                                                         \
    } while (0)

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) throw ::check::Failure{"falhou: " #cond};                    \
    } while (0)

namespace check {

inline std::string type_name(const std::type_info& t) {
    int status = 0;
    char* pretty = abi::__cxa_demangle(t.name(), nullptr, nullptr, &status);
    std::string name = status == 0 && pretty ? pretty : t.name();
    std::free(pretty);
    return name;
}

inline int list_group(std::string_view group) {
    for (const Case& c : registry()) {
        if (group == c.group) std::cout << c.name << "\n";
    }
    return 0;
}

inline int run_group(std::string_view group) {
    int total = 0;
    int failed = 0;
    for (const Case& c : registry()) {
        if (group != c.group) continue;
        ++total;
        std::string error;
        try {
            c.fn();
        } catch (const Failure& f) {
            error = f.message;
        } catch (const std::exception& e) {
            error = "lançou exceção " + type_name(typeid(e)) + " (what: " + e.what() + ")";
        } catch (...) {
            error = "lançou uma exceção desconhecida";
        }
        if (error.empty()) {
            std::cout << "    ✔ " << c.name << "\n";
        } else {
            ++failed;
            std::cout << "    ✘ " << c.name << "\n        " << error << "\n";
        }
        std::cout.flush();
    }
    if (total == 0) {
        std::cout << "    ✘ grupo desconhecido: " << group << "\n";
        return 2;
    }
    return failed == 0 ? 0 : 1;
}

}  // namespace check
