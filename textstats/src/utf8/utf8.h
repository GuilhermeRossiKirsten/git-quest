// Utilitários internos de UTF-8 usados pelo tokenizador. NÃO fazem parte da API pública.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace textstats::utf8 {

inline constexpr char32_t replacement = U'�';

// Decodifica o code point que começa em text[pos] e avança pos.
// Sequências inválidas viram U+FFFD e avançam um único byte.
char32_t decode(std::string_view text, std::size_t& pos);

// Acrescenta cp a out, codificado em UTF-8.
void append(std::string& out, char32_t cp);

// Letras e dígitos ASCII e as letras do bloco Latin-1 (À–ÿ, exceto × e ÷).
bool is_word_char(char32_t cp);

// Minúscula para ASCII e Latin-1; demais code points voltam inalterados.
char32_t to_lower(char32_t cp);

}  // namespace textstats::utf8
