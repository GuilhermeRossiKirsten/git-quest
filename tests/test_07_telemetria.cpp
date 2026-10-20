// O consumidor real: gq::TelemetryLog guarda os 5 eventos mais recentes.
#include <string>
#include <vector>

#include "check.h"
#include "telemetry_log.h"

namespace {

std::vector<unsigned> seqs(const gq::TelemetryLog& log) {
    std::vector<unsigned> out;
    log.for_each([&out](const gq::Event& e) { out.push_back(e.seq); });
    return out;
}

std::string show(const std::vector<unsigned>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + std::to_string(v[i]);
    return out + "]";
}

}  // namespace

TEST(telemetria_poucos_eventos, "com menos de 5 eventos o log guarda todos") {
    gq::TelemetryLog log;
    CHECK(log.size() == 0 && log.latest() == nullptr && log.oldest() == nullptr, "log novo deveria estar vazio");
    log.record(gq::Severity::info, 10);
    log.record(gq::Severity::error, 20);
    log.record(gq::Severity::info, 30);
    CHECK(log.size() == 3, "size() = " << log.size() << ", esperado 3");
    CHECK(log.oldest() && log.oldest()->seq == 1, "o mais antigo deveria ser o evento #1");
    CHECK(log.latest() && log.latest()->seq == 3 && log.latest()->value == 30, "o mais recente deveria ser o evento #3");
    CHECK(log.count(gq::Severity::error) == 1, "deveria haver 1 erro guardado");
    CHECK(show(seqs(log)) == "[1, 2, 3]", "eventos guardados: " << show(seqs(log)) << ", esperado [1, 2, 3]");
}

TEST(telemetria_ultimos_5, "com o log cheio, cada novo evento descarta o mais antigo") {
    gq::TelemetryLog log;
    for (int i = 1; i <= 12; ++i) {
        gq::Severity s = gq::Severity::info;
        if (i % 4 == 0) s = gq::Severity::warning;
        if (i % 5 == 4) s = gq::Severity::error;
        log.record(s, i * 10);
    }
    CHECK(log.size() == 5, "size() = " << log.size() << " depois de 12 eventos; o log guarda os 5 últimos");
    CHECK(log.dropped() == 7, "dropped() = " << log.dropped() << ", esperado 7");
    CHECK(show(seqs(log)) == "[8, 9, 10, 11, 12]",
          "eventos guardados: " << show(seqs(log)) << ", esperado [8, 9, 10, 11, 12]");
    CHECK(log.oldest() && log.oldest()->seq == 8, "o mais antigo deveria ser o evento #8");
    CHECK(log.latest() && log.latest()->seq == 12 && log.latest()->value == 120,
          "o mais recente deveria ser o evento #12 (valor 120)");
    CHECK(log.count(gq::Severity::error) == 1 && log.count(gq::Severity::warning) == 2,
          "entre #8..#12 há 1 erro (#9) e 2 avisos (#8, #12); contados: "
              << log.count(gq::Severity::error) << " erro(s), " << log.count(gq::Severity::warning) << " aviso(s)");
}

TEST(telemetria_longa, "depois de 10 001 eventos, o log tem exatamente #9997..#10001") {
    gq::TelemetryLog log;
    for (int i = 1; i <= 10001; ++i) log.record(gq::Severity::info, i);
    CHECK(show(seqs(log)) == "[9997, 9998, 9999, 10000, 10001]",
          "eventos guardados: " << show(seqs(log)) << ", esperado [9997, 9998, 9999, 10000, 10001]");
    CHECK(log.latest() && log.latest()->seq == 10001, "o mais recente deveria ser o evento #10001");
}
