#include <textstats/textstats.h>

#include <algorithm>
#include <unordered_map>

#include "config.h"
#include "tokenizer.h"

namespace textstats {

struct Analyzer::Impl {
    std::unordered_map<std::string, std::size_t> counts;
    Summary summary;
};

Analyzer::Analyzer() : impl_(std::make_unique<Impl>()) {}
Analyzer::~Analyzer() = default;
Analyzer::Analyzer(Analyzer&&) noexcept = default;
Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

void Analyzer::feed(std::string_view text) {
    detail::Tokenizer tokenizer(text);
    detail::Token token;
    while (tokenizer.next(token)) {
        ++impl_->summary.words;
        impl_->summary.letters += token.letters;
        ++impl_->counts[token.word];
    }
    impl_->summary.lines += tokenizer.non_empty_lines();
    impl_->summary.unique_words = impl_->counts.size();
}

Summary Analyzer::summary() const { return impl_->summary; }

std::vector<WordCount> Analyzer::top(std::size_t n) const {
    std::vector<WordCount> all;
    all.reserve(impl_->counts.size());
    for (const auto& [word, count] : impl_->counts) all.push_back({word, count});
    const auto order = [](const WordCount& a, const WordCount& b) {
        return a.count != b.count ? a.count > b.count : a.word < b.word;
    };
    const std::size_t keep = std::min(n, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(keep), all.end(), order);
    all.resize(keep);
    return all;
}

std::vector<WordCount> top_words(std::span<const std::string_view> documents, std::size_t n) {
    Analyzer analyzer;
    for (const std::string_view doc : documents) analyzer.feed(doc);
    return analyzer.top(n);
}

const char* version() noexcept { return TEXTSTATS_VERSION; }
const char* build_stamp() noexcept { return TEXTSTATS_BUILD_STAMP; }

}  // namespace textstats
