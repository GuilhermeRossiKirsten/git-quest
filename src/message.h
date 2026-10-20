// message.h — uma mensagem do barramento: tópico, cabeçalhos e payload.
//
// Contrato (os testes dependem dele):
//   - Message tem semântica de VALOR: copiar uma Message produz outra
//     independente (inclusive o payload — nada de bytes compartilhados);
//   - mover uma Message não copia o payload; a Message movida fica vazia
//     (topic().empty(), header_count() == 0, payload().empty()) e pode ser
//     destruída ou receber um novo valor;
//   - compose() devolve uma Message pronta sem copiar nem mover a Message
//     construída internamente (o compilador pode construí-la direto no destino).
//
// Mantenha o membro probe_: os testes o usam para contar cópias e movimentos.
#ifndef GITQUEST_MESSAGE_H
#define GITQUEST_MESSAGE_H

#include <cstddef>
#include <map>
#include <string>

#include "buffer.h"
#include "lifecycle_probe.h"

class Message {
public:
    Message();
    Message(const std::string& topic, const Buffer& payload);

    // Mensagem com payload de `size` bytes iguais a `fill` e os cabeçalhos
    // "content-length" (tamanho em decimal) e "content-type"
    // ("application/octet-stream").
    static Message compose(const std::string& topic, std::size_t size, char fill);

    // Virtual para permitir mensagens especializadas no futuro.
    virtual ~Message();

    const std::string& topic() const { return topic_; }

    void set_header(const std::string& key, const std::string& value);
    bool has_header(const std::string& key) const;
    std::string header(const std::string& key) const;  // "" se ausente
    std::size_t header_count() const { return headers_.size(); }

    const Buffer& payload() const { return payload_; }
    Buffer& payload() { return payload_; }

private:
    std::string topic_;
    std::map<std::string, std::string> headers_;
    Buffer payload_;
    LifecycleProbe<Message> probe_;
};

#endif
