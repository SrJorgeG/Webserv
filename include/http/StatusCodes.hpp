#ifndef STATUS_CODES_HPP
#define STATUS_CODES_HPP

#include "Webserv.hpp"
#include <map>

class StatusCodes {
public:
    static std::string getMessage(int code);

private:
    StatusCodes();
    ~StatusCodes();
    StatusCodes(const StatusCodes& other);
    StatusCodes& operator=(const StatusCodes& other);

    static std::map<int, std::string> _codes;
    static bool _initialized;
    static void _initCodes();
};

#endif
