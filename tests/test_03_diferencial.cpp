// Teste diferencial: 100 000 operações aleatórias (semente fixa) aplicadas ao mesmo
// tempo a gq::RingBuffer e a um modelo feito com std::deque. Qualquer divergência
// é relatada com o número da operação e as últimas operações executadas.
#include <cstdint>
#include <deque>
#include <random>
#include <sstream>
#include <string>

#include "check.h"
#include "ring_buffer.h"

namespace {

// Modelo de referência: std::deque com capacidade limitada à mão.
template <std::size_t N>
struct Model {
    std::deque<int> items;

    bool push(int v) {
        if (items.size() == N) return false;
        items.push_back(v);
        return true;
    }
    void push_overwrite(int v) {
        if (items.size() == N) items.pop_front();
        items.push_back(v);
    }
};

std::string show(const std::deque<int>& d) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < d.size(); ++i) os << (i ? ", " : "") << d[i];
    os << "]";
    return os.str();
}

template <std::size_t N>
std::string show(const gq::RingBuffer<int, N>& rb) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < rb.size() && i <= N; ++i) os << (i ? ", " : "") << rb[i];
    os << "]";
    return os.str();
}

template <std::size_t N>
void run_differential(std::uint32_t seed, int operations) {
    std::mt19937 rng(seed);  // std::mt19937 tem sequência definida pelo padrão: determinístico
    gq::RingBuffer<int, N> rb;
    Model<N> model;
    std::deque<std::string> history;  // últimas operações, para a mensagem de erro

    auto fail_here = [&](int op, const std::string& what) {
        std::ostringstream os;
        os << "capacidade " << N << ", operação #" << op << ": " << what << "\n      últimas operações:";
        for (const std::string& h : history) os << " " << h << ";";
        os << "\n      fila: " << show(rb) << " (size " << rb.size() << ")  ·  modelo std::deque: "
           << show(model.items);
        CHECK(false, os.str());
    };

    for (int op = 1; op <= operations; ++op) {
        const std::uint32_t r = rng();
        const int value = static_cast<int>(rng() % 100000u);
        std::string desc;
        switch (r % 16) {
            case 0: case 1: case 2: case 3: case 4: case 5: {
                bool got = rb.push(value);
                bool want = model.push(value);
                desc = "push(" + std::to_string(value) + ")";
                if (got != want) fail_here(op, desc + " devolveu " + (got ? "true" : "false") + ", esperado " + (want ? "true" : "false"));
                break;
            }
            case 6: case 7: case 8: {
                rb.push_overwrite(value);
                model.push_overwrite(value);
                desc = "push_overwrite(" + std::to_string(value) + ")";
                break;
            }
            case 9: case 10: case 11: case 12: case 13: {
                auto got = rb.pop();
                desc = "pop()";
                if (model.items.empty()) {
                    if (got.has_value()) fail_here(op, "pop() devolveu " + std::to_string(*got) + " com a fila vazia");
                } else {
                    int want = model.items.front();
                    model.items.pop_front();
                    if (!got.has_value()) fail_here(op, "pop() devolveu nullopt, esperado " + std::to_string(want));
                    if (*got != want) fail_here(op, "pop() devolveu " + std::to_string(*got) + ", esperado " + std::to_string(want));
                }
                break;
            }
            case 14: {
                desc = "inspeção";
                break;
            }
            default: {
                if (r % 256 == 15) {  // clear() é raro: a fila precisa ficar cheia muitas vezes
                    rb.clear();
                    model.items.clear();
                    desc = "clear()";
                } else {
                    desc = "inspeção";
                }
                break;
            }
        }
        history.push_back(desc);
        if (history.size() > 6) history.pop_front();

        const std::size_t n = model.items.size();
        if (rb.size() != n) fail_here(op, "depois de " + desc + ": size() = " + std::to_string(rb.size()) + ", esperado " + std::to_string(n));
        if (rb.empty() != (n == 0)) fail_here(op, "depois de " + desc + ": empty() incorreto");
        if (rb.full() != (n == N)) fail_here(op, "depois de " + desc + ": full() = " + (rb.full() ? "true" : "false") + " com " + std::to_string(n) + " de " + std::to_string(N) + " lugares ocupados");
        if (n == 0) continue;
        if (rb.front() != model.items.front()) fail_here(op, "depois de " + desc + ": front() = " + std::to_string(rb.front()) + ", esperado " + std::to_string(model.items.front()));
        if (rb.back() != model.items.back()) fail_here(op, "depois de " + desc + ": back() = " + std::to_string(rb.back()) + ", esperado " + std::to_string(model.items.back()));
        const std::size_t i = rng() % n;
        if (rb[i] != model.items[i]) fail_here(op, "depois de " + desc + ": rb[" + std::to_string(i) + "] = " + std::to_string(rb[i]) + ", esperado " + std::to_string(model.items[i]));
        if (op % 97 == 0) {  // de vez em quando, compara a iteração completa
            std::size_t k = 0;
            for (int v : rb) {
                if (k >= n || v != model.items[k]) fail_here(op, "a iteração (range-for) diverge do modelo na posição " + std::to_string(k));
                ++k;
            }
            if (k != n) fail_here(op, "a iteração percorreu " + std::to_string(k) + " elementos, esperado " + std::to_string(n));
        }
    }
}

}  // namespace

TEST(diferencial_capacidade_7, "100 000 operações aleatórias iguais às de um std::deque (capacidade 7)") {
    run_differential<7>(20241014u, 100000);
}

TEST(diferencial_capacidade_16, "100 000 operações aleatórias iguais às de um std::deque (capacidade 16)") {
    run_differential<16>(14u, 100000);
}

TEST(diferencial_capacidade_1, "20 000 operações aleatórias iguais às de um std::deque (capacidade 1)") {
    run_differential<1>(1u, 20000);
}
