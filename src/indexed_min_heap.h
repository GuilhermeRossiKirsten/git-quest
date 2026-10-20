// indexed_min_heap.h — fila de prioridade indexada (min-heap binário + mapa de posições).
//
// Diferente de std::priority_queue, cada elemento tem uma CHAVE única, e a chave permite
// consultar, alterar a prioridade ou remover um elemento que está no meio do heap.
//
// Contrato público (os testes dependem dele):
//
//   bool push(key, priority)            insere; false (e nada muda) se a chave já existe
//   const Key& top_key() const          chave de menor prioridade; std::out_of_range se vazio
//   const Priority& top_priority() const
//   std::pair<Key, Priority> pop()      remove e devolve o topo; std::out_of_range se vazio
//   bool contains(key) const
//   const Priority& priority(key) const prioridade atual; std::out_of_range se ausente
//   bool update_priority(key, p)        diminui OU aumenta a prioridade; false se ausente
//   bool erase(key)                     remove a chave de qualquer posição; false se ausente
//   std::size_t size() const, bool empty() const, void clear()
//
// Ordem: menor prioridade primeiro (só operator< de Priority é usado).
// Empates: entre prioridades equivalentes sai primeiro quem teve a prioridade definida
// há mais tempo — a ordem de push(), sendo que update_priority() conta como uma nova
// definição e manda o elemento para o fim da fila daquela prioridade (FIFO).
//
// Complexidade esperada: push, pop, update_priority e erase em O(log n);
// top, contains, priority, size e empty em O(1) (médio, pelo mapa de posições).
#ifndef GQ_INDEXED_MIN_HEAP_H
#define GQ_INDEXED_MIN_HEAP_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gq {

template <typename Key, typename Priority, typename Hash = std::hash<Key>>
class IndexedMinHeap {
public:
    bool push(const Key& key, const Priority& priority) {
        if (pos_.count(key) != 0) {
            return false;
        }
        pos_[key] = heap_.size();
        heap_.push_back(Node{key, priority});
        sift_up(heap_.size() - 1);
        return true;
    }

    const Key& top_key() const {
        require_non_empty("top_key");
        return heap_.front().key;
    }

    const Priority& top_priority() const {
        require_non_empty("top_priority");
        return heap_.front().priority;
    }

    std::pair<Key, Priority> pop() {
        require_non_empty("pop");
        std::pair<Key, Priority> out(heap_.front().key, heap_.front().priority);
        swap_nodes(0, heap_.size() - 1);
        pos_.erase(out.first);
        heap_.pop_back();
        if (!heap_.empty()) {
            sift_down(0);
        }
        return out;
    }

    bool contains(const Key& key) const { return pos_.count(key) != 0; }

    const Priority& priority(const Key& key) const {
        auto it = pos_.find(key);
        if (it == pos_.end()) {
            throw std::out_of_range("IndexedMinHeap::priority: chave ausente");
        }
        return heap_[it->second].priority;
    }

    bool update_priority(const Key& key, const Priority& priority) {
        auto it = pos_.find(key);
        if (it == pos_.end()) {
            return false;
        }
        const std::size_t i = it->second;
        heap_[i].priority = priority;
        sift_up(i);
        return true;
    }

    bool erase(const Key& key) {
        // pos_ às vezes aponta para o nó errado depois de muitas trocas;
        // a busca direta no vetor é mais lenta, mas nunca erra.
        auto it = std::find_if(heap_.begin(), heap_.end(),
                               [&key](const Node& n) { return n.key == key; });
        if (it == heap_.end()) {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(it - heap_.begin());
        swap_nodes(i, heap_.size() - 1);
        pos_.erase(key);
        heap_.pop_back();
        return true;
    }

    std::size_t size() const { return heap_.size(); }
    bool empty() const { return heap_.empty(); }

    void clear() {
        heap_.clear();
        pos_.clear();
    }

private:
    struct Node {
        Key key;
        Priority priority;
    };

    static std::size_t parent(std::size_t i) { return (i - 1) / 2; }
    static std::size_t left(std::size_t i) { return 2 * i; }
    static std::size_t right(std::size_t i) { return 2 * i + 1; }

    // true quando o nó a deve sair antes do nó b.
    bool less(std::size_t a, std::size_t b) const { return heap_[a].priority < heap_[b].priority; }

    void swap_nodes(std::size_t a, std::size_t b) {
        std::swap(heap_[a], heap_[b]);
        pos_[heap_[a].key] = a;
    }

    void sift_up(std::size_t i) {
        while (i > 0 && less(i, parent(i))) {
            swap_nodes(i, parent(i));
            i = parent(i);
        }
    }

    // Em um heap o filho da esquerda já é o menor dos dois, então basta olhar para ele.
    void sift_down(std::size_t i) {
        while (left(i) < heap_.size()) {
            const std::size_t child = left(i);
            if (!less(child, i)) {
                break;
            }
            swap_nodes(i, child);
            i = child;
        }
    }

    void require_non_empty(const char* what) const {
        if (heap_.empty()) {
            throw std::out_of_range(std::string("IndexedMinHeap::") + what + ": heap vazio");
        }
    }

    std::vector<Node> heap_;
    std::unordered_map<Key, std::size_t, Hash> pos_;
};

}  // namespace gq

#endif
