#include "http/PostHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include "utils/StringUtils.hpp"
#include <cstring>
#include <cctype>

static std::string extractBoundary(const std::string& contentType) {
    size_t pos = contentType.find("boundary=");
    if (pos == std::string::npos) return "";

    pos += 9; // length of "boundary="
    size_t end = pos;

    // Check if the boundary value is quoted
    if (end < contentType.size() && contentType[end] == '"') {
        // Quoted boundary: extract between the quotes
        size_t quoteEnd = contentType.find('"', end + 1);
        if (quoteEnd == std::string::npos) return "";
        return contentType.substr(end + 1, quoteEnd - end - 1);
    }

    // Unquoted boundary: read until ';' or end of string
    while (end < contentType.size() && contentType[end] != ';' && contentType[end] != ' ') {
        ++end;
    }
    return contentType.substr(pos, end - pos);
}

static bool parseMultipartPart(const std::string& body,
                               const std::string& boundary,
                               std::string& filename,
                               std::string& fileContent) {
    std::string delimiter = "--" + boundary;

    size_t pos = 0;
    while (pos < body.size()) {
        size_t delimPos = body.find(delimiter, pos);
        if (delimPos == std::string::npos) break;

        size_t contentStart = delimPos + delimiter.size();
        if (contentStart >= body.size()) break;

        // Check for closing delimiter (--)
        if (contentStart + 1 < body.size() &&
            body[contentStart] == '-' && body[contentStart + 1] == '-') {
            break;
        }

        // Skip CRLF or LF after boundary delimiter
        if (contentStart < body.size() && body[contentStart] == '\r') {
            if (contentStart + 1 < body.size() && body[contentStart + 1] == '\n') {
                contentStart += 2;
            } else {
                break;
            }
        } else if (contentStart < body.size() && body[contentStart] == '\n') {
            contentStart += 1;
        }

        size_t nextDelim = body.find(delimiter, contentStart);
        if (nextDelim == std::string::npos) break;

        std::string part = body.substr(contentStart, nextDelim - contentStart);

        // Find the blank line separating headers from content
        // Try CRLF CRLF first, then LF LF
        size_t headerEnd = part.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            headerEnd = part.find("\n\n");
            if (headerEnd == std::string::npos) {
                pos = nextDelim;
                continue;
            }
            // With LF-only: headers end at \n\n, content starts after \n\n
            std::string headers = part.substr(0, headerEnd);
            std::string content = part.substr(headerEnd + 2);

            // Strip trailing CRLF or LF before the next boundary
            if (content.size() >= 2 && content[content.size() - 2] == '\r' &&
                content[content.size() - 1] == '\n') {
                content = content.substr(0, content.size() - 2);
            } else if (content.size() >= 1 && content[content.size() - 1] == '\n') {
                content = content.substr(0, content.size() - 1);
            }

            size_t namePos = headers.find("filename=\"");
            if (namePos != std::string::npos) {
                size_t valueStart = namePos + 10;
                size_t nameEnd = headers.find("\"", valueStart);
                if (nameEnd != std::string::npos && nameEnd > valueStart) {
                    filename = headers.substr(valueStart, nameEnd - valueStart);
                    fileContent = content;
                    return true;
                }
            }

            pos = nextDelim;
            continue;
        }

        // Standard CRLF line endings
        std::string headers = part.substr(0, headerEnd);
        std::string content = part.substr(headerEnd + 4);

        // Strip trailing CRLF before the next boundary delimiter
        if (content.size() >= 2 && content[content.size() - 2] == '\r' &&
            content[content.size() - 1] == '\n') {
            content = content.substr(0, content.size() - 2);
        }

        size_t namePos = headers.find("filename=\"");
        if (namePos != std::string::npos) {
            size_t valueStart = namePos + 10;
            size_t nameEnd = headers.find("\"", valueStart);
            if (nameEnd != std::string::npos && nameEnd > valueStart) {
                filename = headers.substr(valueStart, nameEnd - valueStart);
                fileContent = content;
                return true;
            }
        }

        pos = nextDelim;
    }
    return false;
}

PostHandler::PostHandler() {}

PostHandler::~PostHandler() {}

void PostHandler::handle(const Request& request, Response& response,
                         const RouteConfig& route, const ServerConfig& server) {
    if (!route.isUploadEnabled()) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    std::string uploadDir = route.getUploadStore();
    if (uploadDir.empty()) {
        uploadDir = route.getRoot();
    }

    if (!FileUtils::isDirectory(uploadDir)) {
        FileUtils::createDirectory(uploadDir);
    }

    std::string contentType = request.getHeader("Content-Type");
    std::string body = request.getBody();

    std::string filename;
    std::string fileContent;

    if (StringUtils::startsWith(StringUtils::toLower(contentType), "multipart/form-data")) {
        std::string boundary = extractBoundary(contentType);
        if (!boundary.empty()) {
            if (parseMultipartPart(body, boundary, filename, fileContent)) {
                body = fileContent;
                if (filename.empty()) {
                    filename = "upload_" + StringUtils::intToString(time(NULL)) + ".dat";
                }
            } else {
                LOG_WARN("multipart/form-data parsing failed for Content-Type: " + contentType);
            }
        } else {
            LOG_WARN("multipart/form-data without boundary in Content-Type: " + contentType);
        }
    }

    if (filename.empty()) {
        filename = "upload_" + StringUtils::intToString(time(NULL)) + ".txt";
    }

    std::string filepath = FileUtils::joinPath(uploadDir, filename);

    if (FileUtils::writeFile(filepath, body)) {
        response.setStatus(201);
        response.setHeader("Content-Type", "text/plain");
        response.setBody("File uploaded successfully: " + filename);
    } else {
        response.buildError(500, server.getErrorPages(), route.getRoot());
    }
    response.setReady(true);
}