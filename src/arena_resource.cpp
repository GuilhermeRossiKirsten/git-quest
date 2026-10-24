// arena_resource.cpp — memory_resource sobre a Arena.
#include "arena_resource.h"

void* ArenaResource::do_allocate(std::size_t bytes, std::size_t alignment) {
    return arena_->allocate(bytes, alignment);
}

void ArenaResource::do_deallocate(void*, std::size_t, std::size_t) {
    // Nada a fazer: a arena libera tudo de uma vez.
}

bool ArenaResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    // Nenhuma ArenaResource devolve memória em deallocate(), então liberar por
    // uma ou por outra dá no mesmo: são todas intercambiáveis.
    return dynamic_cast<const ArenaResource*>(&other) != nullptr;
}
