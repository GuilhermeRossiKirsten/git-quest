// check.h — mini framework de testes do desafio (sem dependências externas).
//
// Cada teste roda em um processo separado (veja tests/run.sh), para que um
// double free ou um vazamento detectado pelos sanitizers seja atribuído ao
// teste certo e não derrube os demais.
#ifndef GITQUEST_CHECK_H
#define GITQUEST_CHECK_H

#include <sstream>
#include <string>
#include <vector>

namespace gq {

struct TestCase {
    const char* name;
    const char* description;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, const char* description, void (*fn)()) {
        TestCase tc = {name, description, fn};
        registry().push_back(tc);
    }
};

// Lançada por CHECK quando uma verificação falha.
struct Failure {
    std::string message;
};

inline void fail(const char* file, int line, const std::string& message) {
    std::ostringstream os;
    os << file << ":" << line << ": " << message;
    Failure f = {os.str()};
    throw f;
}

}  // namespace gq

#define GQ_CONCAT_(a, b) a##b
#define GQ_CONCAT(a, b) GQ_CONCAT_(a, b)

// TEST(nome, "descrição em português") { ... }
#define TEST(name, description)                                                       \
    static void name();                                                               \
    static ::gq::Registrar GQ_CONCAT(name, _registrar)(#name, description, &name);    \
    static void name()

// CHECK(condição, mensagem << com << operador <<)
#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::ostringstream gq_os_;                      \
            gq_os_ << msg;                                  \
            ::gq::fail(__FILE__, __LINE__, gq_os_.str());   \
        }                                                   \
    } while (0)

// CHECK_THROWS(expressão, TipoDaExceção, mensagem)
#define CHECK_THROWS(expr, ExType, msg)                                         \
    do {                                                                        \
        bool gq_thrown_ = false;                                                \
        try {                                                                   \
            (void)(expr);                                                       \
        } catch (const ExType&) {                                               \
            gq_thrown_ = true;                                                  \
        }                                                                       \
        CHECK(gq_thrown_, msg << " (esperava " #ExType " de: " #expr ")");      \
    } while (0)

#endif
