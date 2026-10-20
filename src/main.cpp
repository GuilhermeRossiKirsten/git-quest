// Pipeline de eventos de auditoria: filtra, ordena, formata e entrega a um sink.
//
//   make build && ./build/pipeline
//
// É só uma demonstração de uso de gq/functional.hpp; os testes do avaliador
// exercitam a biblioteca diretamente.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gq/functional.hpp"

namespace {

struct Event {
    long timestamp;
    int level;
    std::string source;
    std::string message;
};

// Um comparador por campo, como era costume antes de lambdas genéricas.
struct ByTimestamp {
    bool operator()(const Event& a, const Event& b) const { return a.timestamp < b.timestamp; }
};

struct BySource {
    bool operator()(const Event& a, const Event& b) const { return a.source < b.source; }
};

class ConsoleSink {
public:
    using value_type = std::string;

    void consume(const std::string& line) {
        ++count_;
        std::printf("%3d  %s\n", count_, line.c_str());
    }

private:
    int count_ = 0;
};

}  // namespace

int main() {
    std::vector<Event> events = {
        {1700000300, 2, "pagamentos", "estorno solicitado"},
        {1700000100, 1, "login", "senha incorreta"},
        {1700000200, 3, "pagamentos", "limite diário excedido"},
        {1700000050, 3, "login", "conta bloqueada após 5 tentativas"},
        {1700000400, 0, "health", "ok"},
    };

    std::function<bool(const int&)> important = gq::at_least(2);
    std::function<std::string(const std::string&)> tag = gq::tagger("auditoria-financeira-2024");
    std::function<void(const std::string&)> deliver =
        gq::bind_sink(std::unique_ptr<ConsoleSink>(new ConsoleSink()));

    std::sort(events.begin(), events.end(), ByTimestamp());
    std::stable_sort(events.begin(), events.end(), BySource());

    for (const Event& e : events) {
        if (important(e.level)) {
            deliver(tag(e.source + ": " + e.message));
        }
    }
    return 0;
}
