// heap_meter.cpp — operator new/delete globais instrumentados (veja heap_meter.h).
#include "heap_meter.h"

#include <cstdlib>
#include <new>

namespace {

bool g_active = false;
long g_count = 0;
std::size_t g_bytes = 0;

void note(std::size_t n) {
    if (g_active) {
        ++g_count;
        g_bytes += n;
    }
}

void* plain_alloc(std::size_t n) {
    note(n);
    return std::malloc(n == 0 ? 1 : n);
}

void* aligned_alloc_for(std::size_t n, std::align_val_t al) {
    note(n);
    std::size_t a = static_cast<std::size_t>(al);
    if (a < sizeof(void*)) {
        a = sizeof(void*);
    }
    const std::size_t size = (n == 0 ? a : (n + a - 1) / a * a);
    return std::aligned_alloc(a, size);
}

void* or_throw(void* p) {
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

}  // namespace

void* operator new(std::size_t n) { return or_throw(plain_alloc(n)); }
void* operator new[](std::size_t n) { return or_throw(plain_alloc(n)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return plain_alloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return plain_alloc(n); }
void* operator new(std::size_t n, std::align_val_t al) { return or_throw(aligned_alloc_for(n, al)); }
void* operator new[](std::size_t n, std::align_val_t al) { return or_throw(aligned_alloc_for(n, al)); }
void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
    return aligned_alloc_for(n, al);
}
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
    return aligned_alloc_for(n, al);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }

namespace gq {

HeapMeter::HeapMeter() {
    g_count = 0;
    g_bytes = 0;
    g_active = true;
}

HeapMeter::~HeapMeter() { g_active = false; }

long HeapMeter::allocations() const { return g_count; }

std::size_t HeapMeter::bytes() const { return g_bytes; }

}  // namespace gq
