// Testes de BlockingQueue: ordem, fechamento, parada cooperativa e muitos produtores/consumidores.
#include <atomic>
#include <chrono>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "check.h"
#include "../src/blocking_queue.h"

using namespace std::chrono_literals;

TEST(fila_fifo, "fila entrega em ordem FIFO; depois de close() recusa push e esvazia o que restou") {
    BlockingQueue<int> q;
    CHECK(q.push(1) && q.push(2) && q.push(3), "push() numa fila aberta deveria devolver true");
    CHECK(q.size() == 3, "size() deveria ser 3, é " << q.size());
    for (int expected = 1; expected <= 3; ++expected) {
        std::optional<int> v = q.pop();
        CHECK(v && *v == expected, "pop() deveria devolver " << expected);
    }
    CHECK(q.push(4) && q.push(5), "push() deveria aceitar mais itens");
    q.close();
    q.close();  // idempotente
    CHECK(!q.push(6), "push() depois de close() deveria devolver false");
    std::optional<int> a = q.pop();
    std::optional<int> b = q.pop();
    CHECK(a && *a == 4 && b && *b == 5, "close() não pode descartar itens que já estavam na fila");
    gq::step("pop() numa fila fechada e vazia");
    CHECK(!q.pop().has_value(), "pop() numa fila fechada e vazia deveria devolver std::nullopt");
    CHECK(q.size() == 0, "a fila deveria estar vazia");
}

TEST(fila_close_acorda_todos, "close() acorda TODOS os consumidores bloqueados em pop() em < 200 ms") {
    BlockingQueue<int> q;
    constexpr int kConsumers = 4;
    std::atomic<int> returned{0};
    std::atomic<int> got_value{0};
    std::vector<std::jthread> consumers;
    for (int i = 0; i < kConsumers; ++i) {
        consumers.emplace_back([&] {
            if (q.pop().has_value()) {
                ++got_value;
            }
            ++returned;
        });
    }
    std::this_thread::sleep_for(50ms);  // todos bloqueados em pop()
    CHECK(returned.load() == 0, "pop() retornou numa fila aberta e vazia (deveria bloquear)");
    gq::step("esperando os consumidores acordarem depois de close()");
    q.close();
    const bool all = gq::wait_until([&] { return returned.load() == kConsumers; }, 200ms);
    CHECK(all, "só " << returned.load() << " de " << kConsumers
                     << " consumidores acordaram até 200 ms depois de close(); os outros continuam bloqueados");
    CHECK(got_value.load() == 0, "pop() inventou um item numa fila vazia");
}

TEST(fila_stop_token, "pop(stop_token) bloqueado retorna std::nullopt em < 200 ms quando a parada é pedida") {
    BlockingQueue<int> q;

    // 1. Já parado e fila vazia: não bloqueia.
    std::stop_source stopped;
    stopped.request_stop();
    gq::step("pop() com um stop_token já parado");
    CHECK(!q.pop(stopped.get_token()).has_value(), "pop() com parada já pedida e fila vazia deveria devolver nullopt");

    // 2. Com um token que nunca para, itens continuam saindo normalmente.
    std::stop_source idle;
    q.push(7);
    std::optional<int> v = q.pop(idle.get_token());
    CHECK(v && *v == 7, "pop(stop_token) deveria devolver o item disponível");

    // 3. Parada pedida pelo próprio std::jthread enquanto o consumidor espera.
    std::atomic<bool> returned{false};
    std::atomic<bool> got_value{false};
    std::jthread consumer([&](std::stop_token st) {
        got_value = q.pop(st).has_value();
        returned = true;
    });
    std::this_thread::sleep_for(50ms);
    CHECK(!returned.load(), "pop(stop_token) retornou antes de alguém pedir parada");
    gq::step("esperando pop(stop_token) retornar depois de request_stop()");
    consumer.request_stop();
    CHECK(gq::wait_until([&] { return returned.load(); }, 200ms),
          "pop(stop_token) continuou bloqueado 200 ms depois de request_stop() (a fila continua aberta)");
    CHECK(!got_value.load(), "pop(stop_token) deveria devolver nullopt quando para sem item");

    // 4. Parada pedida por um std::stop_source externo, com outro consumidor esperando sem token.
    std::stop_source external;
    std::atomic<bool> returned2{false};
    std::atomic<bool> plain_returned{false};
    std::jthread c2([&] {
        (void)q.pop(external.get_token());
        returned2 = true;
    });
    std::jthread plain([&] {
        (void)q.pop();
        plain_returned = true;
    });
    std::this_thread::sleep_for(50ms);
    gq::step("esperando pop(token de um stop_source externo) retornar");
    external.request_stop();
    CHECK(gq::wait_until([&] { return returned2.load(); }, 200ms),
          "pop(token) continuou bloqueado 200 ms depois de stop_source::request_stop()");
    CHECK(!plain_returned.load(), "a parada de um token não pode acordar quem espera com outro token");
    q.push(1);  // libera o consumidor sem token
    CHECK(gq::wait_until([&] { return plain_returned.load(); }, 200ms), "push() não acordou o consumidor");
}

TEST(fila_mpmc, "4 produtores x 4 consumidores: cada item sai exatamente uma vez; pop() nunca devolve nullopt com a fila aberta") {
    BlockingQueue<int> q;
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 5000;
    constexpr int kTotal = kProducers * kPerProducer;
    std::vector<std::atomic<int>> seen(kTotal);
    std::atomic<bool> closing{false};
    std::atomic<int> premature{0};
    std::atomic<int> finished{0};
    std::vector<std::jthread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (std::optional<int> v = q.pop()) {
                seen[*v].fetch_add(1);
            }
            if (!closing.load()) {
                ++premature;  // nullopt com a fila aberta
            }
            ++finished;
        });
    }
    {
        std::vector<std::jthread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&q, p] {
                for (int i = 0; i < kPerProducer; ++i) {
                    q.push(p * kPerProducer + i);
                    if (i % 512 == 0) {
                        std::this_thread::yield();
                    }
                }
            });
        }
    }  // produtores terminaram
    gq::step("esperando os consumidores esvaziarem a fila");
    CHECK(gq::wait_until([&] { return q.size() == 0 || premature.load() == kConsumers; }, 5s),
          "a fila não esvaziou em 5 s");
    std::this_thread::sleep_for(20ms);
    CHECK(premature.load() == 0, premature.load() << " consumidor(es) receberam std::nullopt de pop() com a fila "
                                                     "ABERTA e desistiram antes da hora");
    closing = true;
    q.close();
    gq::step("esperando os consumidores terminarem depois de close()");
    CHECK(gq::wait_until([&] { return finished.load() == kConsumers; }, 500ms),
          "só " << finished.load() << " de " << kConsumers << " consumidores terminaram depois de close()");
    for (int i = 0; i < kTotal; ++i) {
        CHECK(seen[i].load() == 1, "o item " << i << " saiu " << seen[i].load() << " vez(es) (esperava 1)");
    }
}
