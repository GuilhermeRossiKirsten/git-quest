// Zero alocações: este binário substitui o operator new global para contar quantas
// vezes o heap é usado enquanto a fila é criada e usada. O esperado é zero.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "check.h"
#include "ring_buffer.h"
#include "support/tracked.h"
#include "telemetry_log.h"

namespace {
bool g_counting = false;
std::size_t g_allocations = 0;
std::size_t g_bytes = 0;

void* counted_alloc(std::size_t size, std::size_t align) {
    if (g_counting) {
        ++g_allocations;
        g_bytes += size;
    }
    if (size == 0) size = 1;
    void* p = nullptr;
    if (align <= alignof(std::max_align_t)) {
        p = std::malloc(size);
    } else {
        p = std::aligned_alloc(align, (size + align - 1) / align * align);
    }
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

// Liga a contagem só enquanto `body` roda; mensagens de erro são montadas depois.
template <typename Body>
std::size_t allocations_during(Body&& body) {
    g_allocations = 0;
    g_bytes = 0;
    g_counting = true;
    body();
    g_counting = false;
    return g_allocations;
}
}  // namespace

void* operator new(std::size_t size) { return counted_alloc(size, 0); }
void* operator new[](std::size_t size) { return counted_alloc(size, 0); }
void* operator new(std::size_t size, std::align_val_t a) { return counted_alloc(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) { return counted_alloc(size, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

TEST(alocacao_contador_funciona, "o contador de alocações do teste detecta um new") {
    std::size_t n = allocations_during([] {
        int* volatile p = new int(5);  // volatile: o compilador não pode eliminar o par new/delete
        delete p;
    });
    CHECK(n == 1, "o contador do teste deveria ver 1 alocação, viu " << n);
}

TEST(alocacao_int, "criar e usar RingBuffer<std::uint64_t, 128> não aloca nada no heap") {
    std::uint64_t sum = 0;
    std::size_t n = allocations_during([&sum] {
        gq::RingBuffer<std::uint64_t, 128> rb;
        for (std::uint64_t i = 0; i < 1000; ++i) {
            if (!rb.push(i)) rb.push_overwrite(i);
            if (i % 7 == 0) rb.pop();
        }
        for (std::uint64_t v : rb) sum += v;
        sum += rb.front() + rb.back() + rb[3];
        rb.clear();
    });
    CHECK(n == 0, n << " alocação(ões) no heap (" << g_bytes << " bytes) ao criar e usar a fila;"
                       " os elementos deveriam ficar dentro do próprio objeto RingBuffer");
    CHECK(sum > 0, "soma de controle inesperada");
}

TEST(alocacao_inline, "os lugares da fila ficam dentro do objeto (sizeof cobre os N elementos)") {
    constexpr std::size_t size = sizeof(gq::RingBuffer<std::uint64_t, 128>);
    CHECK(size >= 128 * sizeof(std::uint64_t),
          "sizeof(RingBuffer<std::uint64_t, 128>) = " << size << " bytes, menor que os "
              << 128 * sizeof(std::uint64_t) << " bytes de 128 elementos: o armazenamento está fora do objeto");
}

TEST(alocacao_tracked, "RingBuffer<Tracked, 8> com push/pop/overwrite/clear não aloca") {
    std::size_t n = allocations_during([] {
        gq::RingBuffer<gq_test::Tracked, 8> rb;
        for (int i = 0; i < 100; ++i) {
            rb.push_overwrite(gq_test::Tracked(i));
            if (i % 3 == 0) rb.pop();
        }
        rb.clear();
    });
    CHECK(n == 0, n << " alocação(ões) no heap (" << g_bytes << " bytes) usando RingBuffer<Tracked, 8>");
}

TEST(alocacao_telemetria, "o log de telemetria registra 1000 eventos sem alocar") {
    std::size_t kept = 0;
    std::size_t n = allocations_during([&kept] {
        gq::TelemetryLog log;
        for (int i = 0; i < 1000; ++i) log.record(i % 2 ? gq::Severity::info : gq::Severity::error, i);
        kept = log.size() + log.count(gq::Severity::error);
    });
    CHECK(n == 0, n << " alocação(ões) no heap (" << g_bytes << " bytes) ao usar gq::TelemetryLog");
    CHECK(kept > 0, "o log deveria guardar eventos");
}
