// Tokenizador interno da textstats. NÃO faz parte da API pública: pode mudar a
// qualquer momento e não deve ser visível para quem usa a biblioteca instalada.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace textstats::detail {

struct Token {
    std::string word;          // normalizada (minúsculas)
    std::size_t letters = 0;   // code points de letra/dígito
};

// Percorre text palavra a palavra e conta as linhas não vazias pelo caminho.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) : text_(text) {}

    // Próxima palavra; false quando o texto acabou.
    bool next(Token& out);

    [[nodiscard]] std::size_t non_empty_lines() const { return lines_; }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t lines_ = 0;
    bool line_has_content_ = false;
};

}  // namespace textstats::detail
