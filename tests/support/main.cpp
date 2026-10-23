// Ponto de entrada comum de core_tests e plugin_tests.
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

#include "support/minitest.hpp"

int minitest::main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--list") {
    for (const auto& c : cases()) std::printf("%s\t%s\n", c.id, c.title);
    return 0;
  }
  if (argc != 2) {
    std::printf("uso: %s --list | <id-do-teste>\n", argv[0]);
    return 2;
  }
  for (const auto& c : cases()) {
    if (std::string_view(argv[1]) != c.id) continue;
    try {
      c.fn();
    } catch (const std::exception& e) {
      fail(0, std::string("exceção inesperada: ") + e.what());
    } catch (...) {
      fail(0, "exceção inesperada de tipo desconhecido");
    }
    return failures() == 0 ? 0 : 1;
  }
  std::printf("teste desconhecido: %s\n", argv[1]);
  return 2;
}

int main(int argc, char** argv) { return minitest::main(argc, argv); }
