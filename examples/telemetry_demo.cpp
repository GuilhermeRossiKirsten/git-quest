// telemetry_demo.cpp — registra 12 eventos num log que guarda só os 5 últimos.
//
//   make demo      (dentro do container; ./start.sh já executa uma vez)
//
// Saída esperada quando a fila circular estiver correta:
//   eventos registrados: 12, guardados: 5, descartados: 7
//   #8 aviso 80 | #9 erro 90 | #10 info 100 | #11 info 110 | #12 aviso 120
//   mais antigo: #8 · mais recente: #12 · erros guardados: 1
#include <cstdio>

#include "telemetry_log.h"

namespace {

const char* name(gq::Severity s) {
    switch (s) {
        case gq::Severity::info: return "info";
        case gq::Severity::warning: return "aviso";
        case gq::Severity::error: return "erro";
    }
    return "?";
}

}  // namespace

int main() {
    gq::TelemetryLog log;
    for (int i = 1; i <= 12; ++i) {
        gq::Severity s = gq::Severity::info;
        if (i % 4 == 0) s = gq::Severity::warning;
        if (i % 5 == 4) s = gq::Severity::error;
        log.record(s, i * 10);
    }

    std::printf("eventos registrados: %u, guardados: %zu, descartados: %u\n",
                static_cast<unsigned>(log.recorded()), log.size(), static_cast<unsigned>(log.dropped()));
    const char* sep = "";
    log.for_each([&sep](const gq::Event& e) {
        std::printf("%s#%u %s %d", sep, static_cast<unsigned>(e.seq), name(e.severity), static_cast<int>(e.value));
        sep = " | ";
    });
    std::printf("\n");
    if (log.oldest() != nullptr && log.latest() != nullptr) {
        std::printf("mais antigo: #%u · mais recente: #%u · erros guardados: %zu\n",
                    static_cast<unsigned>(log.oldest()->seq), static_cast<unsigned>(log.latest()->seq),
                    log.count(gq::Severity::error));
    } else {
        std::printf("o log diz que está vazio\n");
    }
    return 0;
}
