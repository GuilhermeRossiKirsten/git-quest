// Testes de Message: construção sem cópias, movimento, semântica de valor
// (cópia independente) e retorno de compose() sem cópia nem movimento.
#include <string>
#include <type_traits>
#include <utility>

#include "check.h"
#include "copy_meter.h"
#include "helpers.h"
#include "../src/message.h"

using gq::kPayload;

namespace {

typedef LifecycleProbe<Message> Probe;

bool is_empty(const Message& m) {
    return m.topic().empty() && m.header_count() == 0 && m.payload().empty();
}

}  // namespace

TEST(message_construcao_sem_copia, "Message(topic, Buffer temporário ou movido) não copia o payload") {
    {
        gq::CopyMeter meter(kPayload);
        Message m("orders", Buffer(kPayload, 't'));
        long copies = meter.allocations() - 1;
        CHECK(copies == 0, "Message(\"orders\", Buffer(...)) fez " << copies
                                << " cópia(s) profunda(s) do payload temporário (esperado: 0)");
        CHECK(m.topic() == "orders" && gq::filled_with(m.payload(), kPayload, 't'), "conteúdo da Message incorreto");
    }
    {
        Buffer b(kPayload, 'u');
        std::string topic = "billing";
        gq::CopyMeter meter(kPayload);
        Message m(topic, std::move(b));
        CHECK(meter.allocations() == 0, "Message(topic, std::move(b)) fez " << meter.allocations()
                                             << " cópia(s) profunda(s) do payload (esperado: 0)");
        CHECK(m.topic() == "billing" && topic == "billing" && gq::filled_with(m.payload(), kPayload, 'u'),
              "conteúdo da Message incorreto");
    }
    {
        Buffer keep(kPayload, 'k');
        gq::CopyMeter meter(kPayload);
        Message m("audit", keep);  // lvalue: aqui uma cópia é obrigatória
        CHECK(meter.allocations() == 1, "Message(topic, lvalue) deveria copiar o payload exatamente 1 vez, copiou "
                                             << meter.allocations());
        CHECK(gq::filled_with(keep, kPayload, 'k'), "construir a partir de um lvalue não pode esvaziá-lo");
    }
}

TEST(message_move, "mover uma Message não copia o payload e deixa a origem vazia") {
    Message a = Message::compose("orders", kPayload, 'm');
    a.set_header("trace-id", "42");
    gq::CopyMeter meter(kPayload);
    Message b(std::move(a));
    CHECK(meter.allocations() == 0,
          "Message b(std::move(a)) fez " << meter.allocations() << " cópia(s) profunda(s) do payload (esperado: 0)");
    CHECK(b.topic() == "orders" && b.header("trace-id") == "42" && b.header_count() == 3 &&
              gq::filled_with(b.payload(), kPayload, 'm'),
          "a Message construída por movimento deveria ter tópico, cabeçalhos e payload da origem");
    CHECK(is_empty(a), "a Message movida deveria ficar vazia (contrato de message.h): topic='"
                           << a.topic() << "', " << a.header_count() << " cabeçalho(s), payload de "
                           << a.payload().size() << " bytes");

    Message c("other", Buffer(8, 'o'));
    c = std::move(b);
    CHECK(meter.allocations() == 0,
          "c = std::move(b) fez " << meter.allocations() << " cópia(s) profunda(s) do payload (esperado: 0)");
    CHECK(c.topic() == "orders" && gq::filled_with(c.payload(), kPayload, 'm'),
          "depois de c = std::move(b), c deveria ter o conteúdo de b");
    CHECK(is_empty(b), "a Message movida (atribuição) deveria ficar vazia");

    a = Message::compose("reuse", 16, 'r');  // um objeto movido pode receber novo valor
    a.set_header("x", "1");
    CHECK(a.topic() == "reuse" && a.header("x") == "1" && gq::filled_with(a.payload(), 16, 'r'),
          "não foi possível reutilizar uma Message já movida");
}

TEST(message_copia_independente, "copiar uma Message é uma cópia profunda e independente (semântica de valor)") {
    Message a = Message::compose("orders", kPayload, 'a');
    a.set_header("trace-id", "1");
    gq::CopyMeter meter(kPayload);
    Message b(a);
    CHECK(meter.allocations() == 1,
          "Message b(a) fez " << meter.allocations() << " alocação(ões) de payload; uma cópia explícita "
          "precisa de exatamente 1 (0 significa payload compartilhado entre as duas Messages)");
    b.payload()[0] = 'Z';
    b.set_header("trace-id", "2");
    a.payload()[1] = 'Q';
    CHECK(a.payload()[0] == 'a' && a.header("trace-id") == "1",
          "alterar a cópia alterou o original: as Messages compartilham estado");
    CHECK(b.payload()[1] == 'a', "alterar o original alterou a cópia: as Messages compartilham o payload");

    Message c("small", Buffer(8, 's'));
    c = a;
    CHECK(meter.allocations() == 2, "c = a deveria copiar o payload exatamente 1 vez");
    c.payload()[2] = 'W';
    CHECK(a.payload()[2] == 'a' && c.topic() == "orders" && c.header("trace-id") == "1",
          "c = a não produziu uma cópia independente");
    const Message& alias = c;
    c = alias;  // autoatribuição
    CHECK(c.topic() == "orders" && c.payload().size() == kPayload && c.payload()[2] == 'W',
          "autoatribuição (c = c) corrompeu a Message");
}

TEST(message_compose_sem_movimento, "compose() devolve a Message sem copiá-la nem movê-la") {
    Probe::reset();
    gq::CopyMeter meter(kPayload);
    Message m = Message::compose("orders", kPayload, 'c');
    CHECK(meter.allocations() == 1,
          "compose() alocou " << meter.allocations() << " bloco(s) de payload; o necessário é 1");
    CHECK(Probe::copies() == 0, "compose() copiou a Message " << Probe::copies() << " vez(es)");
    CHECK(Probe::moves() == 0, "compose() moveu a Message " << Probe::moves()
                                    << " vez(es); o retorno de uma variável local pode não custar nada");
    CHECK(m.topic() == "orders" && m.header("content-length") == "100003" &&
              m.header("content-type") == "application/octet-stream" && m.header_count() == 2 &&
              gq::filled_with(m.payload(), kPayload, 'c'),
          "compose() produziu uma Message com conteúdo incorreto");
}

TEST(message_noexcept, "o movimento de Message é noexcept (construção e atribuição)") {
    CHECK(std::is_nothrow_move_constructible<Message>::value,
          "std::is_nothrow_move_constructible<Message> é false: std::vector<Message> vai copiar ao realocar");
    CHECK(std::is_nothrow_move_assignable<Message>::value, "std::is_nothrow_move_assignable<Message> é false");
    CHECK(std::is_copy_constructible<Message>::value && std::is_copy_assignable<Message>::value,
          "Message precisa continuar copiável (semântica de valor)");
}
