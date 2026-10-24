// Testes do ArenaResource: igualdade, alinhamento via pmr e movimento entre arenas.
#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "check.h"
#include "support/resources.h"
#include "../src/arena.h"
#include "../src/arena_resource.h"

namespace {

struct alignas(64) Slot {
    std::uint64_t value = 0;
};

std::string long_text(int i) {
    return "mensagem longa o bastante para sair do SSO #" + std::to_string(i);
}

}  // namespace

TEST(resource_igualdade, "uma ArenaResource só é igual a si mesma (do_is_equal)") {
    gq::MallocResource up;
    Arena a(&up), b(&up);
    ArenaResource ra(a), ra2(a), rb(b);
    CHECK(ra.is_equal(ra), "ra.is_equal(ra) deveria ser true");
    CHECK(!ra.is_equal(rb), "ra.is_equal(rb) é true, mas ra e rb entregam memória de arenas diferentes: "
                            "memória de uma não pode ser \"liberada\" pela outra");
    CHECK(!(std::pmr::polymorphic_allocator<int>(&ra) == std::pmr::polymorphic_allocator<int>(&rb)),
          "polymorphic_allocator sobre arenas diferentes compara igual");
    CHECK(!ra.is_equal(*std::pmr::new_delete_resource()), "ArenaResource não pode ser igual a new_delete_resource()");
    // Duas ArenaResource sobre a MESMA arena podem (ou não) ser iguais: as duas
    // respostas são aceitáveis, desde que a relação seja simétrica.
    CHECK(ra.is_equal(ra2) == ra2.is_equal(ra), "is_equal não é simétrica");
}

TEST(resource_alinhamento_pmr, "pmr::vector de alignas(64) e monotonic_buffer_resource sobre ArenaResource ficam alinhados") {
    gq::MallocResource up;
    Arena arena(&up, 4096);
    ArenaResource res(arena);
    std::pmr::vector<char> noise(&res);
    for (int round = 0; round < 40; ++round) {
        noise.push_back('x');  // realocações de tamanhos variados desalinham o cursor
        std::pmr::vector<Slot> slots(&res);
        slots.resize(static_cast<std::size_t>(round + 1));
        CHECK(gq::is_aligned(slots.data(), 64), "pmr::vector<Slot> (alignas(64)) recebeu "
                                                    << static_cast<void*>(slots.data()) << " na rodada " << round);
        slots.back().value = static_cast<std::uint64_t>(round);
    }
    std::pmr::monotonic_buffer_resource mono(64, &res);
    for (int i = 0; i < 50; ++i) {
        mono.allocate(static_cast<std::size_t>(i % 7 + 1), 1);
        void* p = mono.allocate(sizeof(Slot), alignof(Slot));
        CHECK(gq::is_aligned(p, 64), "monotonic_buffer_resource com upstream ArenaResource devolveu " << p);
    }
}

TEST(resource_move_entre_arenas, "mover contêineres pmr entre arenas diferentes não deixa ponteiros para a arena de origem") {
    gq::MallocResource up;
    Arena dst_arena(&up);
    ArenaResource dst_res(dst_arena);
    std::pmr::vector<std::pmr::string> dst(&dst_res);
    std::pmr::string single(&dst_res);
    {
        Arena src_arena(&up);
        ArenaResource src_res(src_arena);
        std::pmr::vector<std::pmr::string> src(&src_res);
        for (int i = 0; i < 64; ++i) {
            src.emplace_back(long_text(i));
        }
        std::pmr::string one(long_text(999), &src_res);
        dst = std::move(src);       // allocators diferentes: precisa copiar para dst_res
        single = std::move(one);    // idem para uma string
    }  // a arena de origem devolve os blocos ao upstream aqui
    CHECK(dst.size() == 64, "dst deveria ter 64 strings, tem " << dst.size());
    for (int i = 0; i < 64; ++i) {
        CHECK(std::string_view(dst[static_cast<std::size_t>(i)]) == long_text(i), "dst[" << i << "] corrompida depois que a arena de origem morreu");
    }
    CHECK(std::string_view(single) == long_text(999), "a string movida ficou corrompida depois que a arena de origem morreu");
    CHECK(dst.get_allocator().resource() == &dst_res, "dst deveria continuar usando o próprio resource");
}
