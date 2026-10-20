// batch.h — acumula mensagens, transforma-as e as despacha em lote.
//
// Contrato (os testes dependem dele):
//   - add(...) enfileira no fim; a ordem de chegada é preservada em tudo;
//   - transform(fn) substitui cada mensagem pendente por fn(mensagem);
//   - dispatch(handler) entrega cada mensagem pendente ao handler, em ordem,
//     devolve quantas entregou e deixa o lote vazio;
//   - drain() devolve todas as mensagens pendentes e deixa o lote vazio.
// Nenhuma dessas operações deve copiar payloads: mensagens são caras.
#ifndef GITQUEST_BATCH_H
#define GITQUEST_BATCH_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "buffer.h"
#include "message.h"

class Batch {
public:
    typedef std::function<Message(Message)> Transform;
    typedef std::function<void(Message)> Handler;

    void add(const Message& msg);
    void add(const std::string& topic, const Buffer& payload);

    void transform(const Transform& fn);
    std::size_t dispatch(const Handler& handler);
    std::vector<Message> drain();

    std::size_t size() const { return pending_.size(); }
    bool empty() const { return pending_.empty(); }
    const Message& operator[](std::size_t i) const { return pending_[i]; }

private:
    std::vector<Message> pending_;
};

#endif
