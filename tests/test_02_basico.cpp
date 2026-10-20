// Testes de borda da fila circular com int: vazia, um elemento, cheia, N = 1,
// wrap-around várias vezes, push_overwrite, operator[] e iteração.
#include <string>
#include <vector>

#include "check.h"
#include "ring_buffer.h"

namespace {

template <typename Buffer>
std::string contents(const Buffer& rb) {
    std::string out = "[";
    bool first = true;
    for (int v : rb) {
        if (!first) out += ", ";
        out += std::to_string(v);
        first = false;
    }
    return out + "]";
}

template <typename Buffer>
std::string by_index(const Buffer& rb) {
    std::string out = "[";
    for (std::size_t i = 0; i < rb.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::to_string(rb[i]);
    }
    return out + "]";
}

std::string expected(const std::vector<int>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::to_string(v[i]);
    }
    return out + "]";
}

// Confere size, front, back, operator[] e iteração contra o conteúdo esperado.
template <typename Buffer>
void check_state(const Buffer& rb, const std::vector<int>& want, const std::string& when) {
    CHECK(rb.size() == want.size(),
          when << ": size() = " << rb.size() << ", esperado " << want.size() << " (esperado " << expected(want) << ")");
    CHECK(rb.empty() == want.empty(), when << ": empty() = " << rb.empty() << " com " << want.size() << " elemento(s)");
    CHECK(rb.full() == (want.size() == Buffer::capacity()),
          when << ": full() = " << rb.full() << " com " << want.size() << " de " << Buffer::capacity() << " lugares ocupados");
    if (want.empty()) return;
    CHECK(rb.front() == want.front(), when << ": front() = " << rb.front() << ", esperado " << want.front());
    CHECK(rb.back() == want.back(), when << ": back() = " << rb.back() << ", esperado " << want.back());
    CHECK(by_index(rb) == expected(want),
          when << ": rb[0..size) = " << by_index(rb) << ", esperado " << expected(want));
    CHECK(contents(rb) == expected(want),
          when << ": a iteração (range-for) deu " << contents(rb) << ", esperado " << expected(want));
}

}  // namespace

TEST(basico_vazia, "fila nova está vazia e pop() devolve std::nullopt") {
    gq::RingBuffer<int, 4> rb;
    CHECK(rb.capacity() == 4, "capacity() deveria ser 4");
    check_state(rb, {}, "fila nova");
    CHECK(!rb.pop().has_value(), "pop() numa fila vazia deveria devolver std::nullopt");
    CHECK(rb.begin() == rb.end(), "numa fila vazia begin() deveria ser igual a end()");
    check_state(rb, {}, "depois de pop() numa fila vazia");
}

TEST(basico_um_elemento, "um elemento: push, front == back, pop e fila vazia de novo") {
    gq::RingBuffer<int, 4> rb;
    CHECK(rb.push(42), "push() numa fila vazia deveria devolver true");
    check_state(rb, {42}, "depois de push(42)");
    auto v = rb.pop();
    CHECK(v.has_value() && *v == 42, "pop() deveria devolver 42");
    check_state(rb, {}, "depois de pop() do único elemento");
}

TEST(basico_cheia, "a fila aceita exatamente capacity() elementos; push() em fila cheia devolve false") {
    gq::RingBuffer<int, 5> rb;
    for (int i = 1; i <= 5; ++i) {
        CHECK(rb.push(i * 10), "push(" << i * 10 << ") deveria caber: a capacidade é 5 e havia "
                                       << rb.size() << " elemento(s)");
    }
    check_state(rb, {10, 20, 30, 40, 50}, "depois de 5 push() numa fila de capacidade 5");
    CHECK(!rb.push(60), "push() numa fila cheia deveria devolver false");
    check_state(rb, {10, 20, 30, 40, 50}, "depois de um push() recusado");
}

TEST(basico_fifo, "pop() devolve na ordem de chegada (FIFO)") {
    gq::RingBuffer<int, 5> rb;
    for (int i = 1; i <= 5; ++i) rb.push(i);
    for (int i = 1; i <= 5; ++i) {
        auto v = rb.pop();
        CHECK(v.has_value(), "pop() número " << i << " devolveu std::nullopt; esperava " << i);
        CHECK(*v == i, "pop() número " << i << " devolveu " << *v << "; esperava " << i);
    }
    CHECK(!rb.pop().has_value(), "depois de esvaziar, pop() deveria devolver std::nullopt");
}

TEST(basico_wrap, "wrap-around: encher e esvaziar parcialmente várias vezes (capacidade 5)") {
    gq::RingBuffer<int, 5> rb;
    std::vector<int> want;
    int next = 1;
    for (int round = 1; round <= 12; ++round) {
        // Enche até o limite, depois retira 3: os índices internos dão a volta repetidas vezes.
        while (want.size() < 5) {
            CHECK(rb.push(next), "rodada " << round << ": push(" << next << ") recusado com " << rb.size()
                                           << " elemento(s) numa fila de capacidade 5");
            want.push_back(next++);
        }
        check_state(rb, want, "rodada " + std::to_string(round) + ", fila cheia");
        for (int k = 0; k < 3; ++k) {
            auto v = rb.pop();
            CHECK(v.has_value() && *v == want.front(),
                  "rodada " << round << ": pop() devolveu " << (v ? std::to_string(*v) : "nullopt")
                            << ", esperava " << want.front());
            want.erase(want.begin());
        }
        check_state(rb, want, "rodada " + std::to_string(round) + ", depois de 3 pop()");
    }
}

TEST(basico_back_apos_volta, "back() é o último inserido mesmo logo depois de a escrita voltar ao início") {
    gq::RingBuffer<int, 5> rb;
    for (int i = 1; i <= 5; ++i) rb.push(i);
    rb.pop();
    rb.pop();
    rb.push(6);  // ocupa o primeiro lugar do array de novo
    check_state(rb, {3, 4, 5, 6}, "depois de push 1..5, 2 pop() e push(6)");
    rb.push(7);
    check_state(rb, {3, 4, 5, 6, 7}, "depois de push(7)");
}

TEST(basico_overwrite, "push_overwrite() numa fila cheia descarta o mais antigo") {
    gq::RingBuffer<int, 5> rb;
    for (int i = 1; i <= 3; ++i) rb.push_overwrite(i);
    check_state(rb, {1, 2, 3}, "3 push_overwrite() numa fila vazia (não deveria descartar nada)");
    std::vector<int> want = {1, 2, 3};
    for (int i = 4; i <= 23; ++i) {
        rb.push_overwrite(i);
        want.push_back(i);
        if (want.size() > 5) want.erase(want.begin());
        check_state(rb, want, "depois de push_overwrite(" + std::to_string(i) + ")");
    }
    auto v = rb.pop();
    CHECK(v.has_value() && *v == 19, "pop() depois dos push_overwrite deveria devolver 19 (o mais antigo)");
}

TEST(basico_clear, "clear() esvazia a fila e ela continua utilizável") {
    gq::RingBuffer<int, 5> rb;
    for (int i = 1; i <= 4; ++i) rb.push(i);
    rb.pop();
    rb.clear();
    check_state(rb, {}, "depois de clear()");
    for (int i = 10; i <= 14; ++i) {
        CHECK(rb.push(i), "push(" << i << ") depois de clear() deveria caber");
    }
    check_state(rb, {10, 11, 12, 13, 14}, "enchendo de novo depois de clear()");
}

TEST(basico_capacidade_1, "capacidade 1 (N = 1): vazia, cheia, push recusado e overwrite") {
    gq::RingBuffer<int, 1> rb;
    check_state(rb, {}, "fila nova de capacidade 1");
    CHECK(rb.push(7), "push(7) numa fila de capacidade 1 vazia deveria devolver true");
    check_state(rb, {7}, "depois de push(7)");
    CHECK(!rb.push(8), "push(8) com a fila de capacidade 1 cheia deveria devolver false");
    check_state(rb, {7}, "depois de push(8) recusado");
    rb.push_overwrite(9);
    check_state(rb, {9}, "depois de push_overwrite(9)");
    auto v = rb.pop();
    CHECK(v.has_value() && *v == 9, "pop() deveria devolver 9");
    check_state(rb, {}, "depois de pop()");
}

TEST(basico_potencia_de_2, "capacidade 8: wrap-around e push_overwrite") {
    gq::RingBuffer<int, 8> rb;
    std::vector<int> want;
    for (int i = 1; i <= 40; ++i) {
        if (i % 3 == 0) {
            rb.push_overwrite(i);
            want.push_back(i);
            if (want.size() > 8) want.erase(want.begin());
        } else if (i % 5 == 0) {
            auto v = rb.pop();
            CHECK(v.has_value() == !want.empty(), "passo " << i << ": pop() em fila com " << want.size() << " elemento(s)");
            if (!want.empty()) {
                CHECK(*v == want.front(), "passo " << i << ": pop() devolveu " << *v << ", esperava " << want.front());
                want.erase(want.begin());
            }
        } else {
            bool ok = rb.push(i);
            CHECK(ok == (want.size() < 8), "passo " << i << ": push() devolveu " << ok << " com " << want.size()
                                                    << " de 8 lugares ocupados");
            if (ok) want.push_back(i);
        }
        check_state(rb, want, "passo " + std::to_string(i));
    }
}
