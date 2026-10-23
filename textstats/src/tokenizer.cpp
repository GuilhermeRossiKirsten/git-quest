#include "tokenizer.h"

#include "utf8.h"

namespace textstats::detail {

bool Tokenizer::next(Token& out) {
    out.word.clear();
    out.letters = 0;
    while (pos_ < text_.size()) {
        if (text_[pos_] == '\n') {
            if (line_has_content_) ++lines_;
            line_has_content_ = false;
            ++pos_;
            if (out.letters > 0) return true;
            continue;
        }
        const std::size_t start = pos_;
        const char32_t cp = utf8::decode(text_, pos_);
        if (cp != U' ' && cp != U'\t' && cp != U'\r') line_has_content_ = true;
        if (utf8::is_word_char(cp)) {
            utf8::append(out.word, utf8::to_lower(cp));
            ++out.letters;
        } else if (out.letters > 0) {
            // Apóstrofo/hífen entre letras continuam a palavra ("d'água", "guarda-chuva").
            const bool joiner = (cp == U'\'' || cp == U'-') && pos_ < text_.size();
            if (joiner) {
                std::size_t look = pos_;
                if (utf8::is_word_char(utf8::decode(text_, look))) {
                    out.word.append(text_.substr(start, pos_ - start));
                    continue;
                }
            }
            return true;
        }
    }
    if (line_has_content_) {
        ++lines_;
        line_has_content_ = false;
    }
    return out.letters > 0;
}

}  // namespace textstats::detail
