#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include "Webserv.hpp"
#include <vector>

class StringUtils {
public:
    static std::string trim(const std::string& str);
    static std::string trimLeft(const std::string& str);
    static std::string trimRight(const std::string& str);

    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::vector<std::string> split(const std::string& str, const std::string& delimiter);

    static std::string toLower(const std::string& str);
    static std::string toUpper(const std::string& str);

    static std::string intToString(int value);
    static std::string sizeToString(size_t value);

    static std::string decodeUrl(const std::string& str);
    static std::string encodeUrl(const std::string& str);

    static bool startsWith(const std::string& str, const std::string& prefix);
    static bool endsWith(const std::string& str, const std::string& suffix);

    static std::string getExtension(const std::string& path);
    static std::string getMimeType(const std::string& extension);

    static std::string htmlEscape(const std::string& str);

private:
    StringUtils();
    ~StringUtils();
    StringUtils(const StringUtils& other);
    StringUtils& operator=(const StringUtils& other);
};

#endif
