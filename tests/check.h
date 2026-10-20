// check.h — mini framework de testes do desafio (sem dependências externas).
//
// Cada teste roda em um processo separado (veja tests/run.sh), duas vezes: uma
// com AddressSanitizer + UBSan e outra com ThreadSanitizer.
//
// Diferente de um framework comum, uma verificação que falha encerra o processo
// NA HORA (std::_Exit): num teste de concorrência, desempilhar a pilha poderia
// esperar para sempre por threads travadas. Um watchdog encerra o teste se ele
// passar do prazo e informa a etapa em que travou (veja STEP).
#ifndef GITQUEST_CHECK_H
#define GITQUEST_CHECK_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
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

// Quantas threads o processo tem agora (/proc/self/task).
std::size_t thread_count();

// Espera até `limit` o número de threads voltar a `baseline`. Devolve o último valor lido.
std::size_t wait_thread_count(std::size_t baseline, std::chrono::milliseconds limit);

// Espera até `limit` a condição ficar verdadeira (consulta a cada 1 ms).
bool wait_until(const std::function<bool()>& cond, std::chrono::milliseconds limit);

// Milissegundos (com fração) gastos por fn().
double elapsed_ms(const std::function<void()>& fn);

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

// Quantas threads o processo pode ter a mais depois que um objeto foi destruído: nenhuma.
#define CHECK_NO_THREADS_LEFT(baseline, when)                                                     \
    do {                                                                                          \
        const std::size_t gq_now_ = ::gq::wait_thread_count((baseline), std::chrono::milliseconds(200)); \
        CHECK(gq_now_ == (baseline), when << ": o processo ainda tem " << (gq_now_ - (baseline))  \
                                          << " thread(s) a mais do que antes (threads que "       \
                                             "continuam vivas depois do fim do objeto)");         \
    } while (0)

#endif
