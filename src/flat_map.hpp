// gq::FlatMap — tabela hash com endereçamento aberto e sondagem linear.
//
// Escrita para o serviço de sessões, que precisava de algo mais compacto que
// std::unordered_map (um nó alocado por elemento). Todos os pares ficam num
// único vetor contíguo de slots; a capacidade é sempre potência de 2, então o
// índice inicial é calculado com uma máscara em vez de uma divisão.
//
// Requisitos de K e V: construíveis por padrão e atribuíveis por cópia/movimento.
// K precisa de operator== e de um Hash compatível.
#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace gq {

template <class K, class V, class Hash = std::hash<K>>
class FlatMap {
public:
    FlatMap() : slots_(kInitialCapacity) {}

    // Insere ou sobrescreve. Devolve true se a chave não existia.
    bool insert_or_assign(const K& key, V value) {
        // Cresce antes de ficar cheia demais (fator de carga máximo 0,75).
        if (size_ > capacity() * 3 / 4) {
            grow();
        }
        std::size_t i = home(key);
        while (slots_[i].state == State::Full && !(slots_[i].key == key)) {
            i = next(i);
        }
        Slot& slot = slots_[i];
        const bool inserted = slot.state != State::Full;
        slot.key = key;
        slot.value = std::move(value);
        slot.state = State::Full;
        ++size_;
        return inserted;
    }

    // Ponteiro para o valor associado à chave, ou nullptr se ela não existe.
    V* find(const K& key) {
        const std::size_t i = locate(key);
        return i == npos ? nullptr : &slots_[i].value;
    }

    const V* find(const K& key) const {
        const std::size_t i = locate(key);
        return i == npos ? nullptr : &slots_[i].value;
    }

    bool contains(const K& key) const { return locate(key) != npos; }

    // Remove a chave. Devolve true se ela existia.
    bool erase(const K& key) {
        const std::size_t i = locate(key);
        if (i == npos) {
            return false;
        }
        // Libera o slot para ser reaproveitado pela próxima inserção.
        slots_[i].state = State::Empty;
        --size_;
        return true;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::size_t capacity() const { return slots_.size(); }

    void clear() {
        for (Slot& slot : slots_) {
            slot.state = State::Empty;
        }
        size_ = 0;
    }

    // Chama f(chave, valor) para cada elemento, em ordem não especificada.
    template <class F>
    void for_each(F&& f) const {
        for (const Slot& slot : slots_) {
            if (slot.state == State::Full) {
                f(slot.key, slot.value);
            }
        }
    }

private:
    enum class State : unsigned char { Empty, Full, Deleted };

    struct Slot {
        State state = State::Empty;
        K key{};
        V value{};
    };

    static constexpr std::size_t kInitialCapacity = 8;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    std::size_t mask() const { return slots_.size() - 1; }
    std::size_t home(const K& key) const { return Hash{}(key) & mask(); }
    std::size_t next(std::size_t i) const { return (i + 1) & mask(); }

    // Índice do slot que contém a chave, ou npos.
    std::size_t locate(const K& key) const {
        std::size_t i = home(key);
        while (slots_[i].state != State::Empty) {
            if (slots_[i].state == State::Full && slots_[i].key == key) {
                return i;
            }
            i = next(i);
        }
        return npos;
    }

    // Dobra a capacidade e reposiciona todos os elementos.
    void grow() {
        std::vector<Slot> old = std::move(slots_);
        slots_ = std::vector<Slot>(old.size() * 2);
        size_ = 0;
        for (Slot& slot : old) {
            if (slot.state != State::Empty) {
                insert_or_assign(slot.key, std::move(slot.value));
            }
        }
    }

    std::vector<Slot> slots_;
    std::size_t size_ = 0;
};

}  // namespace gq
