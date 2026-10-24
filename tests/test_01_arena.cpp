// Testes da Arena: alinhamento, pedidos grandes, destrutores, reset e blocos.
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "support/life_probe.h"
#include "support/resources.h"
#include "../src/arena.h"

namespace {

struct alignas(16) Vec4 {
    float x, y, z, w;
};

struct alignas(64) CacheLine {
    std::uint64_t v[8];
};

// Um objeto que possui memória no heap: se o destrutor não rodar, vaza.
struct Owner {
    std::string text;
    std::vector<int> numbers;
};

}  // namespace

TEST(arena_alinhamento_basico, "allocate() respeita alinhamentos de 2 a 16 bytes depois de pedidos de tamanho ímpar") {
    gq::MallocResource up;
    Arena arena(&up, 4096);
    for (std::size_t prefix = 1; prefix <= 40; ++prefix) {
        arena.allocate(prefix, 1);  // desalinha o cursor de propósito
        for (std::size_t alignment = 2; alignment <= alignof(std::max_align_t); alignment *= 2) {
            void* p = arena.allocate(3 * alignment, alignment);
            CHECK(gq::is_aligned(p, alignment), "allocate(" << 3 * alignment << ", " << alignment << ") depois de um pedido de "
                                                        << prefix << " byte(s) devolveu " << p
                                                        << ", que não é múltiplo de " << alignment);
        }
    }
    // Tipos comuns construídos com make(): o UBSan também confere o alinhamento.
    for (int i = 0; i < 8; ++i) {
        arena.allocate(1, 1);
        Vec4* v = arena.make<Vec4>(Vec4{1, 2, 3, 4});
        CHECK(gq::is_aligned(v, alignof(Vec4)), "make<Vec4>() (alignas(16)) devolveu " << static_cast<void*>(v));
        long double* ld = arena.make<long double>(1.5L);
        CHECK(gq::is_aligned(ld, alignof(long double)), "make<long double>() devolveu " << static_cast<void*>(ld));
    }
}

TEST(arena_alinhamento_64, "alignas(64) e alinhamentos de até 4096 bytes são respeitados, inclusive em bloco novo") {
    gq::MallocResource up;
    Arena arena(&up, 1024);
    for (std::size_t prefix = 1; prefix <= 130; ++prefix) {
        arena.allocate(prefix, 1);
        void* raw = arena.allocate(sizeof(CacheLine), alignof(CacheLine));
        CHECK(gq::is_aligned(raw, 64), "allocate(64, 64) depois de " << prefix << " byte(s) devolveu " << raw
                                                                      << ", que não é múltiplo de 64");
        CacheLine* line = arena.make<CacheLine>();
        CHECK(gq::is_aligned(line, 64), "make<CacheLine>() (alignas(64)) devolveu " << static_cast<void*>(line));
        line->v[0] = prefix;
    }
    for (std::size_t alignment = 128; alignment <= 4096; alignment *= 2) {
        arena.allocate(3, 1);
        void* p = arena.allocate(100, alignment);
        CHECK(gq::is_aligned(p, alignment), "allocate(100, " << alignment << ") devolveu " << p);
    }
    // Maior que um bloco inteiro (chunk_size = 1024) e superalinhado.
    arena.allocate(5, 1);
    void* big = arena.allocate(5000, 64);
    CHECK(gq::is_aligned(big, 64), "allocate(5000, 64) com chunk_size 1024 devolveu " << big);
    std::memset(big, 0x5A, 5000);
}

TEST(arena_sem_sobreposicao, "pedidos variados (inclusive maiores que um bloco) nunca se sobrepõem") {
    gq::MallocResource up;
    Arena arena(&up, 2048);
    struct Piece {
        unsigned char* p;
        std::size_t n;
        unsigned char tag;
    };
    std::vector<Piece> pieces;
    const std::size_t sizes[] = {1, 7, 64, 3, 300, 4096, 13, 2048, 9000, 31, 1};
    const std::size_t aligns[] = {1, 8, 64, 2, 16, 256, 4, 64, 32, 8, 1};
    for (int round = 0; round < 20; ++round) {
        for (std::size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            auto* p = static_cast<unsigned char*>(arena.allocate(sizes[i], aligns[i]));
            CHECK(p != nullptr, "allocate devolveu nullptr");
            const auto tag = static_cast<unsigned char>(pieces.size() * 31 + 7);
            std::memset(p, tag, sizes[i]);
            pieces.push_back({p, sizes[i], tag});
        }
    }
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        for (std::size_t j = 0; j < pieces[i].n; ++j) {
            CHECK(pieces[i].p[j] == pieces[i].tag, "o pedaço " << i << " (" << pieces[i].n
                                                               << " bytes) foi sobrescrito por outro pedido");
        }
    }
}

TEST(arena_destrutores, "make<T>() registra o destrutor: reset() e ~Arena() destroem em ordem inversa") {
    gq::MallocResource up;
    gq::LifeProbe::destroyed().clear();
    {
        Arena arena(&up, 4096);
        for (int i = 0; i < 5; ++i) {
            arena.make<gq::LifeProbe>(i);
            arena.make<int>(i);  // tipos triviais podem ser misturados
        }
        CHECK(gq::LifeProbe::live() == 5, "esperava 5 LifeProbe vivos, há " << gq::LifeProbe::live());
        arena.reset();
        CHECK(gq::LifeProbe::live() == 0, "depois de reset() ainda há " << gq::LifeProbe::live()
                                                                        << " LifeProbe vivo(s): os destrutores não rodaram");
        CHECK(gq::join(gq::LifeProbe::destroyed()) == "[4, 3, 2, 1, 0]",
              "reset() destruiu na ordem " << gq::join(gq::LifeProbe::destroyed()) << "; esperado [4, 3, 2, 1, 0]");

        gq::LifeProbe::destroyed().clear();
        for (int i = 10; i < 13; ++i) {
            arena.make<gq::LifeProbe>(i);
        }
        Owner* o = arena.make<Owner>();
        o->text.assign(500, 'x');  // memória no heap, devolvida só pelo destrutor
        o->numbers.assign(100, 7);
    }
    CHECK(gq::LifeProbe::live() == 0, "depois de ~Arena() ainda há " << gq::LifeProbe::live() << " LifeProbe vivo(s)");
    CHECK(gq::join(gq::LifeProbe::destroyed()) == "[12, 11, 10]",
          "~Arena() destruiu na ordem " << gq::join(gq::LifeProbe::destroyed()) << "; esperado [12, 11, 10]");
    CHECK(up.outstanding() == 0, "a arena não devolveu " << up.outstanding() << " bloco(s) ao upstream");
}

TEST(arena_reset_devolve_blocos, "reset() devolve todos os blocos ao upstream e a arena continua utilizável") {
    gq::MallocResource up;
    {
        Arena arena(&up, 1024);
        for (int i = 0; i < 50; ++i) {
            std::memset(arena.allocate(300, 8), i, 300);
        }
        CHECK(up.outstanding() > 0, "a arena deveria ter pedido blocos ao upstream");
        arena.reset();
        CHECK(up.outstanding() == 0, "depois de reset() ainda há " << up.outstanding() << " bloco(s) com a arena");
        auto* p = static_cast<char*>(arena.allocate(10, 1));
        std::memcpy(p, "123456789", 10);
        CHECK(std::string(p) == "123456789", "a arena não funcionou depois de reset()");
    }
    CHECK(up.outstanding() == 0, "~Arena() não devolveu " << up.outstanding() << " bloco(s) ao upstream");
}

TEST(arena_blocos_grandes, "100 mil alocações pequenas viram poucas idas ao upstream (blocos de chunk_size)") {
    gq::MallocResource up;
    Arena arena(&up, 64 * 1024);
    std::size_t requested = 0;
    for (int i = 0; i < 100000; ++i) {
        const std::size_t n = 8 + static_cast<std::size_t>(i % 5) * 8;
        arena.allocate(n, 8);
        requested += n;
    }
    const long budget = static_cast<long>(requested / (64 * 1024)) * 2 + 4;
    CHECK(up.allocations() <= budget, "a arena chamou o upstream " << up.allocations() << " vezes para " << requested
                                                                    << " bytes; o orçamento é " << budget
                                                                    << " (blocos de 64 KiB)");
    CHECK(up.bytes() <= requested * 3, "a arena pediu " << up.bytes() << " bytes ao upstream para entregar "
                                                        << requested << " (desperdício acima de 3x)");
}
