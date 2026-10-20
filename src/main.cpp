// main.cpp — ponto de entrada do serviço "relay".
//
// Lê o arquivo de configuração, aplica os padrões documentados e imprime a
// configuração efetiva. Este arquivo é cliente da API de config.hpp e já está
// correto: ele só confia no que a API promete.
#include "config.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

namespace {

std::string describe(const cfg::Value& v) {
    return std::visit(
        [](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "(ausente)";
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::string("bool ") + (x ? "true" : "false");
            } else if constexpr (std::is_same_v<T, long>) {
                return "long " + std::to_string(x);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;
                out << "double " << x;
                return out.str();
            } else {
                return "texto \"" + x + "\"";
            }
        },
        v);
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;  // cada linha sai imediatamente (logs do serviço)
    const char* path = argc > 1 ? argv[1] : "examples/relay.ini";
    std::ifstream file(path);
    if (!file) {
        std::cerr << "relay: não foi possível abrir " << path << "\n";
        return 2;
    }
    std::ostringstream text;
    text << file.rdbuf();
    const cfg::Config config = cfg::Config::parse(text.str());

    // Padrões documentados: valem SOMENTE quando a chave não existe.
    const std::string_view host = config.get("server.host").value_or("0.0.0.0");
    const long port = config.get_int("server.port").value_or(8080);
    const long retries = config.get_int("server.retries").value_or(3);
    const double sample = config.get_double("metrics.sample_rate").value_or(1.0);
    const bool debug = config.get_bool("server.debug").value_or(false);
    const std::string_view banner = config.get("server.banner").value_or("Bem-vindo ao relay");

    std::cout << "host        = " << host << "\n"
              << "port        = " << port << "\n"
              << "retries     = " << retries << (retries == -1 ? " (sem limite)" : "") << "\n"
              << "sample_rate = " << sample << "\n"
              << "debug       = " << (debug ? "true" : "false") << "\n"
              << "banner      = \"" << banner << "\"\n";

    std::cout << "\n[database]\n";
    for (const auto& [key, value] : config.entries("database")) {
        std::cout << "  " << key << " -> " << describe(config.typed("database." + std::string(key)))
                  << "  (texto: \"" << value << "\")\n";
    }
    return 0;
}
