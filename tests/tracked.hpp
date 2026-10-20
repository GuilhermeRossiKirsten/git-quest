// Tipo instrumentado: conta cópias e moves para verificar que os utilitários
// não copiam objetos que deveriam apenas encaminhar ou ler por referência.
#ifndef GQ_TESTS_TRACKED_HPP
#define GQ_TESTS_TRACKED_HPP

#include <string>
#include <utility>

struct Tracked {
    static int copies;
    static int moves;
    static void reset() { copies = moves = 0; }

    int key = 0;
    std::string payload;  // "pesado": o tipo não é trivialmente copiável

    Tracked() = default;
    Tracked(int k, std::string p) : key(k), payload(std::move(p)) {}
    Tracked(const Tracked& o) : key(o.key), payload(o.payload) { ++copies; }
    Tracked(Tracked&& o) noexcept : key(o.key), payload(std::move(o.payload)) { ++moves; }
    Tracked& operator=(const Tracked& o) {
        key = o.key;
        payload = o.payload;
        ++copies;
        return *this;
    }
    Tracked& operator=(Tracked&& o) noexcept {
        key = o.key;
        payload = std::move(o.payload);
        ++moves;
        return *this;
    }
};

int Tracked::copies = 0;
int Tracked::moves = 0;

#endif  // GQ_TESTS_TRACKED_HPP
