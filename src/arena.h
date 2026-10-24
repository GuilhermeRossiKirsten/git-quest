// arena.h — arena de alocação sequencial ("bump allocator") do indexador.
//
// A arena pede memória ao `upstream` em blocos grandes e a entrega em pedaços,
// avançando um ponteiro. Não existe liberação individual: tudo volta ao
// upstream de uma vez em reset() ou no destrutor.
//
// Contrato (é o que os testes verificam):
//   * allocate(bytes, alignment) devolve memória alinhada a `alignment` (uma
//     potência de 2), inclusive para tipos superalinhados como alignas(64) —
//     e nunca sobrepõe pedaços entregues antes;
//   * pedidos maiores que um bloco também funcionam (recebem um bloco próprio);
//   * os blocos vêm do upstream sempre em pedaços de pelo menos `chunk_size`
//     bytes (nada de uma ida ao upstream por alocação);
//   * make<T>(args...) constrói um T na arena; os objetos criados por make()
//     são destruídos em ordem INVERSA à de criação, em reset() e no destrutor;
//   * reset() devolve todos os blocos ao upstream e deixa a arena pronta para
//     ser usada de novo.
//
// Não é thread-safe.
#ifndef GITQUEST_ARENA_H
#define GITQUEST_ARENA_H

#include <cstddef>
#include <memory_resource>
#include <new>
#include <utility>

class Arena {
public:
    static constexpr std::size_t kDefaultChunkSize = 64 * 1024;

    explicit Arena(std::pmr::memory_resource* upstream = std::pmr::get_default_resource(),
                   std::size_t chunk_size = kDefaultChunkSize);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));

    template <class T, class... Args>
    T* make(Args&&... args) {
        // A arena devolve a memória toda de uma vez em reset(): não há nada a
        // liberar objeto por objeto.
        void* p = allocate(sizeof(T), alignof(T));
        return ::new (p) T(std::forward<Args>(args)...);
    }

    void reset() noexcept;

    std::pmr::memory_resource* upstream() const noexcept { return upstream_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }

private:
    struct Chunk {
        Chunk* next;
        std::size_t size;  // tamanho total pedido ao upstream (cabeçalho incluído)
    };

    void add_chunk(std::size_t min_payload);

    std::pmr::memory_resource* upstream_;
    std::size_t chunk_size_;
    Chunk* chunks_ = nullptr;  // lista dos blocos, o mais recente primeiro
    std::byte* cur_ = nullptr;
    std::byte* end_ = nullptr;
};

#endif
