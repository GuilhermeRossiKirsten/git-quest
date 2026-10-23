// wordfreq — mostra um resumo e as palavras mais frequentes de arquivos de texto.
//
//   wordfreq [-n N] [ARQUIVO...]     (sem arquivos, lê a entrada padrão)
//   wordfreq --version
#include <textstats/textstats.h>

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int usage() {
    std::cerr << "uso: wordfreq [-n N] [ARQUIVO...] | wordfreq --version\n";
    return 2;
}

bool read_all(std::istream& in, std::string& out) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return !in.bad();
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t n = 10;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version") {
            std::cout << "wordfreq " << textstats::version() << " (build " << textstats::build_stamp()
                      << ")\n";
            return 0;
        }
        if (arg == "-n") {
            if (++i == argc) return usage();
            const std::string_view value = argv[i];
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (ec != std::errc{} || end != value.data() + value.size()) return usage();
        } else if (!arg.empty() && arg.front() == '-') {
            return usage();
        } else {
            files.emplace_back(arg);
        }
    }

    textstats::Analyzer analyzer;
    std::string text;
    if (files.empty()) {
        if (!read_all(std::cin, text)) return 1;
        analyzer.feed(text);
    }
    for (const std::string& name : files) {
        std::ifstream in(name, std::ios::binary);
        if (!in || !read_all(in, text)) {
            std::cerr << "wordfreq: não foi possível ler " << name << '\n';
            return 1;
        }
        analyzer.feed(text);
    }

    const textstats::Summary s = analyzer.summary();
    std::cout << "linhas: " << s.lines << '\n'
              << "palavras: " << s.words << '\n'
              << "distintas: " << s.unique_words << '\n'
              << "letras: " << s.letters << '\n';
    for (const textstats::WordCount& wc : analyzer.top(n)) {
        std::cout << wc.count << '\t' << wc.word << '\n';
    }
    return 0;
}
