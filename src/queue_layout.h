// queue_layout.h — API de introspecção do layout de SpscQueue.
//
// NÃO edite este arquivo: ele é o contrato entre a fila e o avaliador.
//
// Por que uma API de introspecção? False sharing é um problema de DESEMPENHO e
// medi-lo (tempo, perf, cachegrind) num container com CPUs compartilhadas não é
// determinístico: o mesmo código passa e falha conforme a carga da máquina. O
// que causa o problema, porém, é estrutural e 100% determinístico: em que linha
// de cache (blocos de 64 bytes) cada campo quente mora. SpscQueue::layout_info()
// descreve essa estrutura e os testes conferem a descrição contra os endereços
// reais dos índices (veja tests/test_04_layout.cpp).
#ifndef MARKETFEED_QUEUE_LAYOUT_H
#define MARKETFEED_QUEUE_LAYOUT_H

#include <cstddef>
#include <cstdint>
#include <limits>

// Tamanho da linha de cache assumido pelo avaliador (x86-64 e a maioria dos ARM64).
// std::hardware_destructive_interference_size vale 64 no g++ 14 para x86-64.
inline constexpr std::size_t kCacheLineSize = 64;

struct QueueLayout {
    // Valor para um campo que a fila não tem.
    static constexpr std::size_t kAbsent = std::numeric_limits<std::size_t>::max();

    std::size_t object_size = 0;   // sizeof da fila
    std::size_t object_align = 0;  // alignof da fila

    // Deslocamentos em bytes a partir do início do objeto (como offsetof).
    std::size_t producer_index = kAbsent;  // índice escrito SÓ pelo produtor ("tail")
    std::size_t consumer_index = kAbsent;  // índice escrito SÓ pelo consumidor ("head")
    std::size_t producer_cache = kAbsent;  // cópia do índice do consumidor, privada do produtor
    std::size_t consumer_cache = kAbsent;  // cópia do índice do produtor, privada do consumidor
};

// Deslocamento de `member` dentro do objeto que começa em `object`.
// Útil para implementar layout_info() sem offsetof (que não vale para tipos
// que não são standard-layout).
inline std::size_t byte_offset(const void* object, const void* member) noexcept {
    return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(member) -
                                    reinterpret_cast<std::uintptr_t>(object));
}

#endif
