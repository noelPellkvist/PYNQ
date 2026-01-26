#pragma once
#include <string>
#include <map>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <set>
#include <cctype>
#include <cstdint>
#include "Uart.hpp"

// ---- HTTP enums ----
enum class HttpMethod { GET, POST };
enum class DataType { JSON, BINARY, TEXT, UNKNOWN };



static std::string buildHttpResponse(int status, const std::string& body, const std::string& contentType) {
    std::ostringstream os;
    const char* reason = (status == 200 ? "OK" :
        status == 400 ? "Bad Request" :
        status == 404 ? "Not Found" :
        status == 500 ? "Internal Server Error" : "OK");
    os << "HTTP/1.1 " << status << " " << reason << "\r\n"
       << "Content-Type: " << contentType << "\r\n"
       << "Connection: close\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "\r\n"
       << body;
    return os.str();
}

struct HttpResponse {
    int status = -1;
    std::map<std::string, std::string> headers; // lower-cased keys
    std::string body;
    DataType dataType = DataType::UNKNOWN;
    std::string raw_headers;
};

struct HttpRequest {
    int         link_id = -1;
    HttpMethod  method = HttpMethod::GET;
    std::string path;

    std::string raw_headers;
    std::map<std::string, std::string> headers;   // lower-cased keys
    std::string body;
    DataType    dataType = DataType::UNKNOWN;

    using KV = std::unordered_map<std::string, std::string>;
    const KV& toMap() const;   // parses once, then caches

    const char* binaryData() const { return body.empty() ? 0 : body.data(); }
    size_t      binarySize() const { return body.size(); }

    std::function<void(int, const std::string&)> sendFn;

    void respond(int statusCode, const std::string& body,
                 const std::string& contentType = "application/json") const {
        if (!sendFn) return;

        std::string reason;
        switch (statusCode) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 404: reason = "Not Found"; break;
        case 500: reason = "Internal Server Error"; break;
        default:  reason = "ERR"; break;
        }

        std::string resp =
            "HTTP/1.1 " + std::to_string(statusCode) + " " + reason + "\r\n"
            "Content-Type: " + contentType + "\r\n"
            "Connection: close\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" +
            body;

        sendFn(link_id, resp);
    }

private:
    // tiny JSON cache + parser
    mutable bool _jsonParsed = false;
    mutable KV   _jsonKV;

    static std::string trim(std::string s);
    static std::string toLower(std::string s);
    static std::string stripQuotes(const std::string& s);
    static KV          parseFlatJson(const std::string& json);
};

struct EspEvents {
    std::function<void(int)> onConnect;
    std::function<void(int)> onClosed;
    std::function<void(std::string)> onStaConnected;
    std::function<void(std::string)> onStaDisconnected;
    std::function<void(std::string, std::string)> onDistStaIp;
};

class WifiHandler
{
public:
    using HttpHandler = std::function<void(const HttpRequest&)>;

    explicit WifiHandler(EspEvents ev, Uart& uart) : _ev(ev), m_Uart(uart) {}
    ~WifiHandler() {}

    void onHttp(HttpMethod method, std::string path, HttpHandler handler);
    void feed(const std::string chunk);
    bool sendRaw(int link_id, const std::string& fullHttpResponse, bool autoClose = false);
    void sendHttpRequest(const std::string& url,
                                  HttpMethod method,
                                  const std::string& body,
                                  const std::string& contentType,
                                  std::function<void(const HttpResponse&)> onResponse);

private:
    std::set<int> _openLinks;
    Uart& m_Uart;
    std::string _stream;
    struct LinkBuf { std::string buf; };
    std::unordered_map<int, LinkBuf> _links;

    struct PendingIpd {
        int link_id;
        size_t remaining;
        bool active;
        PendingIpd() : link_id(-1), remaining(0), active(false) {}
    } _ipd;

    EspEvents _ev;

    // Route key: (method, path)
    struct RouteKey {
        HttpMethod method;
        std::string path;
        bool operator==(const RouteKey& o) const {
            return method == o.method && path == o.path;
        }
    };
    struct RouteKeyHash {
        size_t operator()(const RouteKey& k) const {
            return (size_t)std::hash<int>()((int)k.method)
                ^ ((size_t)std::hash<std::string>()(k.path) << 1);
        }
    };

    std::unordered_map<RouteKey, HttpHandler, RouteKeyHash> routes_;

    static std::string trim(std::string s);
    static std::string toLower(std::string s);
    static std::string toUpper(std::string s);

    // no std::optional: return true if a CRLF-terminated line was extracted
    bool takeLine(std::string& outLine);

    void processStream();
    void appendToLink(int id, const std::string& bytes);
    void tryParseHttp(int id);

    // no string_view
    static bool startsWith(const std::string& s, const std::string& p);
    static bool endsWith(const std::string& s, const std::string& p);
    static int  parseLeadingInt(const std::string& s);
    static bool parseIpdHeader(const std::string& line, int& id, size_t& len, size_t& colonPos);
    static void parseRequestLine(const std::string& s, HttpMethod& method, std::string& path);
    static std::map<std::string, std::string> parseHeaders(const std::string& block);
    static std::string extractQuoted1(const std::string& s);
    static std::pair<std::string, std::string> extractQuoted2(const std::string& s);

    static DataType deduceDataType(const std::map<std::string, std::string>& headers);

    struct Url {
        std::string scheme, host, path;
        uint16_t port = 0;
        bool is_tls = false;
    };

    static bool parseUrl(const std::string& s, Url& u) {
        auto p = s.find("://");
        if (p == std::string::npos) return false;
        u.scheme = toLower(s.substr(0, p));
        u.is_tls = (u.scheme == "https");
        size_t host_start = p + 3;
        size_t path_start = s.find('/', host_start);
        std::string hostport = (path_start == std::string::npos)
            ? s.substr(host_start)
            : s.substr(host_start, path_start - host_start);
        auto colon = hostport.find(':');
        if (colon != std::string::npos) {
            u.host = hostport.substr(0, colon);
            u.port = (uint16_t)std::stoi(hostport.substr(colon + 1));
        } else {
            u.host = hostport;
            u.port = u.is_tls ? 443 : 5000;
        }
        u.path = (path_start == std::string::npos) ? "/" : s.substr(path_start);
        return !u.host.empty();
    }

    int openClient(const Url& u);
    
};


