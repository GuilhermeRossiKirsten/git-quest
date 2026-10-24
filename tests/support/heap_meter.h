// heap_meter.h — conta alocações no heap global (operator new).
//
// tests/support/heap_meter.cpp substitui TODAS as formas do operator new/delete
// globais do binário de testes (simples, de arrays, nothrow e alinhadas).
// Enquanto um HeapMeter existe, cada chamada a operator new é contada.
//
// std::pmr::new_delete_resource() e std::allocator usam o operator new global,
// então qualquer coisa que "escape" da arena aparece aqui. As substituições
// alocam com malloc/aligned_alloc, que o AddressSanitizer continua
// interceptando (vazamentos e uso após liberação seguem sendo detectados).
#ifndef GITQUEST_HEAP_METER_H
#define GITQUEST_HEAP_METER_H

#include <cstddef>

namespace gq {

class HeapMeter {
public:
    HeapMeter();
    ~HeapMeter();
    HeapMeter(const HeapMeter&) = delete;
    HeapMeter& operator=(const HeapMeter&) = delete;

    long allocations() const;  // chamadas a operator new desde a construção
    std::size_t bytes() const;  // bytes pedidos nessas chamadas
};

}  // namespace gq

#endif
