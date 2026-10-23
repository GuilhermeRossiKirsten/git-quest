// Testes unitários da textstats (rodam com ctest). Testam a API pública e,
// por serem internos ao projeto, também o tokenizador.
#include <textstats/textstats.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "tokenizer.h"

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "FALHA (linha %d): %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    textstats::detail::Tokenizer tokenizer(text);
    textstats::detail::Token token;
    while (tokenizer.next(token)) out.push_back(token.word);
    return out;
}

void test_tokenizer() {
    CHECK((words_of("Olá, MUNDO!  olá") == std::vector<std::string>{"olá", "mundo", "olá"}));
    CHECK((words_of("guarda-chuva d'água -x- ") == std::vector<std::string>{"guarda-chuva", "d'água", "x"}));
    CHECK(words_of("  ...  \n\n").empty());
    CHECK((words_of("A\xFFZ") == std::vector<std::string>{"a", "z"}));  // byte inválido separa
}

void test_summary() {
    textstats::Analyzer a;
    a.feed("Ação e reação.\n\nE de novo: ação!\n");
    const textstats::Summary s = a.summary();
    CHECK(s.lines == 2);
    CHECK(s.words == 7);
    CHECK(s.unique_words == 5);
    CHECK(s.letters == 22);
    const auto top = a.top(2);
    CHECK(top.size() == 2);
    CHECK((top[0] == textstats::WordCount{"ação", 2}));
    CHECK((top[1] == textstats::WordCount{"e", 2}));
}

void test_top_words() {
    const std::string_view docs[] = {"um dois dois", "três três três dois"};
    const auto top = textstats::top_words(docs, 10);
    CHECK(top.size() == 3);
    CHECK((top[0] == textstats::WordCount{"dois", 3}));
    CHECK((top[1] == textstats::WordCount{"três", 3}));
    CHECK((top[2] == textstats::WordCount{"um", 1}));
}

void test_version() {
    CHECK(std::string_view(textstats::version()) == "1.4.2");
    CHECK(std::string_view(textstats::build_stamp()).size() > 0);
}

}  // namespace

int main() {
    test_tokenizer();
    test_summary();
    test_top_words();
    test_version();
    if (failures == 0) std::puts("textstats_tests: OK");
    return failures == 0 ? 0 : 1;
}
