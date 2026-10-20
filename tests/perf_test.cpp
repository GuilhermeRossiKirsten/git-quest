// Teste de desempenho: compilado com -O2 e SEM sanitizers (veja o Makefile).
//
// Carga: 1 milhão de chaves compostas (id do usuário nos 32 bits altos, número
// da sessão nos 32 baixos — a primeira sessão de cada usuário é a 0, então os
// bits baixos das chaves são todos zero), depois buscas de todas elas, 1 milhão
// de buscas de chaves ausentes e a remoção de metade.
//
// Limite: kLimitSeconds. Calibração (gcc:14.2.0, container com 2 CPUs): a
// solução de referência leva 0,22–0,29 s; o limite é ~10x isso para tolerar
// máquinas lentas. Uma tabela em que essas chaves se amontoam num único
// cluster faz centenas de bilhões de sondagens, leva horas e é interrompida pelo timeout do
// tests/run.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "flat_map.hpp"

namespace {

constexpr std::uint64_t kKeys = 1000000;
constexpr std::uint64_t kStride = std::uint64_t{1} << 32;
constexpr double kLimitSeconds = 2.5;

int fail(const char* what, std::uint64_t key) {
    std::printf("FALHA %s (chave %llu)\n", what, static_cast<unsigned long long>(key));
    return 1;
}

}  // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();

    gq::FlatMap<std::uint64_t, std::uint64_t> m;
    for (std::uint64_t k = 0; k < kKeys; ++k) {
        m.insert_or_assign(k * kStride, k);
    }
    if (m.size() != kKeys) {
        return fail("size() diferente do número de chaves inseridas", 0);
    }
    for (std::uint64_t k = 0; k < kKeys; ++k) {
        const std::uint64_t* v = m.find(k * kStride);
        if (v == nullptr || *v != k) {
            return fail("chave inserida não encontrada", k * kStride);
        }
    }
    for (std::uint64_t k = 0; k < kKeys; ++k) {
        if (m.contains(k * kStride + 1)) {
            return fail("chave ausente encontrada", k * kStride + 1);
        }
    }
    for (std::uint64_t k = 0; k < kKeys; k += 2) {
        if (!m.erase(k * kStride)) {
            return fail("erase não achou a chave", k * kStride);
        }
    }
    for (std::uint64_t k = 1; k < kKeys; k += 2) {
        if (!m.contains(k * kStride)) {
            return fail("chave sumiu depois de apagar as vizinhas", k * kStride);
        }
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (seconds > kLimitSeconds) {
        std::printf("FALHA %.3f s (limite %.1f s)\n", seconds, kLimitSeconds);
        return 1;
    }
    std::printf("OK %.3f s (limite %.1f s)\n", seconds, kLimitSeconds);
    return 0;
}
