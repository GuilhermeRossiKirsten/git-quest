// resources.h — memory resources de teste.
//
// MallocResource é o "upstream" usado pelos testes: pede memória direto ao
// malloc/aligned_alloc (fora do operator new, então não aparece no HeapMeter)
// e conta quantas vezes foi chamado. Como a memória volta com free(), o
// AddressSanitizer acusa qualquer uso de um bloco depois que ele foi devolvido.
#ifndef GITQUEST_RESOURCES_H
#define GITQUEST_RESOURCES_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory_resource>
#include <new>

namespace gq {

class MallocResource final : public std::pmr::memory_resource {
public:
    MallocResource() = default;
    MallocResource(const MallocResource&) = delete;
    MallocResource& operator=(const MallocResource&) = delete;

    long allocations() const { return allocations_; }    // chamadas a allocate()
    long outstanding() const { return outstanding_; }    // blocos ainda não devolvidos
    std::size_t bytes() const { return bytes_; }         // bytes pedidos no total
    std::size_t largest_alignment() const { return largest_alignment_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        std::size_t a = alignment < alignof(std::max_align_t) ? alignof(std::max_align_t) : alignment;
        const std::size_t size = bytes == 0 ? a : (bytes + a - 1) / a * a;
        void* p = std::aligned_alloc(a, size);
        if (p == nullptr) {
            throw std::bad_alloc();
        }
        ++allocations_;
        ++outstanding_;
        bytes_ += bytes;
        if (alignment > largest_alignment_) {
            largest_alignment_ = alignment;
        }
        return p;
    }

    void do_deallocate(void* p, std::size_t, std::size_t) override {
        --outstanding_;
        std::free(p);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }

    long allocations_ = 0;
    long outstanding_ = 0;
    std::size_t bytes_ = 0;
    std::size_t largest_alignment_ = 0;
};

inline bool is_aligned(const void* p, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
}

}  // namespace gq

#endif
