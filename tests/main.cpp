// main.cpp — ponto de entrada de cada binário de testes (um por arquivo tests/test_*.cpp).
//
//   build/bin/test_02_basico --list     lista "nome<TAB>descrição"
//   build/bin/test_02_basico <nome>     executa um teste; código 0 = passou
#include <cstring>
#include <exception>
#include <iostream>
#include <vector>

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
        for (const gq::TestCase& t : tests) {
            std::cout << t.name << '\t' << t.description << '\n';
        }
        return 0;
    }
    if (argc != 2) {
        std::cerr << "uso: " << argv[0] << " --list | <nome-do-teste>\n";
        return 2;
    }
    for (const gq::TestCase& t : tests) {
        if (std::strcmp(argv[1], t.name) != 0) {
            continue;
        }
        try {
            t.fn();
        } catch (const gq::Failure& f) {
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
