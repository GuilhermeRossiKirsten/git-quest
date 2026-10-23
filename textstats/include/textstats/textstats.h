// textstats — estatísticas de palavras para textos em UTF-8 (API pública).
//
// Este é o ÚNICO header que os usuários da biblioteca incluem:
//
//     #include <textstats/textstats.h>
//
// A API usa std::span, portanto quem a inclui precisa compilar em C++20 ou superior.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace textstats {

struct WordCount {
    std::string word;
    std::size_t count = 0;

    friend bool operator==(const WordCount&, const WordCount&) = default;
};

struct Summary {
    std::size_t lines = 0;         // linhas não vazias
    std::size_t words = 0;         // total de palavras
    std::size_t unique_words = 0;  // palavras distintas (após normalização)
    std::size_t letters = 0;       // letras (code points), sem contar pontuação
};

// Acumula estatísticas de vários trechos de texto. Palavras são sequências de
// letras/dígitos (ASCII e Latin-1), comparadas sem diferenciar maiúsculas.
class Analyzer {
public:
    Analyzer();
    ~Analyzer();
    Analyzer(Analyzer&&) noexcept;
    Analyzer& operator=(Analyzer&&) noexcept;
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;

    void feed(std::string_view text);
    [[nodiscard]] Summary summary() const;

    // As n palavras mais frequentes; empates em ordem alfabética (bytes UTF-8).
    [[nodiscard]] std::vector<WordCount> top(std::size_t n) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Atalho: as n palavras mais frequentes somando vários documentos.
[[nodiscard]] std::vector<WordCount> top_words(std::span<const std::string_view> documents,
                                               std::size_t n);

// Versão da biblioteca ("MAJOR.MINOR.PATCH") e o carimbo do build.
[[nodiscard]] const char* version() noexcept;
[[nodiscard]] const char* build_stamp() noexcept;

}  // namespace textstats
