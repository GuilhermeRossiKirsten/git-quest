// A fila precisa funcionar com tipos que NÃO têm construtor padrão.
// Se este arquivo não compila, a fila está construindo T() em algum lugar.
#include <string>

#include "check.h"
#include "ring_buffer.h"

namespace {

// Uma leitura só faz sentido com um sensor e um valor: não existe Reading "vazia".
class Reading {
public:
    Reading() = delete;
    Reading(int sensor, double value) : sensor_(sensor), value_(value) {}
    int sensor() const { return sensor_; }
    double value() const { return value_; }

private:
    int sensor_;
    double value_;
};

}  // namespace

TEST(sem_padrao_operacoes, "RingBuffer<Reading, 3> funciona com um tipo sem construtor padrão") {
    gq::RingBuffer<Reading, 3> rb;
    CHECK(rb.empty(), "fila nova deveria estar vazia");
    CHECK(rb.push(Reading(1, 0.5)) && rb.push(Reading(2, 1.5)) && rb.push(Reading(3, 2.5)),
          "3 push() deveriam caber numa fila de capacidade 3");
    CHECK(!rb.push(Reading(4, 3.5)), "push() numa fila cheia deveria devolver false");
    CHECK(rb.front().sensor() == 1 && rb.back().sensor() == 3 && rb[1].sensor() == 2,
          "front/back/[] deveriam ver os sensores 1, 2 e 3");
    rb.push_overwrite(Reading(5, 4.5));
    CHECK(rb.front().sensor() == 2 && rb.back().sensor() == 5, "push_overwrite deveria descartar o sensor 1");
    int sum = 0;
    for (const Reading& r : rb) sum += r.sensor();
    CHECK(sum == 2 + 3 + 5, "a iteração deveria visitar os sensores 2, 3 e 5 (soma " << sum << ")");
    auto first = rb.pop();
    CHECK(first.has_value() && first->sensor() == 2 && first->value() == 1.5, "pop() deveria devolver o sensor 2");
    rb.clear();
    CHECK(rb.empty() && !rb.pop().has_value(), "depois de clear() a fila deveria estar vazia");
}

TEST(sem_padrao_string, "RingBuffer<std::string, 2> guarda e descarta strings corretamente") {
    gq::RingBuffer<std::string, 2> rb;
    rb.push_overwrite(std::string(40, 'a'));  // strings longas usam o heap: o ASan vigia
    rb.push_overwrite(std::string(40, 'b'));
    rb.push_overwrite(std::string(40, 'c'));
    CHECK(rb.size() == 2 && rb.front() == std::string(40, 'b') && rb.back() == std::string(40, 'c'),
          "a fila deveria conter as strings 'b...' e 'c...'");
    auto s = rb.pop();
    CHECK(s.has_value() && *s == std::string(40, 'b'), "pop() deveria devolver 'b...'");
}
