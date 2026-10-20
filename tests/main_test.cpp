// Executor dos testes comportamentais: ./tests <grupo>
#include "check.hpp"
#include "support.hpp"

#include <cstdio>
#include <exception>
#include <print>
#include <typeinfo>

#ifndef GQ_SANITIZE
#include <dlfcn.h>
#endif

namespace gq {

namespace {
std::size_t g_failures = 0;
}

std::size_t g_exceptions_allocated = 0;

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void report_failure(std::string_view file, int line, std::string message) {
    ++g_failures;
    auto slash = file.find_last_of('/');
    std::println("      {}:{}: {}", file.substr(slash == std::string_view::npos ? 0 : slash + 1), line, message);
}

}  // namespace gq

#ifndef GQ_SANITIZE
// Conta quantas exceções o programa cria (inclusive as lançadas dentro da
// biblioteca padrão, como as de std::stoi). Usado pelo grupo "exceptions":
// entrada malformada é um caso esperado, não algo excepcional.
namespace __cxxabiv1 {
extern "C" void* __cxa_allocate_exception(std::size_t size) noexcept {
    ++gq::g_exceptions_allocated;
    using fn_t = void* (*)(std::size_t) noexcept;
    static auto real = reinterpret_cast<fn_t>(dlsym(RTLD_NEXT, "__cxa_allocate_exception"));
    return real(size);
}
}  // namespace __cxxabiv1
#endif

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println(stderr, "uso: {} <grupo>", argv[0]);
        return 2;
    }
    std::string_view group = argv[1];
    std::size_t ran = 0, failed = 0;
    for (const auto& test : gq::registry()) {
        if (test.group != group) {
            continue;
        }
        ++ran;
        const auto before = gq::g_failures;
        std::println("  - {}", test.name);
        try {
            test.body();
        } catch (const std::exception& e) {
            gq::report_failure("teste", 0, std::format("exceção escapou: {} ({})", e.what(), typeid(e).name()));
        } catch (...) {
            gq::report_failure("teste", 0, "exceção desconhecida escapou");
        }
        if (gq::g_failures != before) {
            ++failed;
            std::println("    FALHOU");
        }
    }
    if (ran == 0) {
        std::println(stderr, "grupo desconhecido: {}", group);
        return 2;
    }
    std::println("{} de {} testes passaram no grupo '{}'", ran - failed, ran, group);
    return failed == 0 ? 0 : 1;
}
