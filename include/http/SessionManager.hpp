#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include "Webserv.hpp"
#include <map>
#include <string>

struct Session {
    std::string id;
    std::map<std::string, std::string> data;
    time_t createdAt;
    time_t lastAccessed;
};

class SessionManager {
public:
    static SessionManager& getInstance();

    // Session lifecycle
    std::string createSession();
    Session* getSession(const std::string& id);
    void destroySession(const std::string& id);

    // Session data access
    void setSessionData(const std::string& id, const std::string& key, const std::string& value);
    std::string getSessionData(const std::string& id, const std::string& key, const std::string& defaultValue = "");

    // Session maintenance
    void cleanExpired();
    bool hasSession(const std::string& id);

private:
    SessionManager();
    ~SessionManager();
    SessionManager(const SessionManager& other);
    SessionManager& operator=(const SessionManager& other);

    std::string _generateSessionId();
    bool _isExpired(const Session& session) const;

    std::map<std::string, Session> _sessions;
    static const time_t SESSION_TIMEOUT = 1800; // 30 minutes
};

#endif