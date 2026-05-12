#include "http/SessionManager.hpp"
#include "utils/Logger.hpp"
#include <sstream>
#include <iomanip>

SessionManager::SessionManager() {}

SessionManager::~SessionManager() {}

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;
}

std::string SessionManager::_generateSessionId() {
    std::string result;
    result.reserve(16);

    unsigned int seed = static_cast<unsigned int>(time(NULL));
    seed += static_cast<unsigned int>(reinterpret_cast<uintptr_t>(this));

    for (int i = 0; i < 16; ++i) {
        int c = (rand() % 16);
        if (c < 10) {
            result += static_cast<char>('0' + c);
        } else {
            result += static_cast<char>('a' + (c - 10));
        }
    }

    return result;
}

bool SessionManager::_isExpired(const Session& session) const {
    return (time(NULL) - session.lastAccessed) > SESSION_TIMEOUT;
}

std::string SessionManager::createSession() {
    std::string id = _generateSessionId();

    Session session;
    session.id = id;
    session.createdAt = time(NULL);
    session.lastAccessed = time(NULL);

    _sessions[id] = session;

    LOG_INFO("Created new session: " + id);
    return id;
}

Session* SessionManager::getSession(const std::string& id) {
    std::map<std::string, Session>::iterator it = _sessions.find(id);
    if (it == _sessions.end()) {
        return NULL;
    }

    Session& session = it->second;
    if (_isExpired(session)) {
        _sessions.erase(it);
        LOG_INFO("Session expired: " + id);
        return NULL;
    }

    session.lastAccessed = time(NULL);
    return &session;
}

void SessionManager::destroySession(const std::string& id) {
    std::map<std::string, Session>::iterator it = _sessions.find(id);
    if (it != _sessions.end()) {
        _sessions.erase(it);
        LOG_INFO("Destroyed session: " + id);
    }
}

void SessionManager::setSessionData(const std::string& id, const std::string& key, const std::string& value) {
    Session* session = getSession(id);
    if (session) {
        session->data[key] = value;
    }
}

std::string SessionManager::getSessionData(const std::string& id, const std::string& key, const std::string& defaultValue) {
    Session* session = getSession(id);
    if (!session) {
        return defaultValue;
    }

    std::map<std::string, std::string>::iterator it = session->data.find(key);
    if (it == session->data.end()) {
        return defaultValue;
    }

    return it->second;
}

void SessionManager::cleanExpired() {
    std::map<std::string, Session>::iterator it = _sessions.begin();
    while (it != _sessions.end()) {
        if (_isExpired(it->second)) {
            LOG_INFO("Cleaning expired session: " + it->first);
            _sessions.erase(it++);
        } else {
            ++it;
        }
    }
}

bool SessionManager::hasSession(const std::string& id) {
    return getSession(id) != NULL;
}