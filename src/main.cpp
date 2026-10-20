// loganalyzer — lê um log de acesso (arquivo ou stdin) e imprime o relatório.
//
//   loganalyzer [arquivo]
#include "log_analyzer.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string text;
    if (argc > 1) {
        std::ifstream file(argv[1]);
        std::stringstream buffer;
        buffer << file.rdbuf();
        text = buffer.str();
    } else {
        text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    std::cout << logan::format_report(logan::analyze(text));
    return 0;
}
