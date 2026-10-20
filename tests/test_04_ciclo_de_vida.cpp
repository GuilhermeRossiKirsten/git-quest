// Tempo de vida dos elementos: com o tipo instrumentado Tracked, a quantidade de
// objetos vivos precisa ser sempre igual a size() — nada construído antes da hora,
// nada esquecido vivo depois de sair da fila, nada destruído duas vezes.
#include <cstdint>
#include <random>
#include <type_traits>
#include <utility>

#include "check.h"
#include "ring_buffer.h"
#include "support/tracked.h"

using gq_test::Tracked;

namespace {

void check_balance(const char* when) {
    CHECK(Tracked::misuse == 0, when << ": " << Tracked::misuse
                                     << " uso(s) de objeto que não estava vivo (destruição dupla ou cópia de"
                                        " memória onde nenhum objeto foi construído)");
    CHECK(Tracked::live == 0, when << ": " << Tracked::live << " objeto(s) continuam vivos");
    CHECK(Tracked::constructed == Tracked::destroyed,
          when << ": " << Tracked::constructed << " construções e " << Tracked::destroyed << " destruições");
}

template <typename Buffer>
void check_live(const Buffer& rb, const char* when) {
    CHECK(Tracked::misuse == 0, when << ": " << Tracked::misuse << " uso(s) de objeto que não estava vivo");
    CHECK(Tracked::live == static_cast<int>(rb.size()),
          when << ": há " << Tracked::live << " objeto(s) Tracked vivos, mas a fila tem size() = " << rb.size()
               << " (cada elemento só deve existir enquanto está na fila)");
}

template <typename Buffer>
void check_copy(std::true_type) {
    {
        Buffer a;
        for (int i = 1; i <= 3; ++i) a.push(Tracked(i));
        {
            Buffer b(a);
            CHECK(Tracked::live == 6, "depois de copiar uma fila com 3 elementos esperava 6 objetos vivos, há "
                                          << Tracked::live);
            CHECK(b.size() == 3 && b.front().value() == 1 && b.back().value() == 3,
                  "a cópia deveria ter os mesmos elementos (1, 2, 3)");
            b.pop();
            b.push(Tracked(9));
            CHECK(a.size() == 3 && a.front().value() == 1 && a.back().value() == 3,
                  "alterar a cópia mudou a original");
            Buffer c;
            c.push(Tracked(42));
            c = a;  // atribuição por cópia sobre uma fila não vazia
            CHECK(c.size() == 3 && c[0].value() == 1 && c[2].value() == 3, "depois de c = a, c deveria ser igual a a");
            CHECK(Tracked::live == 9, "depois de c = a esperava 9 objetos vivos (a, b e c com 3 cada), há "
                                          << Tracked::live);
            c = static_cast<const Buffer&>(c);  // autoatribuição
            CHECK(c.size() == 3 && c[1].value() == 2, "autoatribuição (c = c) corrompeu a fila");
        }
        CHECK(Tracked::misuse == 0, "copiar a fila gerou uso de objeto não construído ou destruição dupla");
        CHECK(Tracked::live == 3, "depois que as cópias saíram de escopo esperava 3 objetos vivos, há " << Tracked::live);
    }
    check_balance("depois de copiar filas");
}

template <typename Buffer>
void check_copy(std::false_type) {}  // cópia proibida (= delete): escolha aceita

}  // namespace

TEST(ciclo_fila_nova, "uma fila nova não constrói nenhum elemento") {
    Tracked::reset_counters();
    {
        gq::RingBuffer<Tracked, 16> rb;
        CHECK(Tracked::constructed == 0, "criar uma RingBuffer<Tracked, 16> vazia construiu "
                                             << Tracked::constructed << " objetos Tracked");
    }
    check_balance("depois de destruir a fila nova");
}

TEST(ciclo_push_pop, "pop() destrói o elemento que sai da fila") {
    Tracked::reset_counters();
    {
        gq::RingBuffer<Tracked, 4> rb;
        for (int i = 1; i <= 4; ++i) {
            rb.push(Tracked(i));
            check_live(rb, "depois de push()");
        }
        for (int i = 1; i <= 4; ++i) {
            {
                auto v = rb.pop();
                CHECK(v.has_value() && v->value() == i, "pop() deveria devolver " << i);
            }
            check_live(rb, "depois de pop() (com o valor devolvido já destruído)");
        }
    }
    check_balance("ao final");
}

TEST(ciclo_clear, "clear() destrói todos os elementos") {
    Tracked::reset_counters();
    {
        gq::RingBuffer<Tracked, 4> rb;
        for (int i = 1; i <= 3; ++i) rb.push(Tracked(i));
        rb.clear();
        check_live(rb, "depois de clear()");
        rb.push(Tracked(7));
        check_live(rb, "depois de push() após clear()");
    }
    check_balance("ao final");
}

TEST(ciclo_overwrite, "push_overwrite() destrói o elemento descartado") {
    Tracked::reset_counters();
    {
        gq::RingBuffer<Tracked, 4> rb;
        for (int i = 1; i <= 11; ++i) {
            rb.push_overwrite(Tracked(i));
            check_live(rb, "depois de push_overwrite()");
        }
        CHECK(rb.size() == 4 && rb.front().value() == 8 && rb.back().value() == 11,
              "depois de 11 push_overwrite() a fila deveria conter 8..11");
    }
    check_balance("ao final");
}

TEST(ciclo_destrutor, "o destrutor da fila destrói os elementos que restaram (e só eles)") {
    Tracked::reset_counters();
    {
        gq::RingBuffer<Tracked, 5> rb;
        for (int i = 1; i <= 5; ++i) rb.push(Tracked(i));
        rb.pop();
        rb.pop();
        rb.push(Tracked(6));
        check_live(rb, "antes de a fila sair de escopo");
    }
    check_balance("depois que a fila saiu de escopo");
}

TEST(ciclo_aleatorio, "20 000 operações aleatórias: objetos vivos == size() a cada passo") {
    Tracked::reset_counters();
    {
        std::mt19937 rng(77u);
        gq::RingBuffer<Tracked, 6> rb;
        for (int op = 1; op <= 20000; ++op) {
            const std::uint32_t r = rng() % 10;
            if (r < 4) {
                rb.push(Tracked(op));
            } else if (r < 6) {
                rb.push_overwrite(Tracked(op));
            } else if (r < 9) {
                rb.pop();
            } else if (rng() % 50 == 0) {
                rb.clear();
            }
            CHECK(Tracked::live == static_cast<int>(rb.size()) && Tracked::misuse == 0,
                  "operação #" << op << ": " << Tracked::live << " objeto(s) vivos para size() = " << rb.size()
                               << ", " << Tracked::misuse << " uso(s) indevido(s)");
        }
    }
    check_balance("ao final");
}

TEST(ciclo_copia, "copiar a fila é seguro (cópia independente) ou proibido (= delete)") {
    Tracked::reset_counters();
    using Buffer = gq::RingBuffer<Tracked, 4>;
    constexpr bool copyable = std::is_copy_constructible<Buffer>::value && std::is_copy_assignable<Buffer>::value;
    check_copy<Buffer>(std::integral_constant<bool, copyable>());
}
