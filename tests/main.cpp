// main.cpp — ponto de entrada do binário de testes.
//
//   ./build/asan/test-runner --list     lista "nome<TAB>descrição"
//   ./build/asan/test-runner <nome>     executa um teste; código 0 = passou
//
// O watchdog encerra o teste (código 124) se ele passar de GQ_TEST_DEADLINE_MS
// (padrão: 8000 ms) e diz em qual etapa (STEP) ele estava.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>

#include "check.h"

namespace gq {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

namespace {
std::atomic<const char*> current_step{"início do teste"};
}

void fail(const char* file, int line, const std::string& message) {
    std::fprintf(stdout, "FALHA: %s:%d: %s\n", file, line, message.c_str());
    std::fflush(stdout);
    std::_Exit(1);
}

void step(const char* what) {
    current_step.store(what);
}

void note(const std::string& text) {
    std::fprintf(stdout, "NOTA: %s\n", text.c_str());
    std::fflush(stdout);
}

}  // namespace gq

namespace {

// Watchdog do próprio avaliador (não depende do código em src/).
class Watchdog {
public:
    explicit Watchdog(std::chrono::milliseconds limit) : thread_([this, limit] { run(limit); }) {}
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    void run(std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, limit, [this] { return done_; })) {
            std::fprintf(stdout, "FALHA: tempo esgotado (%lld ms): o teste travou na etapa \"%s\"\n",
                         static_cast<long long>(limit.count()), gq::current_step.load());
            std::fflush(stdout);
            std::_Exit(124);
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    std::thread thread_;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<gq::TestCase>& tests = gq::registry();
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const gq::TestCase& t : tests) {
            std::cout << t.name << '\t' << t.description << '\n';
        }
        return 0;
    }
    if (argc != 2) {
        std::cerr << "uso: " << argv[0] << " --list | <nome-do-teste>\n";
        return 2;
    }
    for (const gq::TestCase& t : tests) {
        if (std::strcmp(argv[1], t.name) != 0) {
            continue;
        }
        long deadline_ms = 8000;
        if (const char* env = std::getenv("GQ_TEST_DEADLINE_MS")) {
            deadline_ms = std::atol(env);
        }
        {
            Watchdog watchdog{std::chrono::milliseconds(deadline_ms)};
            try {
                t.fn();
            } catch (const std::exception& e) {
                std::cout << "FALHA: exceção inesperada: " << e.what() << std::endl;
                return 1;
            } catch (...) {
                std::cout << "FALHA: exceção inesperada de tipo desconhecido" << std::endl;
                return 1;
            }
        }
        std::cout << "OK" << std::endl;
        return 0;
    }
    std::cerr << "teste desconhecido: " << argv[1] << '\n';
    return 2;
}
