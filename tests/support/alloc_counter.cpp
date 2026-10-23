// operator new/delete instrumentados (veja alloc_counter.hpp).
#include "support/alloc_counter.hpp"

#include <cstdlib>
#include <new>

namespace {

bool g_active = false;
long g_count = 0;

void* counted_alloc(std::size_t n) {
  if (g_active) ++g_count;
  void* p = std::malloc(n == 0 ? 1 : n);
  if (!p) throw std::bad_alloc();
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

AllocCounter::AllocCounter() {
  g_count = 0;
  g_active = true;
}

AllocCounter::~AllocCounter() { g_active = false; }

long AllocCounter::count() const { return g_count; }

void AllocCounter::reset() { g_count = 0; }

}  // namespace gq
