// Mini framework de testes do avaliador: cada arquivo tests/test_*.cpp é um
// executável; cada CHECK que falha imprime uma linha "FALHA: ..." e o
// processo termina com código 1 se houve qualquer falha.
#ifndef GQ_TESTS_CHECK_HPP
#define GQ_TESTS_CHECK_HPP

#include <cstdio>
#include <sstream>
#include <string>

namespace check {

inline int& failures() {
    static int n = 0;
    return n;
}

template <class T>
std::string show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string show(const std::string& value) { return "\"" + value + "\""; }
inline std::string show(const char* value) { return show(std::string(value)); }
inline std::string show(bool value) { return value ? "true" : "false"; }

inline void fail(const std::string& what) {
    ++failures();
    std::printf("FALHA: %s\n", what.c_str());
    std::fflush(stdout);
}

inline int result() { return failures() == 0 ? 0 : 1; }

}  // namespace check

// CHECK(condição, "mensagem para o participante")
#define CHECK(cond, msg)                  \
    do {                                  \
        if (!(cond)) check::fail(msg);    \
    } while (0)

// CHECK_EQ(obtido, esperado, "contexto")
#define CHECK_EQ(got, want, msg)                                                       \
    do {                                                                               \
        const auto& gq_got_ = (got);                                                   \
        const auto& gq_want_ = (want);                                                 \
        if (!(gq_got_ == gq_want_))                                                    \
            check::fail(std::string(msg) + ": esperado " + check::show(gq_want_) +     \
                        ", obtido " + check::show(gq_got_));                           \
    } while (0)

#endif  // GQ_TESTS_CHECK_HPP
