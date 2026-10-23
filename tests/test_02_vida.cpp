// Ciclo de vida dos elementos: cada T construído pela fila é destruído
// exatamente uma vez; nada é construído "de reserva"; exceções no construtor
// de T não corrompem a fila; o destrutor da fila libera o que sobrou.
// (Vazamentos de std::string também são acusados pelo LeakSanitizer no build [asan].)
#include <optional>
#include <stdexcept>
#include <string>

#include "check.h"
#include "support/tracked.h"
#include "../src/spsc_queue.h"

TEST(vida_elementos, "cada elemento é construído e destruído exatamente uma vez (pop, volta do anel e destrutor da fila)") {
    Tracked::reset();
    {
        SpscQueue<Tracked> q(8);
        CHECK(Tracked::live.load() == 0, "uma fila vazia já contém " << Tracked::live.load()
                                                                     << " objetos T vivos (construiu T de reserva?)");
        for (std::uint64_t i = 0; i < 5; ++i) {
            CHECK(q.try_push(Tracked(i)), "try_push falhou com " << i << " itens numa fila de capacidade 8");
        }
        CHECK(Tracked::live.load() == 5, "com 5 itens na fila deveria haver 5 objetos vivos, há "
                                             << Tracked::live.load());
        for (std::uint64_t i = 0; i < 2; ++i) {
            std::optional<Tracked> v = q.try_pop();
            CHECK(v && v->value() == i, "esperava o item " << i);
        }  // os valores devolvidos morrem aqui
        CHECK(Tracked::live.load() == 3,
              "depois de tirar 2 de 5 itens (e descartar os valores) deveria haver 3 objetos vivos, há "
                  << Tracked::live.load() << " — o objeto que fica no slot depois do try_pop() precisa ser destruído");
        // Muitas voltas no anel com 3 itens sempre dentro.
        for (std::uint64_t i = 5; i < 205; ++i) {
            CHECK(q.try_push(Tracked(i)), "try_push falhou com 3 itens numa fila de capacidade 8");
            std::optional<Tracked> v = q.try_pop();
            CHECK(v && v->value() == i - 3, "esperava o item " << i - 3 << " (FIFO)");
        }
        CHECK(Tracked::live.load() == 3, "depois de 200 push/pop deveria haver 3 objetos vivos na fila, há "
                                             << Tracked::live.load());
        CHECK(Tracked::bad_destructions.load() == 0,
              Tracked::bad_destructions.load()
                  << " destruição(ões) de objeto já destruído ou de memória onde nunca se construiu um T");
    }  // a fila morre com 3 itens dentro
    CHECK(Tracked::bad_destructions.load() == 0,
          "o destrutor da fila destruiu " << Tracked::bad_destructions.load()
                                          << " objeto(s) já destruído(s) ou memória nunca construída (destruição dupla)");
    CHECK(Tracked::live.load() == 0, "o destrutor da fila deixou " << Tracked::live.load()
                                                                   << " objeto(s) vivo(s): os itens que ainda estavam "
                                                                      "na fila nunca foram destruídos");
    CHECK(Tracked::constructions.load() == Tracked::destructions.load(),
          Tracked::constructions.load() << " construções e " << Tracked::destructions.load() << " destruições");
}

TEST(vida_fila_cheia_destruida, "fila cheia destruída e fila nunca usada: nenhum objeto vivo, nenhuma destruição indevida") {
    Tracked::reset();
    {
        SpscQueue<Tracked> unused(16);
    }
    CHECK(Tracked::bad_destructions.load() == 0,
          "destruir uma fila vazia destruiu " << Tracked::bad_destructions.load()
                                              << " objeto(s) que nunca foram construídos");
    {
        SpscQueue<Tracked> q(5);
        for (std::uint64_t i = 0; i < 5; ++i) {
            CHECK(q.try_push(Tracked(i)), "try_push falhou com " << i << " itens numa fila de capacidade 5");
        }
    }
    CHECK(Tracked::bad_destructions.load() == 0, "destruição dupla/indevida ao destruir uma fila cheia");
    CHECK(Tracked::live.load() == 0, "destruir uma fila cheia deixou " << Tracked::live.load() << " objeto(s) vivo(s)");
}

TEST(vida_excecao_no_construtor, "se o construtor de T lança em try_push, a exceção propaga e a fila continua intacta") {
    Tracked::reset();
    {
        SpscQueue<Tracked> q(4);
        const Tracked original(7);
        CHECK(q.try_push(original), "try_push(const T&) falhou numa fila vazia");
        Tracked::throw_on_copy = true;
        CHECK_THROWS(q.try_push(original), std::runtime_error,
                     "a exceção do construtor de cópia de T deveria sair de try_push");
        Tracked::throw_on_copy = false;
        CHECK(q.size_approx() == 1, "depois da exceção a fila deveria continuar com 1 item, tem " << q.size_approx());
        std::optional<Tracked> a = q.try_pop();
        CHECK(a && a->value() == 7, "o item anterior à exceção deveria sair intacto");
        CHECK(!q.try_pop().has_value(),
              "a tentativa que lançou exceção deixou um item fantasma (memória nunca construída) na fila");
        CHECK(q.try_push(Tracked(8)), "a fila não aceitou um novo item depois da exceção");
        std::optional<Tracked> b = q.try_pop();
        CHECK(b && b->value() == 8, "esperava o item 8 depois da exceção");
    }
    CHECK(Tracked::bad_destructions.load() == 0, "destruição indevida depois de uma exceção no construtor de T");
    CHECK(Tracked::live.load() == 0, Tracked::live.load() << " objeto(s) vivo(s) depois do fim do teste");
}

TEST(vida_strings_heap, "strings longas que sobram na fila são liberadas pelo destrutor (LeakSanitizer)") {
    const std::string base = "VALE3.SA.CONTRATO-FUTURO-LONGO-";
    {
        SpscQueue<std::string> q(8);
        for (int i = 0; i < 6; ++i) {
            CHECK(q.try_push(base + std::to_string(i)), "try_push falhou com " << i << " itens na fila de capacidade 8");
        }
        std::optional<std::string> first = q.try_pop();
        CHECK(first && *first == base + "0", "a primeira string saiu corrompida");
    }  // 5 strings com memória no heap ainda dentro da fila
    // O LeakSanitizer verifica ao fim do processo se essa memória foi liberada.
}
