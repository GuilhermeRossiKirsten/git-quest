// Testes de ponta a ponta do Scheduler (que usa IndexedMinHeap por dentro).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "support/rng.h"
#include "../src/scheduler.h"

namespace {

std::string names_of(const std::vector<gq::Task>& tasks) {
    std::string out;
    for (const gq::Task& t : tasks) {
        out += (out.empty() ? "" : ", ") + t.name + "@" + std::to_string(t.deadline);
    }
    return "[" + out + "]";
}

void expect_run(gq::Scheduler& s, gq::Tick now, const std::vector<std::string>& want) {
    const std::vector<gq::Task> got = s.run_until(now);
    std::vector<std::string> names;
    for (const gq::Task& t : got) {
        names.push_back(t.name);
        CHECK(t.deadline <= now, "run_until(" << now << ") devolveu " << t.name << " com deadline " << t.deadline);
    }
    std::string expected;
    for (const std::string& n : want) {
        expected += (expected.empty() ? "" : ", ") + n;
    }
    CHECK(names == want, "run_until(" << now << ") devolveu " << names_of(got) << "; esperado [" << expected << "]");
}

}  // namespace

TEST(scheduler_basico, "Scheduler executa tarefas por prazo, com empates em ordem de agendamento") {
    gq::Scheduler s;
    CHECK(s.pending() == 0 && !s.peek().has_value(), "escalonador novo deveria estar vazio");
    const gq::TaskId backup = s.schedule("backup", 50);
    const gq::TaskId email = s.schedule("email", 10);
    const gq::TaskId report = s.schedule("relatorio", 10);
    s.schedule("limpeza", 30);
    CHECK(backup == 1 && email == 2 && report == 3, "os ids deveriam ser 1, 2, 3...");
    CHECK(s.pending() == 4, "pending() deveria ser 4");
    const std::optional<gq::Task> next = s.peek();
    CHECK(next && next->name == "email" && next->deadline == 10, "peek() deveria mostrar 'email' (prazo 10)");
    expect_run(s, 5, {});
    expect_run(s, 10, {"email", "relatorio"});
    expect_run(s, 100, {"limpeza", "backup"});
    CHECK(s.pending() == 0, "todas as tarefas deveriam ter sido executadas");
}

TEST(scheduler_reagendar, "reagendar antecipa ou adia uma tarefa pendente") {
    gq::Scheduler s;
    const gq::TaskId a = s.schedule("a", 10);
    const gq::TaskId b = s.schedule("b", 20);
    const gq::TaskId c = s.schedule("c", 30);
    s.schedule("d", 40);
    CHECK(s.reschedule(a, 35), "adiar 'a' deveria devolver true");
    CHECK(s.peek() && s.peek()->name == "b", "depois de adiar 'a' para 35, a próxima tarefa é 'b'");
    CHECK(s.reschedule(c, 5), "antecipar 'c' deveria devolver true");
    CHECK(s.peek() && s.peek()->name == "c", "depois de antecipar 'c' para 5, a próxima tarefa é 'c'");
    CHECK(s.reschedule(b, 30), "adiar 'b' deveria devolver true");
    expect_run(s, 30, {"c", "b"});
    CHECK(s.reschedule(a, 40), "adiar 'a' de novo deveria devolver true");  // empata com 'd', mas depois dele
    expect_run(s, 40, {"d", "a"});
    CHECK(!s.reschedule(a, 1), "reagendar uma tarefa já executada deveria devolver false");
    CHECK(!s.reschedule(999, 1), "reagendar um id inexistente deveria devolver false");
}

TEST(scheduler_cancelar, "cancelar remove a tarefa de qualquer posição da fila") {
    gq::Scheduler s;
    std::vector<gq::TaskId> ids;
    for (int i = 0; i < 15; ++i) {
        ids.push_back(s.schedule("t" + std::to_string(i), 100 + i));
    }
    CHECK(s.cancel(ids[3]) && s.cancel(ids[0]) && s.cancel(ids[11]), "cancelar tarefas pendentes deveria devolver true");
    CHECK(!s.cancel(ids[3]), "cancelar duas vezes deveria devolver false");
    CHECK(s.pending() == 12, "pending() deveria ser 12 depois de 3 cancelamentos");
    expect_run(s, 114, {"t1", "t2", "t4", "t5", "t6", "t7", "t8", "t9", "t10", "t12", "t13", "t14"});
    CHECK(!s.cancel(ids[14]), "cancelar uma tarefa já executada deveria devolver false");
}

TEST(scheduler_ponta_a_ponta, "20 mil eventos (agendar, reagendar, cancelar, executar) contra um modelo") {
    gq::Scheduler s;
    // Modelo: (deadline, ordem de definição) -> id, e id -> chave no modelo.
    std::map<std::tuple<gq::Tick, std::uint64_t>, gq::TaskId> model;
    std::map<gq::TaskId, std::tuple<gq::Tick, std::uint64_t>> where;
    std::uint64_t seq = 0;
    gq::Tick now = 0;
    gq::testing::Rng rng(15020);
    std::vector<gq::TaskId> issued;

    for (int event = 0; event < 20000; ++event) {
        const std::uint64_t kind = rng.below(100);
        const std::string ctx = "evento nº " + std::to_string(event + 1);
        if (kind < 40 || issued.empty()) {
            const gq::Tick deadline = now + static_cast<gq::Tick>(rng.below(200));
            const gq::TaskId id = s.schedule("job-" + std::to_string(issued.size() + 1), deadline);
            CHECK(id == issued.size() + 1, ctx << ": schedule devolveu o id " << id);
            issued.push_back(id);
            model[{deadline, seq}] = id;
            where[id] = {deadline, seq++};
        } else if (kind < 65) {
            const gq::TaskId id = issued[rng.below(issued.size())];
            const gq::Tick deadline = now + static_cast<gq::Tick>(rng.below(200));
            const bool pending = where.count(id) != 0;
            CHECK(s.reschedule(id, deadline) == pending, ctx << ": reschedule(" << id << ") devolveu " << !pending);
            if (pending) {
                model.erase(where[id]);
                model[{deadline, seq}] = id;
                where[id] = {deadline, seq++};
            }
        } else if (kind < 80) {
            const gq::TaskId id = issued[rng.below(issued.size())];
            const bool pending = where.count(id) != 0;
            CHECK(s.cancel(id) == pending, ctx << ": cancel(" << id << ") devolveu " << !pending);
            if (pending) {
                model.erase(where[id]);
                where.erase(id);
            }
        } else {
            now += static_cast<gq::Tick>(rng.below(20));
            const std::vector<gq::Task> got = s.run_until(now);
            std::size_t i = 0;
            while (!model.empty() && std::get<0>(model.begin()->first) <= now) {
                const gq::TaskId want = model.begin()->second;
                CHECK(i < got.size(), ctx << ": run_until(" << now << ") devolveu só " << got.size()
                                          << " tarefas; faltou o id " << want);
                CHECK(got[i].id == want && got[i].deadline == std::get<0>(model.begin()->first) &&
                          got[i].name == "job-" + std::to_string(want),
                      ctx << ": run_until(" << now << ") tarefa nº " << i + 1 << " foi " << got[i].name << "@"
                          << got[i].deadline << ", esperado job-" << want << "@"
                          << std::get<0>(model.begin()->first));
                where.erase(want);
                model.erase(model.begin());
                ++i;
            }
            CHECK(i == got.size(), ctx << ": run_until(" << now << ") devolveu " << got.size() << " tarefas, esperado "
                                       << i);
        }
        CHECK(s.pending() == model.size(), ctx << ": pending() = " << s.pending() << ", esperado " << model.size());
    }
}
