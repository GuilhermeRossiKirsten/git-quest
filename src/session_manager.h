// session_manager.h — mantém as sessões abertas, no máximo uma por usuário.
//
// Contrato público (usado pelos testes — mantenha assinaturas e comportamento):
//   SessionManager(max_sessions)
//   open(user, capacity)  abre uma sessão e devolve uma referência a ela.
//                         Se o usuário já tem sessão, ela é SUBSTITUÍDA por uma
//                         nova (vazia). Lança std::length_error se o gerenciador
//                         já tem max_sessions sessões e o usuário é novo, e
//                         std::invalid_argument para usuário/capacidade inválidos.
//   close(user)           fecha a sessão; false se não existir.
//   close_all(), find(user) (nullptr se não existir), count()
//   Um SessionManager deve poder ser movido (ex.: devolvido por valor).
#ifndef GITQUEST_SESSION_MANAGER_H
#define GITQUEST_SESSION_MANAGER_H

#include <cstddef>
#include <string>
#include <vector>

#include "session.h"

class SessionManager {
public:
    explicit SessionManager(std::size_t max_sessions);
    ~SessionManager();

    Session& open(const std::string& user, std::size_t capacity);
    bool close(const std::string& user);
    void close_all();

    Session* find(const std::string& user);
    const Session* find(const std::string& user) const;

    std::size_t count() const { return sessions_.size(); }
    std::size_t max_sessions() const { return max_sessions_; }

private:
    std::vector<Session*> sessions_;
    std::size_t max_sessions_;
};

#endif
