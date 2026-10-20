// telemetry_log.h — log dos últimos eventos de telemetria do sensor.
//
// Mantido pela equipe de firmware: NÃO faz parte dos arquivos editáveis do desafio.
// Ele só usa a API pública de gq::RingBuffer; se o log se comporta mal, o problema
// está na fila circular.
#ifndef GQ_TELEMETRY_LOG_H
#define GQ_TELEMETRY_LOG_H

#include <cstddef>
#include <cstdint>

#include "ring_buffer.h"

namespace gq {

enum class Severity : std::uint8_t { info, warning, error };

struct Event {
    std::uint32_t seq;   // número sequencial, começa em 1
    Severity severity;
    std::int32_t value;  // leitura do sensor associada ao evento
};

// Guarda apenas os kCapacity eventos mais recentes: ao registrar com o log cheio,
// o evento mais antigo é descartado.
class TelemetryLog {
public:
    static constexpr std::size_t kCapacity = 5;

    void record(Severity severity, std::int32_t value) {
        ++recorded_;
        events_.push_overwrite(Event{recorded_, severity, value});
    }

    std::size_t size() const { return events_.size(); }
    std::uint32_t recorded() const { return recorded_; }
    std::uint32_t dropped() const { return recorded_ - static_cast<std::uint32_t>(events_.size()); }

    // nullptr quando o log está vazio.
    const Event* oldest() const { return events_.empty() ? nullptr : &events_.front(); }
    const Event* latest() const { return events_.empty() ? nullptr : &events_.back(); }

    std::size_t count(Severity severity) const {
        std::size_t n = 0;
        for (const Event& e : events_) {
            if (e.severity == severity) {
                ++n;
            }
        }
        return n;
    }

    // Visita os eventos guardados, do mais antigo ao mais novo.
    template <typename Visitor>
    void for_each(Visitor&& visit) const {
        for (const Event& e : events_) {
            visit(e);
        }
    }

private:
    RingBuffer<Event, kCapacity> events_;
    std::uint32_t recorded_ = 0;
};

}  // namespace gq

#endif
