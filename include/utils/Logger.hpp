#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "Webserv.hpp"
#include <fstream>

class Logger {
public:
    static Logger& getInstance();

    void setLevel(LogLevel level);
    void setOutput(const std::string& filename);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    Logger();
    ~Logger();
    Logger(const Logger& other);
    Logger& operator=(const Logger& other);

    LogLevel _level;
    std::ofstream _file;
    bool _useFile;

    void _log(LogLevel level, const std::string& message);
    std::string _levelToString(LogLevel level) const;
    std::string _getTimestamp() const;
};

#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARN(msg) Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)

#endif
