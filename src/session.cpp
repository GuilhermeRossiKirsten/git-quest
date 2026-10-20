#include "session.h"

#include <cstring>
#include <stdexcept>

namespace {

// Nomes de usuário: 1..32 caracteres, apenas [a-z0-9_].
bool is_valid_user(const std::string& user) {
    if (user.empty() || user.size() > Session::kMaxUserLength) {
        return false;
    }
    for (std::string::size_type i = 0; i < user.size(); ++i) {
        const char c = user[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

const std::size_t Session::kMaxCapacity;
const std::size_t Session::kMaxUserLength;

Session::Session(const std::string& user, std::size_t capacity)
    : user_(user), buffer_(new char[capacity]), capacity_(capacity), size_(0) {
    if (capacity_ == 0 || capacity_ > kMaxCapacity) {
        throw std::invalid_argument("capacidade de sessão inválida");
    }
    if (!is_valid_user(user_)) {
        throw std::invalid_argument("nome de usuário inválido: '" + user_ + "'");
    }
    std::memset(buffer_, 0, capacity_);
}

Session::~Session() {
    delete[] buffer_;
}

bool Session::append(const std::string& data) {
    if (data.size() > capacity_ - size_) {
        return false;
    }
    std::memcpy(buffer_ + size_, data.data(), data.size());
    size_ += data.size();
    return true;
}

std::string Session::contents() const {
    return std::string(buffer_, size_);
}

void Session::clear() {
    size_ = 0;
}
