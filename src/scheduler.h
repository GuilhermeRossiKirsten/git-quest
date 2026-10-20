// scheduler.h — escalonador de tarefas por prazo (deadline), apoiado em IndexedMinHeap.
//
// Contrato público (os testes dependem dele):
//
//   TaskId schedule(name, deadline)   agenda uma tarefa; ids começam em 1 e crescem
//   bool reschedule(id, deadline)     muda o prazo de uma tarefa pendente (para antes OU
//                                     para depois); false se o id não está pendente
//   bool cancel(id)                   remove uma tarefa pendente; false se não está pendente
//   std::optional<Task> peek() const  a próxima tarefa a vencer, sem removê-la
//   std::vector<Task> run_until(now)  remove e devolve, em ordem, todas as tarefas com
//                                     deadline <= now
//   std::size_t pending() const
//
// Ordem de execução: menor deadline primeiro; prazos iguais saem na ordem em que foram
// definidos (schedule ou o último reschedule), como numa fila.
#ifndef GQ_SCHEDULER_H
#define GQ_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "indexed_min_heap.h"

namespace gq {

using TaskId = std::uint64_t;
using Tick = std::int64_t;

struct Task {
    TaskId id;
    std::string name;
    Tick deadline;
};

class Scheduler {
public:
    TaskId schedule(std::string name, Tick deadline);
    bool reschedule(TaskId id, Tick deadline);
    bool cancel(TaskId id);
    std::optional<Task> peek() const;
    std::vector<Task> run_until(Tick now);
    std::size_t pending() const { return queue_.size(); }

private:
    IndexedMinHeap<TaskId, Tick> queue_;
    std::unordered_map<TaskId, std::string> names_;
    TaskId next_id_ = 1;
};

}  // namespace gq

#endif
