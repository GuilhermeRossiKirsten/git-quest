// reference_pq.h — modelo de referência (lento, mas obviamente correto) da fila indexada.
//
// Usa um std::set ordenado por (prioridade, sequência, chave) e um índice chave -> entrada.
// A sequência implementa a regra de desempate do contrato: entre prioridades
// equivalentes sai primeiro quem teve a prioridade definida (push/update) há mais tempo.
#ifndef GQ_TESTS_REFERENCE_PQ_H
#define GQ_TESTS_REFERENCE_PQ_H

#include <cstddef>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace gq::testing {

template <typename Key, typename Priority>
class ReferencePQ {
public:
    bool push(const Key& key, const Priority& priority) {
        if (index_.count(key) != 0) {
            return false;
        }
        insert(key, priority);
        return true;
    }

    bool update_priority(const Key& key, const Priority& priority) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }
        order_.erase(Entry{it->second.first, it->second.second, key});
        index_.erase(it);
        insert(key, priority);
        return true;
    }

    bool erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }
        order_.erase(Entry{it->second.first, it->second.second, key});
        index_.erase(it);
        return true;
    }

    std::pair<Key, Priority> pop() {
        if (order_.empty()) {
            throw std::out_of_range("ReferencePQ::pop: vazio");
        }
        auto first = order_.begin();
        std::pair<Key, Priority> out(std::get<2>(*first), std::get<0>(*first));
        index_.erase(out.first);
        order_.erase(first);
        return out;
    }

    const Key& top_key() const { return std::get<2>(*order_.begin()); }
    const Priority& top_priority() const { return std::get<0>(*order_.begin()); }
    bool contains(const Key& key) const { return index_.count(key) != 0; }
    const Priority& priority(const Key& key) const { return index_.at(key).first; }
    std::size_t size() const { return order_.size(); }
    bool empty() const { return order_.empty(); }

    template <typename Fn>
    void for_each(Fn fn) const {
        for (const auto& [key, entry] : index_) {
            fn(key, entry.first);
        }
    }

private:
    using Entry = std::tuple<Priority, unsigned long long, Key>;

    void insert(const Key& key, const Priority& priority) {
        index_.emplace(key, std::make_pair(priority, seq_));
        order_.emplace(priority, seq_, key);
        ++seq_;
    }

    std::set<Entry> order_;
    std::unordered_map<Key, std::pair<Priority, unsigned long long>> index_;
    unsigned long long seq_ = 0;
};

}  // namespace gq::testing

#endif
