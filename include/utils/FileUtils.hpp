#ifndef FILE_UTILS_HPP
#define FILE_UTILS_HPP

#include "Webserv.hpp"
#include <vector>

class FileUtils {
public:
    static bool fileExists(const std::string& path);
    static bool isDirectory(const std::string& path);
    static bool isReadable(const std::string& path);
    static bool isWritable(const std::string& path);

    static std::string readFile(const std::string& path);
    static bool writeFile(const std::string& path, const std::string& content);
    static bool appendFile(const std::string& path, const std::string& content);
    static bool deleteFile(const std::string& path);

    static std::vector<std::string> listDirectory(const std::string& path);
    static std::string getFileExtension(const std::string& path);
    static size_t getFileSize(const std::string& path);

    static bool createDirectory(const std::string& path);
    static std::string joinPath(const std::string& a, const std::string& b);
    static std::string normalizePath(const std::string& path);
    static std::string getParentDirectory(const std::string& path);

private:
    FileUtils();
    ~FileUtils();
    FileUtils(const FileUtils& other);
    FileUtils& operator=(const FileUtils& other);
};

#endif
