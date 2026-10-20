// Testes de Batch: acumular, transformar, despachar e esvaziar sem copiar
// payloads, preservando a ordem e o conteúdo das mensagens.
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "copy_meter.h"
#include "helpers.h"
#include "../src/batch.h"

using gq::kPayload;

namespace {

// Lote com n mensagens de kPayload bytes (cria fora de qualquer medição).
void fill_batch(Batch& batch, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        batch.add(gq::topic_for(i), Buffer(kPayload, gq::fill_for(i)));
    }
}

bool matches(const Message& m, std::size_t i) {
    return m.topic() == gq::topic_for(i) && gq::filled_with(m.payload(), kPayload, gq::fill_for(i));
}

Message stage_two(Message m) {
    m.set_header("stage", "2");
    m.payload()[0] = 'T';
    return m;
}

}  // namespace

TEST(batch_add_sem_copia, "add() de temporários e de Messages movidas não copia payloads") {
    Batch batch;
    gq::CopyMeter meter(kPayload);
    for (std::size_t i = 0; i < 300; ++i) {
        switch (i % 3) {
            case 0:
                batch.add(gq::topic_for(i), Buffer(kPayload, gq::fill_for(i)));
                break;
            case 1:
                batch.add(Message::compose(gq::topic_for(i), kPayload, gq::fill_for(i)));
                break;
            default: {
                Message m(gq::topic_for(i), Buffer(kPayload, gq::fill_for(i)));
                batch.add(std::move(m));
            }
        }
    }
    long copies = meter.allocations() - 300;
    CHECK(copies == 0, "300 add() fizeram " << copies << " cópia(s) profunda(s) de payload além das 300 "
                                               "criações (esperado: 0)");
    CHECK(batch.size() == 300, "o lote deveria ter 300 mensagens, tem " << batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
        CHECK(matches(batch[i], i), "a mensagem " << i << " está fora de ordem ou com conteúdo incorreto");
    }

    Message keep = Message::compose("keep", kPayload, 'k');
    gq::CopyMeter lvalue(kPayload);
    batch.add(keep);  // lvalue: o chamador continua com a sua Message
    CHECK(lvalue.allocations() == 1,
          "add(lvalue) deveria copiar o payload exatamente 1 vez, copiou " << lvalue.allocations());
    CHECK(keep.topic() == "keep" && gq::filled_with(keep.payload(), kPayload, 'k'),
          "add(lvalue) não pode esvaziar a Message do chamador");
}

TEST(batch_transform_sem_copia, "transform() não copia payloads e preserva ordem e conteúdo") {
    Batch batch;
    fill_batch(batch, 200);
    gq::CopyMeter meter(kPayload);
    batch.transform(&stage_two);
    CHECK(meter.allocations() == 0,
          "transform() de 200 mensagens fez " << meter.allocations() << " cópia(s) profunda(s) (esperado: 0)");
    CHECK(batch.size() == 200, "transform() mudou o tamanho do lote para " << batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
        const Message& m = batch[i];
        CHECK(m.topic() == gq::topic_for(i) && m.header("stage") == "2" && m.payload()[0] == 'T' &&
                  m.payload()[kPayload - 1] == gq::fill_for(i),
              "a mensagem " << i << " não recebeu a transformação ou saiu de ordem");
    }
}

TEST(batch_dispatch_sem_copia, "dispatch() entrega em ordem, sem copiar payloads, e esvazia o lote") {
    Batch batch;
    fill_batch(batch, 300);
    std::vector<Message> received;
    gq::CopyMeter meter(kPayload);
    std::size_t n = batch.dispatch([&received](Message m) { received.push_back(std::move(m)); });
    CHECK(meter.allocations() == 0,
          "dispatch() de 300 mensagens fez " << meter.allocations() << " cópia(s) profunda(s) (esperado: 0)");
    CHECK(n == 300 && received.size() == 300, "dispatch() deveria entregar 300 mensagens, entregou " << n);
    CHECK(batch.empty(), "depois de dispatch() o lote deveria ficar vazio");
    for (std::size_t i = 0; i < received.size(); ++i) {
        CHECK(matches(received[i], i), "a mensagem entregue " << i << " está fora de ordem ou corrompida");
    }
    CHECK(batch.dispatch([](Message) {}) == 0, "despachar um lote vazio deveria entregar 0 mensagens");
}

TEST(batch_drain_sem_copia, "drain() devolve tudo sem copiar payloads e deixa o lote reutilizável") {
    Batch batch;
    fill_batch(batch, 300);
    gq::CopyMeter meter(kPayload);
    std::vector<Message> all = batch.drain();
    CHECK(meter.allocations() == 0,
          "drain() de 300 mensagens fez " << meter.allocations() << " cópia(s) profunda(s) (esperado: 0)");
    CHECK(batch.empty() && all.size() == 300, "drain() deveria devolver 300 mensagens e esvaziar o lote");
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(matches(all[i], i), "a mensagem " << i << " devolvida por drain() está fora de ordem ou corrompida");
    }
    batch.add("again", Buffer(4, 'g'));
    CHECK(batch.size() == 1 && batch[0].topic() == "again", "o lote não aceitou mensagens depois de drain()");
}

TEST(pipeline_mil_mensagens, "1000 mensagens de ~98 KiB: add + transform + dispatch com 0 cópias profundas") {
    gq::CopyMeter meter(kPayload);
    Batch batch;
    for (std::size_t i = 0; i < 1000; ++i) {
        batch.add(gq::topic_for(i), Buffer(kPayload, gq::fill_for(i)));
    }
    batch.transform(&stage_two);
    std::size_t next = 0;
    bool in_order = true;
    std::size_t delivered = batch.dispatch([&](Message m) {
        in_order = in_order && m.topic() == gq::topic_for(next) && m.header("stage") == "2" &&
                   m.payload().size() == kPayload && m.payload()[1] == gq::fill_for(next);
        ++next;
    });
    long copies = meter.allocations() - 1000;
    CHECK(copies == 0, "o pipeline fez " << copies << " cópia(s) profunda(s) de payload (orçamento: 0; "
                                         "cerca de " << (copies * static_cast<long>(kPayload)) / (1024 * 1024)
                                         << " MiB copiados à toa)");
    CHECK(delivered == 1000 && in_order, "o pipeline entregou mensagens fora de ordem ou corrompidas");
}
