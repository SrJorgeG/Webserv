#include "utils/FileUtils.hpp"
#include "utils/Logger.hpp"

bool FileUtils::fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool FileUtils::isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool FileUtils::isReadable(const std::string& path) {
    return access(path.c_str(), R_OK) == 0;
}

bool FileUtils::isWritable(const std::string& path) {
    return access(path.c_str(), W_OK) == 0;
}

std::string FileUtils::readFile(const std::string& path) {
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

bool FileUtils::writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path.c_str());
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

bool FileUtils::appendFile(const std::string& path, const std::string& content) {
    std::ofstream file(path.c_str(), std::ios::app);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

bool FileUtils::deleteFile(const std::string& path) {
    return remove(path.c_str()) == 0;
}

std::vector<std::string> FileUtils::listDirectory(const std::string& path) {
    std::vector<std::string> result;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        throw std::runtime_error("Cannot open directory: " + path);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name != "." && name != "..") {
            result.push_back(name);
        }
    }
    closedir(dir);
    return result;
}

std::string FileUtils::getFileExtension(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos != std::string::npos) {
        return path.substr(pos);
    }
    return "";
}

size_t FileUtils::getFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_size;
}

bool FileUtils::createDirectory(const std::string& path) {
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string FileUtils::joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a[a.size() - 1] == '/') {
        return a + b;
    }
    return a + "/" + b;
}

std::string FileUtils::normalizePath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;

    while (std::getline(iss, part, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else {
            parts.push_back(part);
        }
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        result += "/" + parts[i];
    }

    if (result.empty()) {
        result = "/";
    }

    return result;
}

std::string FileUtils::getParentDirectory(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}
