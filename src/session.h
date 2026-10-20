// session.h — uma sessão de usuário com um buffer de mensagens de tamanho fixo.
//
// Contrato público (usado pelos testes — mantenha assinaturas e comportamento):
//   Session(user, capacity)  lança std::invalid_argument se o usuário ou a
//                            capacidade forem inválidos (veja session.cpp)
//   user(), capacity(), size(), contents(), append(), clear(), live_count()
#ifndef GITQUEST_SESSION_H
#define GITQUEST_SESSION_H

#include <cstddef>
#include <string>

#include "instance_counter.h"

class Session {
public:
    static const std::size_t kMaxCapacity = 65536;
    static const std::size_t kMaxUserLength = 32;

    Session(const std::string& user, std::size_t capacity);
    ~Session();

    const std::string& user() const { return user_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return size_; }

    // Acrescenta `data` ao buffer. Retorna false (sem alterar nada) se não couber.
    bool append(const std::string& data);
    std::string contents() const;
    void clear();

    // Quantas Sessions existem neste momento (usado pelos testes).
    static long live_count() { return InstanceCounter<Session>::live(); }

private:
    std::string user_;
    char* buffer_;
    std::size_t capacity_;
    std::size_t size_;
    InstanceCounter<Session> counter_;
};

#endif
