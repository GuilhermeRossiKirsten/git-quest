// Testes de Session: comportamento, exceções no construtor, cópia e movimento.
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "check.h"
#include "../src/session.h"

namespace {

typedef std::integral_constant<bool, std::is_copy_constructible<Session>::value> SessionCopyable;
typedef std::integral_constant<bool, std::is_copy_assignable<Session>::value> SessionCopyAssignable;
typedef std::integral_constant<bool, std::is_move_constructible<Session>::value> SessionMovable;
typedef std::integral_constant<bool, std::is_move_assignable<Session>::value> SessionMoveAssignable;

std::string user_for(int i) {
    std::string name = "user_";
    name += static_cast<char>('a' + i % 26);
    name += static_cast<char>('a' + i / 26);
    return name;
}

// --- cópia: se Session for copiável, a cópia tem de ser independente ---------

template <typename T>
void check_copy_construct(std::true_type) {
    T original("ana", 8);
    original.append("abc");
    {
        T copy(original);
        CHECK(copy.user() == "ana" && copy.contents() == "abc",
              "a cópia deveria ter o mesmo usuário e conteúdo do original");
        CHECK(Session::live_count() == 2, "esperava 2 Sessions vivas, há " << Session::live_count());
        copy.clear();
        copy.append("XYZ");
        CHECK(original.contents() == "abc",
              "alterar a cópia mudou o original para '" << original.contents()
                  << "': as duas Sessions compartilham o mesmo buffer");
    }
    CHECK(original.contents() == "abc", "o original foi afetado pela destruição da cópia");
}

template <typename T>
void check_copy_construct(std::false_type) {}  // cópia proibida: escolha válida

template <typename T>
void check_copy_assign(std::true_type) {
    T a("ana", 8);
    a.append("abc");
    T b("bia", 4);
    b.append("zz");
    b = a;
    CHECK(b.user() == "ana" && b.contents() == "abc" && b.capacity() == 8,
          "depois de b = a, b deveria ser igual a a");
    b.clear();
    b.append("Q");
    CHECK(a.contents() == "abc", "alterar b mudou a para '" << a.contents()
                                    << "': as duas Sessions compartilham o mesmo buffer");
    T& alias = a;
    a = alias;  // autoatribuição
    CHECK(a.contents() == "abc", "autoatribuição (a = a) corrompeu o conteúdo");
}

template <typename T>
void check_copy_assign(std::false_type) {}

// --- movimento: Session precisa ser movível ----------------------------------

template <typename T>
void check_move_construct(std::true_type) {
    T a("ana", 8);
    a.append("hello");
    T b(std::move(a));
    CHECK(b.user() == "ana" && b.contents() == "hello" && b.capacity() == 8,
          "a Session construída por movimento deveria ter o conteúdo da original");
}

template <typename T>
void check_move_construct(std::false_type) {
    CHECK(false, "Session não pode ser movida (std::is_move_constructible<Session> é false)");
}

template <typename T>
void check_move_assign(std::true_type) {
    T a("ana", 8);
    a.append("hello");
    T b("bia", 4);
    b.append("zz");
    b = std::move(a);
    CHECK(b.user() == "ana" && b.contents() == "hello",
          "depois de b = std::move(a), b deveria ter o conteúdo de a");
    T c("caio", 2);
    a = std::move(c);  // um objeto já movido pode receber um novo valor
    CHECK(a.user() == "caio", "atribuir a um objeto já movido deveria funcionar");
}

template <typename T>
void check_move_assign(std::false_type) {
    CHECK(false, "Session não aceita atribuição por movimento (std::is_move_assignable<Session> é false)");
}

template <typename T>
void check_vector(std::true_type) {
    std::vector<T> sessions;
    for (int i = 0; i < 40; ++i) {  // força várias realocações do vector
        sessions.push_back(T(user_for(i), 16));
        sessions.back().append("msg-" + user_for(i));
    }
    CHECK(Session::live_count() == 40, "esperava 40 Sessions vivas, há " << Session::live_count());
    for (int i = 0; i < 40; ++i) {
        CHECK(sessions[i].contents() == "msg-" + user_for(i),
              "a Session " << i << " perdeu o conteúdo depois de o vector realocar");
    }
    sessions.erase(sessions.begin() + 10, sessions.begin() + 20);
    CHECK(sessions[10].contents() == "msg-" + user_for(20), "erase deslocou as Sessions incorretamente");
}

template <typename T>
void check_vector(std::false_type) {
    CHECK(false, "Session não pode ser guardada em std::vector (não é movível nem copiável)");
}

}  // namespace

TEST(session_basico, "Session guarda, limita e limpa mensagens") {
    Session s("ana_01", 8);
    CHECK(s.user() == "ana_01", "user() devolveu '" << s.user() << "'");
    CHECK(s.capacity() == 8 && s.size() == 0 && s.contents().empty(), "sessão nova deveria estar vazia");
    CHECK(s.append("abc") && s.append("defgh"), "append dentro da capacidade deveria funcionar");
    CHECK(s.contents() == "abcdefgh" && s.size() == 8, "conteúdo inesperado: '" << s.contents() << "'");
    CHECK(!s.append("x"), "append além da capacidade deveria devolver false");
    CHECK(s.contents() == "abcdefgh", "append recusado não pode alterar o conteúdo");
    CHECK(s.append(""), "append de string vazia cabe sempre");
    s.clear();
    CHECK(s.size() == 0 && s.contents().empty(), "clear() deveria esvaziar a sessão");
    CHECK(Session::live_count() == 1, "esperava 1 Session viva, há " << Session::live_count());
}

TEST(session_limites_validos, "Session aceita os limites válidos de usuário e capacidade") {
    Session tiny("a", 1);
    CHECK(tiny.append("z") && !tiny.append("z"), "capacidade 1 deveria aceitar exatamente 1 byte");
    Session big(std::string(32, 'x'), 65536);
    CHECK(big.append(std::string(65536, 'k')), "capacidade máxima (65536) deveria ser aceita");
    CHECK(big.contents().size() == 65536, "conteúdo da sessão grande foi truncado");
}

TEST(session_usuario_invalido, "construtor que lança (usuário inválido) não vaza memória") {
    const char* invalid[] = {"", "Ana", "ana maria", "ana-maria", "jos\xc3\xa9"};
    for (std::size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        CHECK_THROWS(Session(invalid[i], 64), std::invalid_argument,
                     "usuário '" << invalid[i] << "' deveria ser rejeitado");
    }
    CHECK_THROWS(Session(std::string(33, 'x'), 64), std::invalid_argument,
                 "usuário com 33 caracteres deveria ser rejeitado");
    CHECK(Session::live_count() == 0, "nenhuma Session deveria existir, há " << Session::live_count());
}

TEST(session_capacidade_invalida, "construtor que lança (capacidade inválida) não vaza memória") {
    CHECK_THROWS(Session("ana", 0), std::invalid_argument, "capacidade 0 deveria ser rejeitada");
    CHECK_THROWS(Session("ana", 65537), std::invalid_argument, "capacidade 65537 deveria ser rejeitada");
    CHECK(Session::live_count() == 0, "nenhuma Session deveria existir, há " << Session::live_count());
}

TEST(session_copia, "copiar uma Session é seguro (cópia profunda) ou proibido (= delete)") {
    check_copy_construct<Session>(SessionCopyable());
    check_copy_assign<Session>(SessionCopyAssignable());
}

TEST(session_move, "Session pode ser movida (construção e atribuição) sem double free") {
    check_move_construct<Session>(SessionMovable());
    check_move_assign<Session>(SessionMoveAssignable());
}

TEST(session_em_vector, "std::vector<Session> sobrevive a realocações sem corromper memória") {
    check_vector<Session>(std::integral_constant<bool, SessionMovable::value || SessionCopyable::value>());
}
