#include "message.h"

#include <utility>

Message::Message() {}

Message::Message(const std::string& topic, const Buffer& payload)
    : topic_(topic), payload_(payload) {}

Message Message::compose(const std::string& topic, std::size_t size, char fill) {
    Message msg(topic, Buffer(size, fill));
    msg.set_header("content-length", std::to_string(size));
    msg.set_header("content-type", "application/octet-stream");
    return std::move(msg);  // garante que a mensagem seja movida, não copiada
}

Message::~Message() {}

void Message::set_header(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

bool Message::has_header(const std::string& key) const {
    return headers_.find(key) != headers_.end();
}

std::string Message::header(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = headers_.find(key);
    return it == headers_.end() ? std::string() : it->second;
}
