#include "buffer.h"

#include <cstring>

Buffer::Buffer() : data_(0), size_(0) {}

Buffer::Buffer(std::size_t size, char fill) : data_(0), size_(size) {
    if (size_ > 0) {
        data_ = new char[size_];
        std::memset(data_, fill, size_);
    }
}

Buffer::Buffer(const Buffer& other) : data_(0), size_(other.size_) {
    if (size_ > 0) {
        data_ = new char[size_];
        std::memcpy(data_, other.data_, size_);
    }
}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this != &other) {
        Buffer tmp(other);
        char* old = data_;
        data_ = tmp.data_;
        size_ = tmp.size_;
        tmp.data_ = old;  // tmp libera o bloco antigo ao sair de escopo
    }
    return *this;
}

// Movimento: rouba o ponteiro em vez de copiar os bytes.
Buffer::Buffer(Buffer&& other) : data_(other.data_), size_(other.size_) {
    other.data_ = 0;
    other.size_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) {
    delete[] data_;
    data_ = other.data_;
    size_ = other.size_;
    return *this;
}

Buffer::~Buffer() { delete[] data_; }
