// arena_resource.h — adapta a Arena ao modelo std::pmr (C++17).
//
// Com um ArenaResource, qualquer contêiner std::pmr aloca na arena:
//
//   Arena arena(upstream);
//   ArenaResource res(arena);
//   std::pmr::vector<std::pmr::string> v(&res);
//
// deallocate() não faz nada: a memória volta ao upstream quando a arena é
// reiniciada ou destruída.
#ifndef GITQUEST_ARENA_RESOURCE_H
#define GITQUEST_ARENA_RESOURCE_H

#include <cstddef>
#include <memory_resource>

#include "arena.h"

class ArenaResource final : public std::pmr::memory_resource {
public:
    explicit ArenaResource(Arena& arena) noexcept : arena_(&arena) {}

    Arena& arena() const noexcept { return *arena_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    Arena* arena_;
};

#endif
