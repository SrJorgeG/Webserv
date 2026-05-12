#include "utils/Logger.hpp"

Logger::Logger() : _level(LOG_INFO), _useFile(false) {}

Logger::~Logger() {
    if (_file.is_open()) {
        _file.close();
    }
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) {
    _level = level;
}

void Logger::setOutput(const std::string& filename) {
    if (_file.is_open()) {
        _file.close();
    }
    _file.open(filename.c_str(), std::ios::app);
    _useFile = _file.is_open();
}

void Logger::debug(const std::string& message) {
    _log(LOG_DEBUG, message);
}

void Logger::info(const std::string& message) {
    _log(LOG_INFO, message);
}

void Logger::warn(const std::string& message) {
    _log(LOG_WARN, message);
}

void Logger::error(const std::string& message) {
    _log(LOG_ERROR, message);
}

void Logger::_log(LogLevel level, const std::string& message) {
    if (level < _level) return;

    std::string output = "[" + _getTimestamp() + "] " + _levelToString(level) + ": " + message;

    if (_useFile && _file.is_open()) {
        _file << output << std::endl;
    } else {
        if (level >= LOG_WARN) {
            std::cerr << output << std::endl;
        } else {
            std::cout << output << std::endl;
        }
    }
}

std::string Logger::_levelToString(LogLevel level) const {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_WARN: return "WARN";
        case LOG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::_getTimestamp() const {
    time_t now = time(NULL);
    struct tm* tmPtr = localtime(&now);
    if (tmPtr == NULL) {
        return "[TIME_ERROR]";
    }
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmPtr);
    return std::string(buf);
}
