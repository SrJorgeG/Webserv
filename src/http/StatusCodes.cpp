#include "http/StatusCodes.hpp"

std::map<int, std::string> StatusCodes::_codes;
bool StatusCodes::_initialized = false;

std::string StatusCodes::getMessage(int code) {
    if (!_initialized) {
        _initCodes();
        _initialized = true;
    }
    std::map<int, std::string>::const_iterator it = _codes.find(code);
    if (it != _codes.end()) {
        return it->second;
    }
    return "Unknown Status";
}

void StatusCodes::_initCodes() {
    _codes[100] = "Continue";
    _codes[200] = "OK";
    _codes[201] = "Created";
    _codes[204] = "No Content";
    _codes[301] = "Moved Permanently";
    _codes[302] = "Found";
    _codes[400] = "Bad Request";
    _codes[401] = "Unauthorized";
    _codes[403] = "Forbidden";
    _codes[404] = "Not Found";
    _codes[405] = "Method Not Allowed";
    _codes[408] = "Request Timeout";
    _codes[411] = "Length Required";
    _codes[413] = "Payload Too Large";
    _codes[414] = "URI Too Long";
    _codes[415] = "Unsupported Media Type";
    _codes[500] = "Internal Server Error";
    _codes[501] = "Not Implemented";
    _codes[502] = "Bad Gateway";
    _codes[503] = "Service Unavailable";
    _codes[504] = "Gateway Timeout";
}
