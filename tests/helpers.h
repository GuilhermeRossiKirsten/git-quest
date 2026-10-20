// helpers.h — utilidades comuns aos testes.
#ifndef GITQUEST_HELPERS_H
#define GITQUEST_HELPERS_H

#include <cstddef>
#include <sstream>
#include <string>

#include "../src/buffer.h"

namespace gq {

// Tamanho dos payloads "grandes" (~98 KiB). Ímpar de propósito: veja copy_meter.h.
const std::size_t kPayload = 100003;

inline char fill_for(std::size_t i) { return static_cast<char>('a' + i % 26); }

inline std::string topic_for(std::size_t i) {
    std::ostringstream os;
    os << "orders." << i;
    return os.str();
}

// true se `b` tem exatamente `size` bytes, todos iguais a `fill`.
inline bool filled_with(const Buffer& b, std::size_t size, char fill) {
    if (b.size() != size) {
        return false;
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (b[i] != fill) {
            return false;
        }
    }
    return true;
}

}  // namespace gq

#endif
