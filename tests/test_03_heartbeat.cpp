// Testes de Heartbeat: periodicidade, parada rápida, destrutor e argumentos inválidos.
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

#include "check.h"
#include "../src/heartbeat.h"

using namespace std::chrono_literals;

namespace {
constexpr auto kLong = 3000ms;  // "vários segundos": parar não pode esperar por isso
}

TEST(heartbeat_periodico, "Heartbeat chama o callback periodicamente e nunca mais depois de stop()") {
    std::atomic<int> calls{0};
    Heartbeat hb(20ms, [&] { ++calls; });
    CHECK(hb.beats() == 0, "o primeiro beat deveria vir só depois do primeiro intervalo");
    gq::step("esperando 3 beats com intervalo de 20 ms");
    CHECK(gq::wait_until([&] { return hb.beats() >= 3; }, 2s),
          "esperava pelo menos 3 beats em 2 s com intervalo de 20 ms, houve " << hb.beats());
    gq::step("stop() de um heartbeat de 20 ms");
    hb.stop();
    const auto after_stop = hb.beats();
    CHECK(after_stop == static_cast<std::uint64_t>(calls.load()),
          "beats() = " << after_stop << " mas o callback foi chamado " << calls.load() << " vez(es)");
    std::this_thread::sleep_for(80ms);
    CHECK(hb.beats() == after_stop && calls.load() == static_cast<int>(after_stop),
          "o callback continuou sendo chamado depois de stop()");
    hb.stop();  // idempotente
}

TEST(heartbeat_stop_rapido, "stop() de um Heartbeat de 3 s retorna em < 200 ms e não deixa thread viva") {
    const std::size_t baseline = gq::thread_count();
    std::atomic<int> calls{0};
    Heartbeat hb(kLong, [&] { ++calls; });
    std::this_thread::sleep_for(30ms);
    gq::step("stop() de um heartbeat de 3 s");
    const double ms = gq::elapsed_ms([&] { hb.stop(); });
    CHECK(ms < 200.0, "stop() levou " << static_cast<long>(ms) << " ms (limite: 200 ms)");
    CHECK(calls.load() == 0, "o callback não deveria ter sido chamado antes do primeiro intervalo");
    CHECK_NO_THREADS_LEFT(baseline, "depois de stop()");
}

TEST(heartbeat_destrutor, "destruir um Heartbeat sem stop() é rápido e a thread de fundo termina junto") {
    const std::size_t baseline = gq::thread_count();
    std::atomic<int> calls{0};
    auto slow = std::make_unique<Heartbeat>(kLong, [&] { ++calls; });
    std::this_thread::sleep_for(30ms);
    gq::step("destrutor de um heartbeat de 3 s");
    const double ms = gq::elapsed_ms([&] { slow.reset(); });
    CHECK(ms < 200.0, "o destrutor levou " << static_cast<long>(ms) << " ms (limite: 200 ms)");
    CHECK_NO_THREADS_LEFT(baseline, "depois do destrutor de Heartbeat");

    auto fast = std::make_unique<Heartbeat>(5ms, [&] { ++calls; });
    CHECK(gq::wait_until([&] { return calls.load() >= 2; }, 2s), "o heartbeat de 5 ms não bateu");
    gq::step("destrutor de um heartbeat de 5 ms");
    fast.reset();
    const int at_destruction = calls.load();
    std::this_thread::sleep_for(50ms);
    CHECK(calls.load() == at_destruction, "o callback foi chamado " << (calls.load() - at_destruction)
                                                                    << " vez(es) DEPOIS que o destrutor retornou");
    CHECK_NO_THREADS_LEFT(baseline, "depois do destrutor de Heartbeat");
}

TEST(heartbeat_argumentos, "intervalo <= 0 ou callback vazio lançam invalid_argument sem criar thread") {
    const std::size_t baseline = gq::thread_count();
    auto noop = [] {};
    CHECK_THROWS(Heartbeat(0ms, noop), std::invalid_argument, "intervalo zero deveria ser recusado");
    CHECK_THROWS(Heartbeat(-5ms, noop), std::invalid_argument, "intervalo negativo deveria ser recusado");
    CHECK_THROWS(Heartbeat(10ms, std::function<void()>{}), std::invalid_argument,
                 "callback vazio deveria ser recusado");
    CHECK_NO_THREADS_LEFT(baseline, "depois de construções recusadas");
}
