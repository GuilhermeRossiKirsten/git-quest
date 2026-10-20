// buffer.h — bloco de bytes com posse exclusiva (o payload de uma Message).
//
// Contrato (os testes dependem dele):
//   - Buffer()                      vazio: size() == 0, empty() == true
//   - Buffer(n, fill)               n bytes, todos iguais a fill
//   - cópia (construção/atribuição) é PROFUNDA: a cópia não compartilha bytes
//     com a origem; alterar uma não altera a outra
//   - movimento (construção/atribuição) não copia nem aloca bytes; a origem
//     fica VAZIA (empty() == true) e pode ser destruída ou receber novo valor
//   - autoatribuição (a = a, a = std::move(a)) deixa o objeto válido
#ifndef GITQUEST_BUFFER_H
#define GITQUEST_BUFFER_H

#include <cstddef>

class Buffer {
public:
    Buffer();
    explicit Buffer(std::size_t size, char fill = '\0');

    Buffer(const Buffer& other);
    Buffer& operator=(const Buffer& other);
    Buffer(Buffer&& other);
    Buffer& operator=(Buffer&& other);
    ~Buffer();

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const char* data() const { return data_; }
    char* data() { return data_; }
    char& operator[](std::size_t i) { return data_[i]; }
    char operator[](std::size_t i) const { return data_[i]; }

private:
    char* data_;
    std::size_t size_;
};

#endif
