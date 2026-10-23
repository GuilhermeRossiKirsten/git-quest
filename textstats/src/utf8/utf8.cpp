#include "utf8.h"

namespace textstats::utf8 {

namespace {

// Comprimento da sequência a partir do primeiro byte (0 = byte inválido como início).
constexpr unsigned char kSequenceLength[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xxxxxxx
    0, 0, 0, 0, 0, 0, 0, 0,                          // 10xxxxxx (continuação)
    2, 2, 2, 2,                                      // 110xxxxx
    3, 3,                                            // 1110xxxx
    4,                                               // 11110xxx
    0,                                               // 11111xxx
};

bool is_continuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

}  // namespace

char32_t decode(std::string_view text, std::size_t& pos) {
    const auto lead = static_cast<unsigned char>(text[pos]);
    const unsigned length = kSequenceLength[lead >> 3];
    if (length == 0 || pos + length > text.size()) {
        ++pos;
        return replacement;
    }
    if (length == 1) {
        ++pos;
        return lead;
    }
    char32_t cp = lead & (0x7Fu >> length);
    for (unsigned i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[pos + i]);
        if (!is_continuation(byte)) {
            ++pos;
            return replacement;
        }
        cp = (cp << 6) | (byte & 0x3Fu);
    }
    static constexpr char32_t kMinimum[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMinimum[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;  // forma "overlong", fora do intervalo ou surrogate
        return replacement;
    }
    pos += length;
    return cp;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool is_word_char(char32_t cp) {
    if (cp < 0x80) {
        return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || (cp >= U'0' && cp <= U'9');
    }
    return cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7;
}

char32_t to_lower(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    return cp;
}

}  // namespace textstats::utf8
