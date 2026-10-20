// Testes de Buffer: cópia profunda, movimento sem cópia, estado pós-movimento,
// autoatribuição e comportamento dentro de std::vector.
#include <type_traits>
#include <utility>
#include <vector>

#include "check.h"
#include "copy_meter.h"
#include "helpers.h"
#include "../src/buffer.h"

using gq::kPayload;

TEST(buffer_copia_profunda, "copiar um Buffer cria bytes independentes (uma alocação por cópia)") {
    Buffer a(kPayload, 'a');
    gq::CopyMeter meter(kPayload);
    Buffer b(a);
    CHECK(meter.allocations() == 1,
          "Buffer b(a) fez " << meter.allocations() << " alocação(ões) de payload; uma cópia "
                                "profunda faz exatamente 1");
    b[0] = 'Z';
    CHECK(a[0] == 'a', "alterar a cópia alterou o original: os dois compartilham bytes");
    Buffer c(16, 'c');
    c = a;
    CHECK(meter.allocations() == 2, "c = a deveria alocar exatamente 1 bloco novo");
    c[1] = 'Y';
    CHECK(a[1] == 'a' && gq::filled_with(a, kPayload, 'a'), "c = a deixou c e a compartilhando bytes");
    const Buffer& alias = c;
    c = alias;  // autoatribuição por cópia
    CHECK(c.size() == kPayload && c[1] == 'Y', "autoatribuição por cópia (c = c) corrompeu o Buffer");
}

TEST(buffer_move_construcao, "Buffer(std::move(x)) não aloca e deixa x vazio") {
    Buffer a(kPayload, 'm');
    const char* bytes = a.data();
    gq::CopyMeter meter(kPayload);
    Buffer b(std::move(a));
    CHECK(meter.allocations() == 0,
          "construir por movimento alocou " << meter.allocations() << " bloco(s) de payload (esperado: 0)");
    CHECK(b.data() == bytes && gq::filled_with(b, kPayload, 'm'),
          "o Buffer construído por movimento deveria ficar com os bytes da origem");
    CHECK(a.empty() && a.size() == 0, "a origem de um movimento deve ficar vazia (contrato de buffer.h); size() == "
                                          << a.size());
}

TEST(buffer_move_atribuicao, "b = std::move(a) não aloca, deixa a vazio e sem double free") {
    Buffer a(kPayload, 'x');
    Buffer b(32, 'y');
    gq::CopyMeter meter(kPayload);
    b = std::move(a);
    CHECK(meter.allocations() == 0,
          "atribuição por movimento alocou " << meter.allocations() << " bloco(s) de payload (esperado: 0)");
    CHECK(gq::filled_with(b, kPayload, 'x'), "depois de b = std::move(a), b deveria ter os bytes de a");
    CHECK(a.empty() && a.size() == 0,
          "a origem de uma atribuição por movimento deve ficar vazia (contrato de buffer.h); size() == " << a.size());
    a = Buffer(8, 'n');  // um objeto movido pode receber um novo valor
    CHECK(gq::filled_with(a, 8, 'n'), "atribuir um novo valor a um Buffer já movido deveria funcionar");
}

TEST(buffer_auto_movimento, "a = std::move(a) deixa o Buffer válido (sem uso após liberação)") {
    Buffer a(kPayload, 's');
    Buffer& alias = a;
    a = std::move(alias);
    CHECK(a.empty() || gq::filled_with(a, kPayload, 's'),
          "depois de a = std::move(a), o Buffer deveria estar intacto ou vazio — está inconsistente");
    if (!a.empty()) {
        a[0] = 'q';  // e continua utilizável
    }
    a = Buffer(4, 'r');
    CHECK(gq::filled_with(a, 4, 'r'), "o Buffer não aceitou um novo valor depois do automovimento");
}

TEST(buffer_vector_realocacao, "std::vector<Buffer> realoca movendo, sem copiar payloads") {
    std::vector<Buffer> v;  // sem reserve: força várias realocações
    gq::CopyMeter meter(kPayload);
    for (std::size_t i = 0; i < 64; ++i) {
        v.push_back(Buffer(kPayload, gq::fill_for(i)));
    }
    long copies = meter.allocations() - 64;
    CHECK(copies == 0, "64 push_back fizeram " << copies << " cópia(s) profunda(s) de payload além das 64 "
                       "criações: o vector copiou os Buffers ao realocar (por que ele não moveu?)");
    for (std::size_t i = 0; i < v.size(); ++i) {
        CHECK(gq::filled_with(v[i], kPayload, gq::fill_for(i)), "o Buffer " << i << " perdeu o conteúdo");
    }
}

TEST(buffer_noexcept, "o movimento de Buffer é noexcept (construção e atribuição)") {
    CHECK(std::is_nothrow_move_constructible<Buffer>::value,
          "std::is_nothrow_move_constructible<Buffer> é false: std::vector vai copiar em vez de mover");
    CHECK(std::is_nothrow_move_assignable<Buffer>::value, "std::is_nothrow_move_assignable<Buffer> é false");
}
