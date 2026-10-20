// copy_meter.h — mede cópias profundas de payload contando alocações.
//
// tests/copy_meter.cpp substitui o operator new/delete global do binário de
// testes. Enquanto um CopyMeter existe, toda alocação de exatamente `payload`
// ou `payload + 1` bytes é contada: cada uma é um bloco do tamanho de um
// payload, ou seja, um payload criado ou copiado. Os testes usam tamanhos
// ímpares, que nunca coincidem com vetores de Message (múltiplos de 8 bytes).
//
// Medir alocações (e não chamadas a construtores) torna a medição
// independente de como Buffer e Message são implementados.
#ifndef GITQUEST_COPY_METER_H
#define GITQUEST_COPY_METER_H

#include <cstddef>

namespace gq {

class CopyMeter {
public:
    explicit CopyMeter(std::size_t payload_size);
    ~CopyMeter();
    long allocations() const;  // blocos do tamanho de um payload alocados até agora

private:
    CopyMeter(const CopyMeter&);
    CopyMeter& operator=(const CopyMeter&);
};

}  // namespace gq

#endif
