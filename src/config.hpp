// config.hpp — leitor de configuração no formato INI usado pelo serviço "relay".
//
// A interface pública já foi modernizada para C++17. A implementação em
// config.cpp ainda é a porta do parser legado e precisa honrar este contrato
// (veja "Especificação do formato" no README.md).
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cfg {

// Valor de uma chave com o tipo inferido a partir do texto.
// std::monostate significa "a chave não existe".
using Value = std::variant<std::monostate, long, double, bool, std::string>;

class Config {
public:
    // Interpreta o texto completo de um arquivo de configuração.
    static Config parse(std::string_view text);

    // Chaves de seções são acessadas como "secao.chave"; chaves que aparecem
    // antes da primeira seção são acessadas pelo nome puro ("chave").
    //
    // Os std::string_view retornados apontam para dados guardados pelo próprio
    // Config e continuam válidos enquanto ele existir.
    std::optional<std::string_view> get(std::string_view key) const;

    // Conversões: std::nullopt quando a chave não existe OU o texto não
    // representa um valor do tipo pedido. Nunca lançam exceções.
    std::optional<long> get_int(std::string_view key) const;
    std::optional<double> get_double(std::string_view key) const;
    std::optional<bool> get_bool(std::string_view key) const;

    // Valor com o tipo inferido (bool, long, double ou texto).
    Value typed(std::string_view key) const;

    // Pares (chave sem o prefixo da seção, valor) de uma seção, em ordem
    // alfabética de chave. A seção "" contém as chaves globais.
    std::vector<std::pair<std::string_view, std::string_view>>
    entries(std::string_view section) const;

    // Quantidade de chaves distintas.
    std::size_t size() const;

private:
    std::string lookup(std::string_view key) const;  // "" quando ausente

    std::map<std::string, std::string> values_;  // "secao.chave" -> valor
};

}  // namespace cfg
