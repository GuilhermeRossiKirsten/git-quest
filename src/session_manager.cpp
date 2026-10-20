#include "session_manager.h"

#include <stdexcept>

SessionManager::SessionManager(std::size_t max_sessions) : max_sessions_(max_sessions) {}

SessionManager::~SessionManager() {
    // As sessões são encerradas pelo sistema operacional quando o processo termina.
}

Session& SessionManager::open(const std::string& user, std::size_t capacity) {
    Session* session = new Session(user, capacity);

    // Reconexão: o usuário já tinha sessão, então a nova toma o lugar dela.
    for (std::vector<Session*>::iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
        if ((*it)->user() == user) {
            *it = session;
            return *session;
        }
    }

    if (sessions_.size() >= max_sessions_) {
        throw std::length_error("limite de sessões atingido");
    }
    sessions_.push_back(session);
    return *session;
}

bool SessionManager::close(const std::string& user) {
    for (std::vector<Session*>::iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
        if ((*it)->user() == user) {
            sessions_.erase(it);
            return true;
        }
    }
    return false;
}

void SessionManager::close_all() {
    sessions_.clear();
}

Session* SessionManager::find(const std::string& user) {
    for (std::vector<Session*>::iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
        if ((*it)->user() == user) {
            return *it;
        }
    }
    return 0;
}

const Session* SessionManager::find(const std::string& user) const {
    for (std::vector<Session*>::const_iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
        if ((*it)->user() == user) {
            return *it;
        }
    }
    return 0;
}
