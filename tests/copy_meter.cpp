// copy_meter.cpp — operator new/delete instrumentados (veja copy_meter.h).
//
// Funciona junto com o AddressSanitizer: as substituições alocam com malloc e
// liberam com free, que o ASan continua interceptando (vazamentos, double free
// e uso após liberação seguem sendo detectados).
#include "copy_meter.h"

#include <cstdlib>
#include <new>

namespace {

std::size_t g_watched = 0;  // 0 = medição desligada
long g_count = 0;

void* counted_alloc(std::size_t n) {
    if (g_watched != 0 && (n == g_watched || n == g_watched + 1)) {
        ++g_count;
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}

}  // namespace

void* operator new(std::size_t n) { return counted_alloc(n); }
void* operator new[](std::size_t n) { return counted_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace gq {

CopyMeter::CopyMeter(std::size_t payload_size) {
    g_watched = payload_size;
    g_count = 0;
}

CopyMeter::~CopyMeter() { g_watched = 0; }

long CopyMeter::allocations() const { return g_count; }

}  // namespace gq
