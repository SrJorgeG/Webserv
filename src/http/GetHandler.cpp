#include "http/GetHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include "http/StatusCodes.hpp"
#include <cstring>

// Portable UTC mktime — converts a UTC struct tm to time_t without
// relying on timegm() (GNU extension) or timezone state.
static time_t utcMktime(struct tm* t)
{
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = t->tm_year + 1900;
    long days = (year - 1970) * 365L;
    for (int y = 1970; y < year; ++y)
    {
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ++days;
    }
    for (int m = 0; m < t->tm_mon; ++m)
    {
        days += mdays[m];
        if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) ++days;
    }
    days += t->tm_mday - 1;
    return days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec;
}

// Parses RFC 1123: "Mon, 15 Apr 2024 12:00:00 GMT" → time_t, or -1 on failure.
static time_t parseHttpDate(const std::string& s)
{
    static const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0, 0, 0, 0};
    struct tm t;
    std::memset(&t, 0, sizeof(t));
    if (std::sscanf(s.c_str(), "%*3s, %d %3s %d %d:%d:%d GMT",
                    &t.tm_mday, mon, &t.tm_year, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return -1;
    t.tm_year -= 1900;
    const char* p = std::strstr(MONTHS, mon);
    if (!p) return -1;
    t.tm_mon = static_cast<int>((p - MONTHS) / 3);
    return utcMktime(&t);
}

GetHandler::GetHandler()
{
}
GetHandler::~GetHandler()
{
}

void GetHandler::handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server)
{
    std::string uri = StringUtils::stripQueryString(request.getUri());

    // try_files: probe each configured path before falling back to normal resolution
    std::string path;
    if (!route.getTryFiles().empty())
    {
        path = _tryFilesResolve(uri, route);
    }
    if (path.empty())
    {
        path = _resolvePath(uri, route);
    }

    if (path.empty())
    {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    // If path doesn't exist, check whether appending a slash resolves to a directory
    // (e.g., /uploads -> /uploads/ when route root is a directory)
    if (!FileUtils::fileExists(path))
    {
        if (!uri.empty() && uri[uri.size() - 1] != '/')
        {
            std::string altPath = _resolvePath(uri + "/", route);
            if (!altPath.empty() && FileUtils::isDirectory(altPath))
            {
                response.setStatus(301);
                response.setHeader("Location", uri + "/");
                response.setBody("");
                response.setReady(true);
                return;
            }
        }
        response.buildError(404, server.getErrorPages(), route.getRoot());
        return;
    }

    // Existing directory: redirect to URI with trailing slash
    if (FileUtils::isDirectory(path))
    {
        if (!uri.empty() && uri[uri.size() - 1] != '/')
        {
            response.setStatus(301);
            response.setHeader("Location", uri + "/");
            response.setBody("");
            response.setReady(true);
            return;
        }
        _serveDirectory(path, route, response);
        return;
    }

    _serveFile(path, request, response);
}

void GetHandler::_serveFile(const std::string& path, const Request& request, Response& response)
{
    struct stat st;
    if (!FileUtils::getFileStat(path, st))
    {
        response.setStatus(403);
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><body><h1>403 Forbidden</h1></body></html>");
        response.setReady(true);
        return;
    }

    // Always advertise byte-range support
    response.setHeader("Accept-Ranges", "bytes");

    // Compute and send ETag + Last-Modified
    std::string etag = _computeEtag(st);
    response.setHeader("ETag", etag);

    // Format Last-Modified as RFC 1123 date
    char dateBuf[128];
    std::tm* tmPtr = std::gmtime(&st.st_mtime);
    if (tmPtr != NULL)
    {
        std::strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", tmPtr);
        response.setHeader("Last-Modified", dateBuf);
    }

    // Check conditional headers — may short-circuit with 304
    if (_checkConditional(st, request, response))
    {
        return;
    }

    std::string mimeType = StringUtils::getMimeType(StringUtils::getExtension(path));
    size_t fileSize = static_cast<size_t>(st.st_size);

    // Range request handling — may respond with 206
    std::string rangeHeader = request.getHeader("Range");
    if (!rangeHeader.empty() && _handleRange(path, fileSize, rangeHeader, response))
    {
        response.setHeader("Content-Type", mimeType);
        return;
    }

    // Regular 200 response
    try
    {
        std::string content = FileUtils::readFile(path);
        response.setStatus(200);
        response.setHeader("Content-Type", mimeType);
        response.setBody(content);
        response.setReady(true);
    }
    catch (const std::exception& e)
    {
        (void)e;
        response.setStatus(403);
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><body><h1>403 Forbidden</h1></body></html>");
        response.setReady(true);
    }
}

// ETag = "<mtime_hex>-<size_hex>". Using mtime+size avoids needing a hash
// function (which would require libcrypto or implementing MD5 in C++98).
// This is what Apache httpd uses by default.
std::string GetHandler::_computeEtag(const struct stat& st) const
{
    return "\"" + StringUtils::toHex(static_cast<unsigned long>(st.st_mtime))
         + "-" + StringUtils::toHex(static_cast<unsigned long>(st.st_size)) + "\"";
}

// Returns true and sends 304 if client cache is still fresh.
// Checks If-None-Match (ETag) first, then If-Modified-Since.
bool GetHandler::_checkConditional(const struct stat& st, const Request& request,
                                   Response& response)
                                   {
    std::string etag = _computeEtag(st);

    std::string inm = request.getHeader("If-None-Match");
    if (!inm.empty())
    {
        if (inm == etag || inm == "*")
        {
            response.setStatus(304);
            response.setHeader("ETag", etag);
            response.setReady(true);
            return true;
        }
        return false;
    }

    std::string ims = request.getHeader("If-Modified-Since");
    if (!ims.empty())
    {
        time_t clientTime = parseHttpDate(ims);
        if (clientTime != -1 && st.st_mtime <= clientTime)
        {
            response.setStatus(304);
            response.setHeader("ETag", etag);
            response.setReady(true);
            return true;
        }
    }
    return false;
}

// Parses "bytes=start-end" and responds 206.
// Handles:  bytes=0-499  bytes=500-  bytes=-500
// Returns false if Range is malformed or unsatisfiable → caller sends 200.
bool GetHandler::_handleRange(const std::string& path, size_t fileSize,
                              const std::string& rangeHeader, Response& response)
{
    if (rangeHeader.substr(0, 6) != "bytes=") return false;

    std::string spec = rangeHeader.substr(6);
    size_t dashPos = spec.find('-');
    if (dashPos == std::string::npos) return false;

    std::string startStr = spec.substr(0, dashPos);
    std::string endStr   = spec.substr(dashPos + 1);

    size_t rangeStart = 0;
    size_t rangeEnd   = fileSize > 0 ? fileSize - 1 : 0;

    if (startStr.empty() && endStr.empty()) return false;

    if (startStr.empty())
    {
        // bytes=-N  →  last N bytes
        size_t suffix = static_cast<size_t>(std::atoi(endStr.c_str()));
        if (suffix == 0 || suffix > fileSize) return false;
        rangeStart = fileSize - suffix;
        rangeEnd   = fileSize - 1;
    }
    else
    {
        char* ep;
        long s = std::strtol(startStr.c_str(), &ep, 10);
        if (*ep != '\0' || s < 0) return false;
        rangeStart = static_cast<size_t>(s);
        if (!endStr.empty())
        {
            long e = std::strtol(endStr.c_str(), &ep, 10);
            if (*ep != '\0' || e < 0) return false;
            rangeEnd = static_cast<size_t>(e);
        }
    }

    if (rangeStart > rangeEnd || rangeEnd >= fileSize)
    {
        // 416 Range Not Satisfiable
        response.setStatus(416);
        std::ostringstream cr;
        cr << "bytes */" << fileSize;
        response.setHeader("Content-Range", cr.str());
        response.setBody("");
        response.setReady(true);
        return true;
    }

    size_t length = rangeEnd - rangeStart + 1;
    try
    {
        std::string data = FileUtils::readFileRange(path, rangeStart, length);
        std::ostringstream cr;
        cr << "bytes " << rangeStart << "-" << rangeEnd << "/" << fileSize;

        response.setStatus(206);
        response.setHeader("Content-Range", cr.str());
        response.setBody(data);
        response.setReady(true);
        return true;
    }
    catch (const std::exception& e)
    {
        (void)e;
        return false;
    }
}

void GetHandler::_serveDirectory(const std::string& path, const RouteConfig& route, Response& response)
{
    if (!route.getIndex().empty())
    {
        std::string indexPath = FileUtils::joinPath(path, route.getIndex());
        if (FileUtils::fileExists(indexPath) && !FileUtils::isDirectory(indexPath))
        {
            Request emptyReq;
            _serveFile(indexPath, emptyReq, response);
            return;
        }
    }

    if (route.getAutoindex())
    {
        _generateAutoindex(path, route.getPath(), response);
    }
    else
    {
        response.setStatus(403);
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><body><h1>403 Forbidden</h1></body></html>");
        response.setReady(true);
    }
}

void GetHandler::_generateAutoindex(const std::string& path, const std::string& uri, Response& response)
{
    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><title>Index of " << uri << "</title></head>\n"
         << "<body>\n"
         << "<h1>Index of " << uri << "</h1>\n"
         << "<hr>\n"
         << "<table>\n";

    try
    {
        std::vector<std::string> entries = FileUtils::listDirectory(path);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            std::string entryPath = FileUtils::joinPath(path, entries[i]);
            std::string displayName = entries[i];
            if (FileUtils::isDirectory(entryPath))
            {
                displayName += "/";
            }
            html << "<tr><td><a href=\"" << StringUtils::htmlEscape(displayName) << "\">"
                 << StringUtils::htmlEscape(displayName) << "</a></td></tr>\n";
        }
    }
    catch (const std::exception& e)
    {
        (void)e;
    }

    html << "</table>\n"
         << "<hr>\n"
         << "</body>\n"
         << "</html>\n";

    response.setStatus(200);
    response.setHeader("Content-Type", "text/html");
    response.setBody(html.str());
    response.setReady(true);
}

std::string GetHandler::_resolvePath(const std::string& rawUri, const RouteConfig& route)
{
    std::string decodedUri = StringUtils::decodeUrl(rawUri);
    return StringUtils::resolvePath(decodedUri, route.getPath(), route.getRoot());
}

// try_files: replaces $uri with the actual request path, then probes each
// candidate in order. The last entry is a fallback path (if prefixed with /
// it is treated as an absolute URI redirect; otherwise as a filesystem path).
std::string GetHandler::_tryFilesResolve(const std::string& uri, const RouteConfig& route)
{
    const std::vector<std::string>& tryFiles = route.getTryFiles();
    if (tryFiles.empty()) return "";

    // Try all entries except the last (which is the fallback)
    for (size_t i = 0; i + 1 < tryFiles.size(); ++i)
    {
        std::string candidate = tryFiles[i];
        // Replace $uri token with actual request path
        size_t pos = candidate.find("$uri");
        if (pos != std::string::npos)
        {
            candidate.replace(pos, 4, uri);
        }
        std::string resolved = _resolvePath(candidate, route);
        if (!resolved.empty() && FileUtils::fileExists(resolved))
        {
            return resolved;
        }
    }

    // Fallback: last entry
    std::string fallback = tryFiles.back();
    size_t pos = fallback.find("$uri");
    if (pos != std::string::npos)
    {
        fallback.replace(pos, 4, uri);
    }
    return _resolvePath(fallback, route);
}
