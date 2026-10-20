// Critério: o callable devolvido por gq::bind_sink é DONO do sink: o sink vive
// enquanto o callable (ou quem recebeu o callable por move) existir, e é
// destruído exatamente uma vez.
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "gq/functional.hpp"

namespace {

struct RecordingSink {
    using value_type = std::string;

    RecordingSink(std::vector<std::string>* log, int* destroyed) : log_(log), destroyed_(destroyed) {}
    RecordingSink(const RecordingSink&) = delete;
    RecordingSink& operator=(const RecordingSink&) = delete;
    ~RecordingSink() { ++*destroyed_; }

    void consume(const std::string& line) { log_->push_back(prefix_ + line); }

private:
    std::vector<std::string>* log_;
    int* destroyed_;
    std::string prefix_ = "sink-de-gravação-com-prefixo-longo: ";
};

struct SummingSink {
    using value_type = int;

    explicit SummingSink(long* total) : total_(total) {}
    void consume(const int& value) { *total_ += value; }

private:
    long* total_;
};

}  // namespace

int main() {
    std::vector<std::string> log;
    int destroyed = 0;
    {
        auto handler = gq::bind_sink(std::make_unique<RecordingSink>(&log, &destroyed));
        CHECK_EQ(destroyed, 0, "destruições do sink logo após bind_sink (o handler ainda existe)");

        handler("login");
        handler("logout");
        CHECK_EQ(log.size(), std::size_t(2), "itens entregues ao sink");

        auto moved = std::move(handler);
        moved("pagamento");
        CHECK_EQ(log.size(), std::size_t(3), "itens entregues após mover o handler");
        CHECK_EQ(destroyed, 0, "destruições do sink enquanto o handler movido existe");
    }
    CHECK_EQ(destroyed, 1, "destruições do sink depois que o handler saiu de escopo");
    if (log.size() == 3) {
        CHECK_EQ(log[2], std::string("sink-de-gravação-com-prefixo-longo: pagamento"), "último item gravado");
    }

    long total = 0;
    {
        auto add = gq::bind_sink(std::make_unique<SummingSink>(&total));
        for (int i = 1; i <= 10; ++i) {
            add(i);
        }
    }
    CHECK_EQ(total, 55L, "SummingSink: soma de 1..10");

    return check::result();
}
