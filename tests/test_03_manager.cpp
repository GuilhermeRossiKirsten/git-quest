// Testes de SessionManager: posse das sessões, exceções, substituição e movimento.
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "check.h"
#include "../src/session_manager.h"

// Macro (e não função) para que a falha aponte a linha do teste.
#define EXPECT_LIVE(expected, when)                  \
    CHECK(Session::live_count() == (expected),       \
          when << ": esperava " << (expected)        \
               << " Session(s) viva(s), há " << Session::live_count())

namespace {

template <typename M>
void check_manager_move(std::true_type) {
    M original(4);
    original.open("ana", 8).append("a1");
    original.open("bia", 8).append("b1");
    M moved(std::move(original));
    CHECK(moved.count() == 2, "o SessionManager movido deveria ter 2 sessões, tem " << moved.count());
    CHECK(moved.find("ana") != 0 && moved.find("ana")->contents() == "a1",
          "a sessão de 'ana' se perdeu no movimento");
    EXPECT_LIVE(2, "depois de mover o SessionManager");
    moved.open("caio", 8);
    EXPECT_LIVE(3, "depois de abrir uma sessão no SessionManager movido");
}

template <typename M>
void check_manager_move(std::false_type) {
    CHECK(false, "SessionManager não pode ser movido (std::is_move_constructible<SessionManager> é false)");
}

}  // namespace

TEST(manager_basico, "SessionManager abre, encontra e libera as sessões ao ser destruído") {
    {
        SessionManager m(8);
        CHECK(m.count() == 0 && m.max_sessions() == 8, "gerenciador novo deveria estar vazio");
        Session& ana = m.open("ana", 16);
        ana.append("oi");
        m.open("bia", 16);
        m.open("caio", 4);
        CHECK(m.count() == 3, "esperava 3 sessões, há " << m.count());
        CHECK(m.find("ana") != 0 && m.find("ana")->contents() == "oi",
              "open() deveria devolver a mesma sessão que find() encontra");
        CHECK(m.find("davi") == 0, "find() de usuário inexistente deveria devolver nullptr");
        const SessionManager& cm = m;
        CHECK(cm.find("caio") != 0 && cm.find("caio")->capacity() == 4, "find() const falhou");
        EXPECT_LIVE(3, "com 3 sessões abertas");
    }
    EXPECT_LIVE(0, "depois que o SessionManager saiu de escopo");
}

TEST(manager_close, "close() e close_all() destroem as sessões imediatamente") {
    SessionManager m(8);
    m.open("ana", 8);
    m.open("bia", 8);
    m.open("caio", 8);
    CHECK(m.close("bia"), "close() de sessão existente deveria devolver true");
    CHECK(!m.close("bia"), "fechar duas vezes deveria devolver false");
    CHECK(!m.close("ninguem"), "close() de usuário inexistente deveria devolver false");
    CHECK(m.count() == 2 && m.find("bia") == 0, "a sessão de 'bia' deveria ter sumido");
    EXPECT_LIVE(2, "depois de close(\"bia\")");
    m.close_all();
    CHECK(m.count() == 0, "close_all() deveria esvaziar o gerenciador");
    EXPECT_LIVE(0, "depois de close_all()");
    m.open("ana", 8);
    CHECK(m.count() == 1, "o gerenciador deveria continuar utilizável depois de close_all()");
}

TEST(manager_reabrir, "reabrir a sessão de um usuário substitui a antiga sem vazar") {
    SessionManager m(2);
    m.open("ana", 8).append("velho");
    m.open("bia", 8);
    Session& fresh = m.open("ana", 16);
    CHECK(m.count() == 2, "reabrir não deveria criar uma segunda sessão para 'ana'");
    CHECK(&fresh == m.find("ana"), "open() deveria devolver a sessão nova");
    CHECK(fresh.capacity() == 16 && fresh.contents().empty(), "a sessão reaberta deveria estar vazia");
    EXPECT_LIVE(2, "depois de reabrir a sessão de 'ana'");
}

TEST(manager_lotado, "open() num gerenciador lotado lança length_error sem vazar") {
    SessionManager m(2);
    m.open("ana", 8);
    m.open("bia", 8);
    CHECK_THROWS(m.open("caio", 8), std::length_error, "o terceiro usuário deveria ser recusado");
    CHECK(m.count() == 2 && m.find("caio") == 0, "a sessão recusada não pode ficar no gerenciador");
    EXPECT_LIVE(2, "depois de open() recusado");
    m.open("ana", 4);  // reabrir um usuário existente continua permitido
    EXPECT_LIVE(2, "depois de reabrir 'ana' com o gerenciador lotado");
    SessionManager none(0);
    CHECK_THROWS(none.open("ana", 8), std::length_error, "um gerenciador com limite 0 recusa tudo");
    EXPECT_LIVE(2, "depois de open() num gerenciador com limite 0");
}

TEST(manager_entrada_invalida, "open() com usuário ou capacidade inválidos não altera nada") {
    SessionManager m(4);
    m.open("ana", 8).append("x");
    CHECK_THROWS(m.open("Ana Maria", 8), std::invalid_argument, "usuário inválido deveria lançar");
    CHECK_THROWS(m.open("bia", 0), std::invalid_argument, "capacidade 0 deveria lançar");
    CHECK_THROWS(m.open("ana", 70000), std::invalid_argument, "capacidade acima do limite deveria lançar");
    CHECK(m.count() == 1 && m.find("ana")->contents() == "x",
          "um open() que falhou não pode mexer na sessão existente");
    EXPECT_LIVE(1, "depois de open() inválidos");
}

TEST(manager_move, "SessionManager pode ser movido sem duplicar nem perder sessões") {
    check_manager_move<SessionManager>(
        std::integral_constant<bool, std::is_move_constructible<SessionManager>::value>());
}

TEST(manager_carga, "1000 operações determinísticas mantêm sessões vivas == sessões abertas") {
    SessionManager m(16);
    unsigned seed = 12345;  // gerador congruente linear: sempre a mesma sequência
    for (int i = 0; i < 1000; ++i) {
        seed = seed * 1103515245u + 12345u;
        const unsigned r = (seed >> 16) & 0x7fff;
        std::string user = "u";
        user += static_cast<char>('a' + r % 20);
        switch (r % 4) {
            case 0:
                m.close(user);
                break;
            case 1:
                try {
                    m.open(user, (r % 3 == 0) ? 0 : 1 + r % 64);
                } catch (const std::invalid_argument&) {
                } catch (const std::length_error&) {
                }
                break;
            default:
                try {
                    m.open(user, 1 + r % 64).append("x");
                } catch (const std::length_error&) {
                }
        }
        CHECK(Session::live_count() == static_cast<long>(m.count()),
              "operação " << i << ": " << Session::live_count() << " Sessions vivas, mas só "
                          << m.count() << " abertas no gerenciador");
    }
    m.close_all();
    EXPECT_LIVE(0, "depois de close_all()");
}
