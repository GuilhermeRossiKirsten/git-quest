// check.h — mini framework de testes do desafio (sem dependências externas).
//
// Cada teste roda em um processo separado (veja tests/run.sh), em até três
// variantes: AddressSanitizer + UBSan + LeakSanitizer, ThreadSanitizer e um
// build otimizado sem sanitizers (só para a medição informativa de desempenho).
//
// Uma verificação que falha encerra o processo NA HORA (std::_Exit): num teste
// com duas threads girando, desempilhar a pilha poderia esperar para sempre.
// Um watchdog encerra o teste se ele passar do prazo e informa a etapa (STEP).
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
        registry().push_back(TestCase{name, description, fn});
    }
};

// Imprime "FALHA: arquivo:linha: mensagem" e encerra o processo imediatamente.
[[noreturn]] void fail(const char* file, int line, const std::string& message);

// Etapa atual do teste; o watchdog a menciona se o teste travar.
void step(const char* what);

// Linha informativa que tests/run.sh repete na saída (ex.: medições).
void note(const std::string& text);

// true quando o binário foi compilado com ThreadSanitizer (cargas menores).
constexpr bool kThreadSanitizer =
#if defined(__SANITIZE_THREAD__)
    true;
#else
    false;
#endif

}  // namespace gq

#define GQ_CONCAT_(a, b) a##b
#define GQ_CONCAT(a, b) GQ_CONCAT_(a, b)

// TEST(nome, "descrição em português") { ... }
#define TEST(name, description)                                                    \
    static void name();                                                            \
    static ::gq::Registrar GQ_CONCAT(name, _registrar)(#name, description, &name); \
    static void name()

// CHECK(condição, mensagem << com << operador <<)
#define CHECK(cond, msg)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            std::ostringstream gq_os_;                    \
            gq_os_ << msg;                                \
            ::gq::fail(__FILE__, __LINE__, gq_os_.str()); \
        }                                                 \
    } while (0)

// CHECK_THROWS(expressão, TipoDaExceção, mensagem)
#define CHECK_THROWS(expr, ExType, msg)                                    \
    do {                                                                   \
        bool gq_thrown_ = false;                                           \
        try {                                                              \
            (void)(expr);                                                  \
        } catch (const ExType&) {                                          \
            gq_thrown_ = true;                                             \
        }                                                                  \
        CHECK(gq_thrown_, msg << " (esperava " #ExType " de: " #expr ")"); \
    } while (0)

#endif
