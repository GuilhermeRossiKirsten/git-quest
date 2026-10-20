// main.cpp — ponto de entrada do binário de testes.
//
//   ./build/test-runner --list     lista "nome<TAB>descrição"
//   ./build/test-runner <nome>     executa um teste; código 0 = passou
#include <cstring>
#include <exception>
#include <iostream>

#include "check.h"

namespace gq {
std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}
}  // namespace gq

int main(int argc, char** argv) {
    const std::vector<gq::TestCase>& tests = gq::registry();
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (std::size_t i = 0; i < tests.size(); ++i) {
            std::cout << tests[i].name << '\t' << tests[i].description << '\n';
        }
        return 0;
    }
    if (argc != 2) {
        std::cerr << "uso: " << argv[0] << " --list | <nome-do-teste>\n";
        return 2;
    }
    for (std::size_t i = 0; i < tests.size(); ++i) {
        if (std::strcmp(argv[1], tests[i].name) != 0) {
            continue;
        }
        try {
            tests[i].fn();
        } catch (const gq::Failure& f) {
            // std::endl: o LeakSanitizer encerra o processo sem esvaziar o buffer.
            std::cout << "FALHA: " << f.message << std::endl;
            return 1;
        } catch (const std::exception& e) {
            std::cout << "FALHA: exceção inesperada: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "OK" << std::endl;
        return 0;
    }
    std::cerr << "teste desconhecido: " << argv[1] << '\n';
    return 2;
}
