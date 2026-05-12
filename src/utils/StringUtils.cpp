#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include <sstream>
#include <cctype>

std::string StringUtils::trim(const std::string& str) {
    return trimLeft(trimRight(str));
}

std::string StringUtils::trimLeft(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(str[start])) {
        ++start;
    }
    return str.substr(start);
}

std::string StringUtils::trimRight(const std::string& str) {
    if (str.empty()) return str;
    size_t end = str.size() - 1;
    while (end > 0 && std::isspace(str[end])) {
        --end;
    }
    return str.substr(0, end + 1);
}

std::vector<std::string> StringUtils::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

std::vector<std::string> StringUtils::split(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        if (end > start) {
            result.push_back(str.substr(start, end - start));
        }
        start = end + delimiter.size();
        end = str.find(delimiter, start);
    }
    if (start < str.size()) {
        result.push_back(str.substr(start));
    }
    return result;
}

std::string StringUtils::toLower(const std::string& str) {
    std::string result = str;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = std::tolower(result[i]);
    }
    return result;
}

std::string StringUtils::toUpper(const std::string& str) {
    std::string result = str;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = std::toupper(result[i]);
    }
    return result;
}

std::string StringUtils::intToString(int value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string StringUtils::sizeToString(size_t value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string StringUtils::decodeUrl(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hex = 0;
            for (int j = 1; j <= 2; ++j) {
                char c = str[i + j];
                if (c >= '0' && c <= '9') hex = hex * 16 + (c - '0');
                else if (c >= 'A' && c <= 'F') hex = hex * 16 + (c - 'A' + 10);
                else if (c >= 'a' && c <= 'f') hex = hex * 16 + (c - 'a' + 10);
            }
            result += static_cast<char>(hex);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string StringUtils::encodeUrl(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (std::isalnum(str[i]) || str[i] == '-' || str[i] == '_' || str[i] == '.' || str[i] == '~') {
            result += str[i];
        } else {
            std::ostringstream oss;
            oss << '%' << std::uppercase << std::hex << static_cast<unsigned int>(static_cast<unsigned char>(str[i]));
            result += oss.str();
        }
    }
    return result;
}

bool StringUtils::startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StringUtils::caseInsensitiveEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string StringUtils::getExtension(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return "";
}

std::string StringUtils::getMimeType(const std::string& extension) {
    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "json") return "application/json";
    if (extension == "xml") return "application/xml";
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "ico") return "image/x-icon";
    if (extension == "woff") return "font/woff";
    if (extension == "woff2") return "font/woff2";
    if (extension == "ttf") return "font/ttf";
    if (extension == "eot") return "application/vnd.ms-fontobject";
    if (extension == "pdf") return "application/pdf";
    if (extension == "zip") return "application/zip";
    if (extension == "gz" || extension == "gzip") return "application/gzip";
    if (extension == "tar") return "application/x-tar";
    if (extension == "mp3") return "audio/mpeg";
    if (extension == "mp4") return "video/mp4";
    if (extension == "webm") return "video/webm";
    if (extension == "webp") return "image/webp";
    if (extension == "txt" || extension == "md") return "text/plain";
    return "application/octet-stream";
}

std::string StringUtils::htmlEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string StringUtils::stripQueryString(const std::string& uri) {
    size_t qpos = uri.find('?');
    if (qpos != std::string::npos) {
        return uri.substr(0, qpos);
    }
    return uri;
}

std::string StringUtils::resolvePath(const std::string& uri, const std::string& routePath, const std::string& root) {
    std::string path = stripQueryString(uri);

    if (routePath != "/" && startsWith(path, routePath)) {
        if (path.size() > routePath.size()) {
            path = path.substr(routePath.size());
        }
    }

    if (path.empty() || path[0] != '/') {
        path = "/" + path;
    }

    std::string fullPath = FileUtils::joinPath(root, path);
    std::string normalizedPath = FileUtils::normalizePath(fullPath);

    if (!startsWith(normalizedPath, root)) {
        return "";
    }

    return normalizedPath;
}
