// compare.h — compara um LogIndex com a implementação de referência.
#ifndef GITQUEST_COMPARE_H
#define GITQUEST_COMPARE_H

#include <sstream>
#include <string>

#include "../../src/log_index.h"
#include "corpus.h"

namespace gq {

// Devolve "" se tudo bate; senão, a primeira divergência encontrada.
inline std::string diff_with_reference(const LogIndex& idx, const Reference& ref) {
    std::ostringstream os;
    if (idx.lines() != ref.accepted.size()) {
        os << "lines() == " << idx.lines() << ", esperado " << ref.accepted.size();
        return os.str();
    }
    if (idx.rejected() != ref.rejected) {
        os << "rejected() == " << idx.rejected() << ", esperado " << ref.rejected;
        return os.str();
    }
    if (idx.services() != ref.per_level.size()) {
        os << "services() == " << idx.services() << ", esperado " << ref.per_level.size();
        return os.str();
    }
    for (std::size_t i = 0; i < ref.accepted.size(); ++i) {
        const Record& r = *ref.accepted[i];
        if (idx.message(i) != r.message) {
            os << "message(" << i << ") == \"" << idx.message(i) << "\", esperado \"" << r.message << "\"";
            return os.str();
        }
        if (idx.service(i) != r.service) {
            os << "service(" << i << ") == \"" << idx.service(i) << "\", esperado \"" << r.service << "\"";
            return os.str();
        }
        if (idx.level(i) != r.level) {
            os << "level(" << i << ") == " << level_name(idx.level(i)) << ", esperado " << level_name(r.level);
            return os.str();
        }
    }
    for (const auto& [name, counts] : ref.per_level) {
        if (idx.count(name) != ref.total(name)) {
            os << "count(\"" << name << "\") == " << idx.count(name) << ", esperado " << ref.total(name);
            return os.str();
        }
        for (std::size_t l = 0; l < kLevelCount; ++l) {
            const Level level = static_cast<Level>(l);
            if (idx.count(name, level) != counts[l]) {
                os << "count(\"" << name << "\", " << level_name(level) << ") == " << idx.count(name, level)
                   << ", esperado " << counts[l];
                return os.str();
            }
        }
    }
    if (idx.count("servico-que-nao-existe") != 0) {
        os << "count() de um serviço inexistente deveria ser 0";
        return os.str();
    }
    return std::string();
}

}  // namespace gq

#endif
