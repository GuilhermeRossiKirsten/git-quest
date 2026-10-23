// Consumidor do avaliador: compila contra a textstats instalada e confere resultados.
#include <textstats/textstats.h>

#include <cstdio>
#include <cstring>
#include <string_view>

int main() {
    int failures = 0;
    const auto expect = [&failures](bool ok, const char* what) {
        if (!ok) {
            std::printf("GQ_FAIL %s\n", what);
            ++failures;
        }
    };

    textstats::Analyzer analyzer;
    analyzer.feed("O rato roeu a roupa do rei de Roma.\nO REI riu.\n");
    const textstats::Summary s = analyzer.summary();
    expect(s.lines == 2, "summary().lines == 2");
    expect(s.words == 12, "summary().words == 12");
    expect(s.unique_words == 10, "summary().unique_words == 10");

    const std::string_view docs[] = {"pão pão queijo", "Pão e queijo"};
    const auto top = textstats::top_words(docs, 2);  // std::span na assinatura: exige C++20
    expect(top.size() == 2, "top_words(...).size() == 2");
    expect(top.size() == 2 && top[0] == textstats::WordCount{"pão", 3}, "top_words[0] == {pão, 3}");
    expect(top.size() == 2 && top[1] == textstats::WordCount{"queijo", 2}, "top_words[1] == {queijo, 2}");

    expect(std::strcmp(textstats::version(), "1.4.2") == 0, "version() == \"1.4.2\"");
    std::printf("GQ_CPLUSPLUS=%ld\n", static_cast<long>(__cplusplus));
    if (failures == 0) std::puts("GQ_CONSUMER_OK");
    return failures == 0 ? 0 : 1;
}
