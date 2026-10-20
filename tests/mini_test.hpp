// Mini framework de testes do desafio (sem dependências externas).
//
//   TEST(nome) { CHECK(cond); CHECK_EQ(a, b); }
//
// O executável aceita:
//   --list   lista os testes registrados, um por linha
//   <nome>   executa só esse teste (tests/run.sh roda cada um com timeout próprio)
//   (nada)   executa todos
#pragma once

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gqtest {

struct Failure {
    std::string message;
};

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <class T, class = void>
struct Printable : std::false_type {};
template <class T>
struct Printable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T>
std::string show(const T& v) {
    if constexpr (Printable<T>::value) {
        std::ostringstream out;
        out << v;
        return out.str();
    } else {
        return "<valor>";
    }
}

[[noreturn]] inline void fail(const char* file, int line, const std::string& what) {
    std::ostringstream out;
    out << file << ":" << line << ": " << what;
    throw Failure{out.str()};
}

inline int run_case(const Case& c) {
    try {
        c.fn();
    } catch (const Failure& f) {
        std::cout << "FALHA " << f.message << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cout << "FALHA exceção inesperada: " << e.what() << "\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}

inline int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--list") {
        for (const Case& c : registry()) {
            std::cout << c.name << "\n";
        }
        return 0;
    }
    int failures = 0;
    bool found = false;
    for (const Case& c : registry()) {
        if (argc > 1 && argv[1] != std::string(c.name)) {
            continue;
        }
        found = true;
        if (argc == 1) {
            std::cout << c.name << ": " << std::flush;
        }
        failures += run_case(c);
    }
    if (!found) {
        std::cout << "teste desconhecido: " << argv[1] << "\n";
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace gqtest

#define GQ_CAT2(a, b) a##b
#define GQ_CAT(a, b) GQ_CAT2(a, b)

#define TEST(name)                                                          \
    static void name();                                                     \
    static const ::gqtest::Registrar GQ_CAT(gq_reg_, name)(#name, &name);   \
    static void name()

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) ::gqtest::fail(__FILE__, __LINE__, "esperado: " #cond); \
    } while (0)

#define CHECK_MSG(cond, msg)                                                \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::ostringstream gq_msg_;                                     \
            gq_msg_ << msg;                                                 \
            ::gqtest::fail(__FILE__, __LINE__, gq_msg_.str());              \
        }                                                                   \
    } while (0)

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        const auto& gq_a_ = (actual);                                       \
        const auto& gq_e_ = (expected);                                     \
        if (!(gq_a_ == gq_e_)) {                                            \
            ::gqtest::fail(__FILE__, __LINE__,                              \
                           std::string(#actual " == " #expected ": obtido ") + \
                               ::gqtest::show(gq_a_) + ", esperado " +      \
                               ::gqtest::show(gq_e_));                      \
        }                                                                   \
    } while (0)
