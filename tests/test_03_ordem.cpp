// Memory ordering e cache do índice oposto, verificados de forma determinística
// com um índice instrumentado (tests/support/probe_index.h).
//
// O cenário roda numa única thread, alternando "fases": na fase do produtor só
// há try_push; na do consumidor, só try_pop. A cada volta o produtor enche a
// fila (e tenta um item a mais) e o consumidor a esvazia (e tenta mais um).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "check.h"
#include "support/probe_index.h"
#include "../src/spsc_queue.h"

namespace {

constexpr std::size_t kCapacity = 64;
constexpr int kRounds = 50;

using ProbedQueue = SpscQueue<int, ProbeIndex>;

struct Roles {
    probe::Entry* producer_index = nullptr;  // escrito na fase do produtor ("tail")
    probe::Entry* consumer_index = nullptr;  // escrito na fase do consumidor ("head")
};

// Roda o cenário e identifica qual índice pertence a qual lado.
Roles run_scenario(ProbedQueue& q) {
    int next_in = 0;
    int next_out = 0;
    for (int round = 0; round < kRounds; ++round) {
        // A capacidade exata é verificada em test_01; aqui basta encher até recusar.
        probe::current_phase = probe::kProducer;
        std::size_t accepted = 0;
        for (std::size_t i = 0; i < kCapacity + 1; ++i) {
            if (q.try_push(next_in)) {
                ++next_in;
                ++accepted;
            }
        }
        CHECK(accepted > 0, "a fila de capacidade " << kCapacity << " não aceitou nenhum item");
        probe::current_phase = probe::kConsumer;
        for (std::size_t i = 0; i < kCapacity + 1; ++i) {
            std::optional<int> v = q.try_pop();
            if (i < accepted) {
                CHECK(v && *v == next_out, "esperava o item " << next_out << " (FIFO)");
                ++next_out;
            } else {
                CHECK(!v.has_value(), "try_pop() inventou um item numa fila vazia");
            }
        }
    }
    probe::current_phase = probe::kNone;

    std::vector<probe::Entry*> indices = probe::inside(&q, sizeof(q));
    CHECK(indices.size() == 2, "esperava exatamente 2 índices do tipo IndexT dentro da fila (head e tail), há "
                                   << indices.size());
    Roles roles;
    for (probe::Entry* e : indices) {
        const bool by_producer = e->by_phase[probe::kProducer].stores > 0;
        const bool by_consumer = e->by_phase[probe::kConsumer].stores > 0;
        CHECK(!(by_producer && by_consumer),
              "um mesmo índice foi escrito pelo produtor E pelo consumidor; numa fila SPSC cada índice tem um único "
              "escritor");
        if (by_producer) {
            roles.producer_index = e;
        } else if (by_consumer) {
            roles.consumer_index = e;
        }
    }
    CHECK(roles.producer_index != nullptr, "nenhum índice IndexT foi escrito por try_push (o produtor publica onde?)");
    CHECK(roles.consumer_index != nullptr, "nenhum índice IndexT foi escrito por try_pop (o consumidor libera onde?)");
    return roles;
}

}  // namespace

TEST(ordem_publicacao, "publicar e liberar slots: escrita do próprio índice com release e leitura do índice oposto com acquire") {
    auto q = std::make_unique<ProbedQueue>(kCapacity);
    const Roles r = run_scenario(*q);

    const probe::Stats& tail_by_producer = r.producer_index->by_phase[probe::kProducer];
    const probe::Stats& head_by_consumer = r.consumer_index->by_phase[probe::kConsumer];
    const probe::Stats& head_by_producer = r.consumer_index->by_phase[probe::kProducer];
    const probe::Stats& tail_by_consumer = r.producer_index->by_phase[probe::kConsumer];

    CHECK(tail_by_producer.weak_stores == 0,
          "try_push publica o índice do produtor com " << tail_by_producer.weak_store_order
                                                       << ": o consumidor pode enxergar o índice novo ANTES do elemento "
                                                          "construído no slot (precisa de release)");
    CHECK(tail_by_consumer.loads > 0, "try_pop nunca leu o índice do produtor: como sabe que há item?");
    CHECK(tail_by_consumer.weak_loads == 0,
          "try_pop lê o índice do produtor com " << tail_by_consumer.weak_load_order
                                                 << ": a leitura do slot pode enxergar dados antigos (precisa de acquire)");
    CHECK(head_by_consumer.weak_stores == 0,
          "try_pop libera o slot com " << head_by_consumer.weak_store_order
                                       << ": o produtor pode sobrescrever o slot enquanto o consumidor ainda o lê "
                                          "(precisa de release)");
    CHECK(head_by_producer.loads > 0, "try_push nunca leu o índice do consumidor: como sabe que há espaço?");
    CHECK(head_by_producer.weak_loads == 0,
          "try_push lê o índice do consumidor com " << head_by_producer.weak_load_order
                                                    << ": pode reutilizar um slot que o consumidor ainda não terminou "
                                                       "de ler (precisa de acquire)");
}

TEST(cache_indice_oposto, "cada lado mantém uma cópia local do índice oposto e só o relê quando ela indica fila cheia/vazia") {
    auto q = std::make_unique<ProbedQueue>(kCapacity);
    const Roles r = run_scenario(*q);

    const std::uint64_t ops_per_side = static_cast<std::uint64_t>(kRounds) * (kCapacity + 1);
    const std::uint64_t limit = ops_per_side / 8;
    const std::uint64_t head_reads = r.consumer_index->by_phase[probe::kProducer].loads;
    const std::uint64_t tail_reads = r.producer_index->by_phase[probe::kConsumer].loads;
    gq::note("leituras do índice oposto em " + std::to_string(ops_per_side) + " operações: produtor " +
             std::to_string(head_reads) + ", consumidor " + std::to_string(tail_reads));
    CHECK(head_reads <= limit, "o produtor leu o índice do consumidor " << head_reads << " vezes em " << ops_per_side
                                                                        << " try_push (limite: " << limit
                                                                        << "); cada leitura puxa a linha de cache que o "
                                                                           "outro núcleo está escrevendo");
    CHECK(tail_reads <= limit, "o consumidor leu o índice do produtor " << tail_reads << " vezes em " << ops_per_side
                                                                        << " try_pop (limite: " << limit << ")");
}
