#include "WifiHandler.hpp"
#include "Debugger.hpp"
#include "sleep.h"
#include <iostream>
#include <ostream>

// ===== HttpRequest helpers =====
std::string HttpRequest::trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || std::isspace((unsigned char)s.back()))) s.pop_back();
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}
std::string HttpRequest::toLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string HttpRequest::stripQuotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

HttpRequest::KV HttpRequest::parseFlatJson(const std::string& json) {
    KV out;
    std::string s = json;
    size_t l = s.find('{');
    size_t r = s.rfind('}');
    if (l == std::string::npos || r == std::string::npos || r <= l) return out;
    s = s.substr(l + 1, r - l - 1);

    std::string cur; bool inQ = false; char qc = 0;
    // local lambdas are fine in Vitis
    struct Flusher {
        static void flush(std::string& cur, KV& out, std::string (*trim)(std::string), std::string (*toLower)(std::string), std::string (*stripQuotes)(const std::string&)) {
            std::string kv = trim(cur);
            cur.clear();
            if (kv.empty()) return;
            size_t colon = kv.find(':');
            if (colon == std::string::npos) return;
            std::string k = trim(kv.substr(0, colon));
            std::string v = trim(kv.substr(colon + 1));
            k = stripQuotes(k);
            if (!v.empty()) {
                if (v[0] == '"' || v[0] == '\'') v = stripQuotes(v);
                else {
                    std::string low = toLower(v);
                    if (low == "true" || low == "false" || low == "null") v = low;
                }
            }
            if (!k.empty()) out[k] = v;
        }
    };

    for (size_t idx = 0; idx < s.size(); ++idx) {
        char ch = s[idx];
        if ((ch == '"' || ch == '\'') && !inQ) { inQ = true; qc = ch; cur.push_back(ch); continue; }
        if (inQ) {
            cur.push_back(ch);
            if (ch == qc) inQ = false;
            continue;
        }
        if (ch == ',') Flusher::flush(cur, out, &HttpRequest::trim, &HttpRequest::toLower, &HttpRequest::stripQuotes);
        else cur.push_back(ch);
    }
    Flusher::flush(cur, out, &HttpRequest::trim, &HttpRequest::toLower, &HttpRequest::stripQuotes);
    return out;
}

const HttpRequest::KV& HttpRequest::toMap() const {
    if (!_jsonParsed) {
        _jsonKV.clear();
        if (dataType == DataType::JSON) {
            _jsonKV = parseFlatJson(body);
        }
        _jsonParsed = true;
    }
    return _jsonKV;
}

// ===== WifiHandler =====

void WifiHandler::onHttp(HttpMethod method, std::string path, HttpHandler h)
{
#if 1
    // Fallback for toolchains missing insert_or_assign
    RouteKey key; key.method = method; key.path = path;
    routes_.erase(key);
    routes_.insert(std::make_pair(key, h));
#else
    routes_.insert_or_assign(RouteKey{ method, std::move(path) }, std::move(h));
#endif
}

void WifiHandler::feed(const std::string chunk)
{
    DBG("Feed: +" << chunk.size() << " bytes");
    _stream.append(chunk);
    processStream();
}

bool WifiHandler::sendRaw(int link_id, const std::string& fullHttpResponse, bool autoClose) {
    if (_openLinks.find(link_id) == _openLinks.end()) {
        DBG("RESP: link " << link_id << " already closed, dropping");
        return false;
    }

    const int len = (int)fullHttpResponse.size();
    const std::string cmd = "AT+CIPSEND=" + std::to_string(link_id) + "," + std::to_string(len) + "\r\n";
    DBG("RESP: link=" << link_id << " sending " << len << " bytes");

    // 1) Flush any stale RX
    std::string rx; m_Uart.ReadFromUart(rx); rx.clear();

    // 2) CIPSEND
    m_Uart.WriteToUart(cmd);

    // 3) Wait for '>' prompt (or ERROR/busy)
    const unsigned T1_MS = 1500;             // prompt timeout
    unsigned waited = 0;
    bool got_prompt = false;
    while (waited < T1_MS) {
        std::string chunk;
        m_Uart.ReadFromUart(chunk);
        if (!chunk.empty()) {
            rx += chunk;
            if (rx.find('>') != std::string::npos) { got_prompt = true; break; }
            if (rx.find("ERROR") != std::string::npos || rx.find("busy") != std::string::npos) {
                DBG("RESP: CIPSEND refused: " << printable(rx));
                return false;
            }
        } else {
            usleep(1000); waited += 1;
        }
    }
    if (!got_prompt) {
        DBG("RESP: timeout waiting for '>' prompt. RX=" << printable(rx, 200));
        return false;
    }

    // 4) Send payload exactly once '>' seen
    m_Uart.WriteToUart(fullHttpResponse);

    // 5) Wait for 'SEND OK' (or 'ERROR')
    rx.clear();
    const unsigned T2_MS = 3000;
    waited = 0;
    bool sent_ok = false;
    while (waited < T2_MS) {
        std::string chunk;
        m_Uart.ReadFromUart(chunk);
        if (!chunk.empty()) {
            rx += chunk;
            if (rx.find("SEND OK") != std::string::npos) { sent_ok = true; break; }
            if (rx.find("ERROR")   != std::string::npos)  { break; }
        } else {
            usleep(1000); waited += 1;
        }
    }
    DBG("RESP: after payload -> " << printable(rx, 200));
    if (!sent_ok) {
        DBG("RESP: didn't see 'SEND OK' (might still succeed, but safer to stop)");
        // fall through; you can choose to return here
    }

    if (autoClose)
    m_Uart.WriteToUart("AT+CIPCLOSE=" + std::to_string(link_id) + "\r\n");

    return true;
}


std::string WifiHandler::trim(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0; while (i < s.size() && (s[i] == '\r' || s[i] == '\n')) ++i;
    return s.substr(i);
}
std::string WifiHandler::toLower(std::string s) { for (size_t i=0;i<s.size();++i) s[i]=(char)std::tolower((unsigned char)s[i]); return s; }
std::string WifiHandler::toUpper(std::string s) { for (size_t i=0;i<s.size();++i) s[i]=(char)std::toupper((unsigned char)s[i]); return s; }

bool WifiHandler::takeLine(std::string& outLine) {
    size_t pos = _stream.find("\r\n");
    if (pos == std::string::npos) return false;
    outLine = _stream.substr(0, pos);
    _stream.erase(0, pos + 2);
    DBG("Line: " << printable(outLine));
    return true;
}

void WifiHandler::processStream() {
    while (true) {
        if (_ipd.active) {
            if (_stream.size() < _ipd.remaining) {
                appendToLink(_ipd.link_id, _stream);
                _ipd.remaining -= _stream.size();
                _stream.clear();
                return;
            } else {
                appendToLink(_ipd.link_id, _stream.substr(0, _ipd.remaining));
                _stream.erase(0, _ipd.remaining);
                _ipd.active = false;
                _ipd.remaining = 0;
                tryParseHttp(_ipd.link_id);
                continue;
            }
        }

        if (_stream.rfind("+IPD,", 0) == 0) {
            size_t colon = _stream.find(':');
            if (colon == std::string::npos) return;
            int id = -1; size_t len = 0;
            std::string header = _stream.substr(1, colon - 1);
            size_t p = header.find(',');
            if (p == std::string::npos) { DBG("WARN malformed IPD header A: " << printable(header)); _stream.erase(0, colon + 1); continue; }
            size_t q = header.find(',', p + 1);
            if (q == std::string::npos) { DBG("WARN malformed IPD header B: " << printable(header)); _stream.erase(0, colon + 1); continue; }
            // If exceptions are disabled in BSP, consider replacing with manual parse
            try {
                id = std::stoi(header.substr(p + 1, q - (p + 1)));
                len = (size_t)std::stoul(header.substr(q + 1));
            } catch (...) {
                DBG("WARN malformed IPD numbers: " << printable(header));
                _stream.erase(0, colon + 1);
                continue;
            }
            DBG("+IPD header: id=" << id << " len=" << len);
            _stream.erase(0, colon + 1);

            if (_stream.size() < len) {
                appendToLink(id, _stream);
                size_t need = len - _stream.size();
                DBG("+IPD: appended " << _stream.size() << ", need " << need << " more");
                _ipd.active = true;
                _ipd.link_id = id;
                _ipd.remaining = need;
                _stream.clear();
                return;
            } else {
                appendToLink(id, _stream.substr(0, len));
                DBG("+IPD: completed with " << len << " bytes");
                _stream.erase(0, len);
                tryParseHttp(id);
                continue;
            }
        }

        std::string line;
        if (!takeLine(line)) return;
        if (line.empty()) continue;

        if (endsWith(line, ",CONNECT")) {
            int id = parseLeadingInt(line);
            DBG("EVENT: " << id << " CONNECT");
            _openLinks.insert(id);
            if (id >= 0 && _ev.onConnect) _ev.onConnect(id);
            continue;
        }
        if (endsWith(line, ",CLOSED")) {
            int id = parseLeadingInt(line);
            DBG("EVENT: " << id << " CLOSED");
            if (id >= 0 && _ev.onClosed) _ev.onClosed(id);
            _links.erase(id);
            _openLinks.erase(id);
            continue;
        }

        if (startsWith(line, "+STA_CONNECTED:")) { std::string mac = extractQuoted1(line); if (_ev.onStaConnected) _ev.onStaConnected(mac); continue; }
        if (startsWith(line, "+STA_DISCONNECTED:")) { std::string mac = extractQuoted1(line); if (_ev.onStaDisconnected) _ev.onStaDisconnected(mac); continue; }
        if (startsWith(line, "+DIST_STA_IP:")) { std::pair<std::string,std::string> pr = extractQuoted2(line); if (_ev.onDistStaIp) _ev.onDistStaIp(pr.first, pr.second); continue; }

        DBG("UNHANDLED line: " << printable(line));
    }
}

void WifiHandler::appendToLink(int id, const std::string& bytes) {
    DBG("Link " << id << " append +" << bytes.size() << " (buf=" << _links[id].buf.size() << "->"
        << (_links[id].buf.size() + bytes.size()) << ")");
    _links[id].buf.append(bytes);
}

static HttpMethod methodFromToken(const std::string& t) {
    if (t == "GET")  return HttpMethod::GET;
    if (t == "POST") return HttpMethod::POST;
    return HttpMethod::GET;
}

DataType WifiHandler::deduceDataType(const std::map<std::string, std::string>& headers) {
    std::map<std::string, std::string>::const_iterator it = headers.find("content-type");
    if (it == headers.end()) return DataType::UNKNOWN;
    std::string ct = toLower(it->second);
    size_t sc = ct.find(';');
    if (sc != std::string::npos) ct.erase(sc);
    if (ct == "application/json")         return DataType::JSON;
    if (ct == "application/octet-stream") return DataType::BINARY;
    if (ct == "text/plain")               return DataType::TEXT;
    if (ct == "application/binary")       return DataType::BINARY;
    if (ct == "binary/octet-stream")      return DataType::BINARY;
    return DataType::UNKNOWN;
}

void WifiHandler::tryParseHttp(int id) {
    if (_openLinks.find(id) == _openLinks.end()) {
        DBG("HTTP: link " << id << " closed; drop buffered request (" << _links[id].buf.size() << " bytes)");
        _links.erase(id);
        return;
    }
    std::string& buf = _links[id].buf;
    DBG("HTTP? link " << id << " buf=" << buf.size());

    size_t hdrEnd = buf.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) { DBG("HTTP: no header end yet"); return; }

    size_t lineEnd = buf.find("\r\n");
    if (lineEnd == std::string::npos || lineEnd > hdrEnd) { DBG("HTTP: no request line yet"); return; }
    std::string requestLine = buf.substr(0, lineEnd);
    HttpMethod method;
    std::string path;
    parseRequestLine(requestLine, method, path);
    DBG("HTTP reqLine: " << printable(requestLine) << " -> method=" << (method == HttpMethod::GET ? "GET" : "POST") << " path=" << path);

    std::string headersBlock = buf.substr(lineEnd + 2, hdrEnd - (lineEnd + 2));
    std::map<std::string,std::string> headers = parseHeaders(headersBlock);
    DBG("HTTP headers (" << headers.size() << "): " << printable(headersBlock, 300));

    size_t bodyLen = 0;
    std::map<std::string,std::string>::const_iterator it = headers.find("content-length");
    if (it != headers.end()) {
        bodyLen = (size_t)std::stoul(it->second);
    }
    DBG("HTTP bodyLen=" << bodyLen);

    size_t totalNeeded = hdrEnd + 4 + bodyLen;
    if (buf.size() < totalNeeded) {
        DBG("HTTP: waiting for body: have=" << buf.size() << " need=" << totalNeeded
            << " missing=" << (totalNeeded - buf.size()));
        return;
    }
    std::string body = buf.substr(hdrEnd + 4, bodyLen);
    DBG("HTTP body ready (" << body.size() << "): " << printable(body, 200));

    HttpRequest req;
    req.link_id = id;
    req.method = method;
    req.path = path;
    req.raw_headers = headersBlock;
    req.headers = headers;
    req.body = body;
    req.dataType = deduceDataType(req.headers);
    req.sendFn = [this](int id2, const std::string& fullHttpResponse) {
        this->sendRaw(id2, fullHttpResponse, true);
    };

    buf.erase(0, totalNeeded);

    RouteKey key; key.method = method; key.path = path;
    DBG("HTTP dispatch -> (" << (method == HttpMethod::GET ? "GET" : "POST") << ", " << path << ")");
    std::unordered_map<RouteKey, HttpHandler, RouteKeyHash>::iterator rit = routes_.find(key);
    if (rit != routes_.end()) {
        rit->second(req);
        DBG("HTTP handler returned for path=" << path);
    } else {
        DBG("HTTP 404 (no handler) for path=" << path);
    }

    // Parse next if pipelined
    tryParseHttp(id);
}

bool WifiHandler::startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool WifiHandler::endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

int WifiHandler::parseLeadingInt(const std::string& s) {
    int val = 0; size_t i = 0; bool any = false;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) { any = true; val = val * 10 + (s[i] - '0'); ++i; }
    return any ? val : -1;
}

bool WifiHandler::parseIpdHeader(const std::string& line, int& id, size_t& len, size_t& colonPos) {
    if (!startsWith(line, "+IPD,")) return false;
    size_t p = 5, q = p;
    while (q < line.size() && std::isdigit((unsigned char)line[q])) ++q;
    if (q == p || q >= line.size() || line[q] != ',') return false;
    id = std::stoi(line.substr(p, q - p));
    p = q + 1; q = p;
    while (q < line.size() && std::isdigit((unsigned char)line[q])) ++q;
    if (q == p || q >= line.size() || line[q] != ':') return false;
    len = (size_t)std::stoul(line.substr(p, q - p));
    colonPos = q;
    return true;
}

void WifiHandler::parseRequestLine(const std::string& s, HttpMethod& method, std::string& path) {
    size_t a = s.find(' ');
    size_t b = (a == std::string::npos) ? std::string::npos : s.find(' ', a + 1);
    std::string m = (a == std::string::npos) ? s : s.substr(0, a);
    path = (a == std::string::npos || b == std::string::npos) ? "/" : s.substr(a + 1, b - a - 1);
    method = methodFromToken(m);
}

std::map<std::string, std::string> WifiHandler::parseHeaders(const std::string& block) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < block.size()) {
        size_t end = block.find("\r\n", start);
        if (end == std::string::npos) end = block.size();
        std::string line = block.substr(start, end - start);
        start = (end == block.size()) ? end : end + 2;
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = toLower(trim(line.substr(0, colon)));
        std::string v = trim(line.substr(colon + 1));
        size_t i = 0; while (i < v.size() && std::isspace((unsigned char)v[i])) ++i;
        v.erase(0, i);
        out[k] = v;
    }
    return out;
}

std::string WifiHandler::extractQuoted1(const std::string& s) {
    size_t a = s.find('"'); if (a == std::string::npos) return std::string();
    size_t b = s.find('"', a + 1); if (b == std::string::npos) return std::string();
    return s.substr(a + 1, b - a - 1);
}

std::pair<std::string, std::string> WifiHandler::extractQuoted2(const std::string& s) {
    size_t a = s.find('"'); if (a == std::string::npos) return std::pair<std::string,std::string>();
    size_t b = s.find('"', a + 1); if (b == std::string::npos) return std::pair<std::string,std::string>();
    size_t c = s.find('"', b + 1); if (c == std::string::npos) return std::pair<std::string,std::string>();
    size_t d = s.find('"', c + 1); if (d == std::string::npos) return std::pair<std::string,std::string>();
    return std::pair<std::string,std::string>( s.substr(a + 1, b - a - 1), s.substr(c + 1, d - c - 1) );
}

int WifiHandler::openClient(const Url& u) {
    int id = 0;
    while (_openLinks.count(id)) ++id;
    if (id > 4) return -1;

    std::string type = u.is_tls ? "SSL" : "TCP";
    std::ostringstream cmd;
    cmd << "AT+CIPSTART=" << id << ",\"" << type << "\",\"" << u.host
        << "\"," << u.port << "\r\n";

    DBG("CIPSTART cmd: " << printable(cmd.str()));
    m_Uart.WriteToUart(cmd.str());

    std::string rx;
    unsigned waited = 0;
    while (waited < 8000) {
        std::string chunk;
        m_Uart.ReadFromUart(chunk);
        if (!chunk.empty()) {
            rx += chunk;
            if (rx.find("CONNECT") != std::string::npos) {
                DBG("CIPSTART success: " << printable(rx));
                _openLinks.insert(id);
                return id;
            }
            if (rx.find("ALREADY CONNECTED") != std::string::npos) {
                DBG("CIPSTART already connected: " << printable(rx));
                _openLinks.insert(id);
                return id;
            }
            if (rx.find("ERROR") != std::string::npos ||
                rx.find("FAIL")  != std::string::npos) {
                    auto m = printable(rx);
                DBG("CIPSTART fail RX=" << printable(rx));
                return -1;
            }
        } else { usleep(1000); waited += 1; }
    }

    DBG("CIPSTART timeout RX=" << printable(rx));
    return -1;
}

void WifiHandler::sendHttpRequest(const std::string& url,
                                  HttpMethod method,
                                  const std::string& body,
                                  const std::string& contentType,
                                  std::function<void(const HttpResponse&)> onResponse)
{
    std::cout << "Sending to server now" << std::endl;
    Url u;
    if (!parseUrl(url, u)) {
        DBG("Bad URL: " << url);
        return;
    }

    int id = openClient(u);
    if (id < 0) {
        DBG("CIPSTART failed");
        return;
    }

    // --- Build request ---
    std::ostringstream req;
    std::string methodStr = (method == HttpMethod::GET ? "GET" : "POST");
    req << methodStr << " " << u.path << " HTTP/1.1\r\n";
    req << "Host: " << u.host << "\r\n";
    req << "User-Agent: esp8266-vitis/1.0\r\n";
    req << "Connection: close\r\n";
    if (method == HttpMethod::POST) {
        req << "Content-Type: " << contentType << "\r\n";
        req << "Content-Length: " << body.size() << "\r\n\r\n";
    } else {
        req << "\r\n";
    }

    std::string headerStr = req.str();

    // --- Send headers ---
    if (!sendRaw(id, headerStr)) {
        DBG("Header send failed");
        return;
    }

    // --- Send body in chunks ---
    if (method == HttpMethod::POST && !body.empty()) {
        const size_t CHUNK = 2048;
        size_t offset = 0;
        while (offset < body.size()) {
            size_t n = std::min(CHUNK, body.size() - offset);
            if (!sendRaw(id, body.substr(offset, n))) {
                DBG("Chunk send failed");
                return;
            }
            offset += n;
        }
    }

    // --- Wait for and read response ---
    std::string rx;
    unsigned waited = 0;
    const unsigned TIMEOUT_MS = 5000;

    while (waited < TIMEOUT_MS) {
        std::string chunk;
        m_Uart.ReadFromUart(chunk);
        if (!chunk.empty()) {
            rx += chunk;
            if (rx.find("CLOSED") != std::string::npos)
                break;
        } else { usleep(1000); waited++; }
    }

    // --- Parse HTTP response ---
    HttpResponse resp;
    size_t hdrEnd = rx.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) {
        DBG("Bad HTTP response");
        m_Uart.WriteToUart("AT+CIPCLOSE=" + std::to_string(id) + "\r\n");
        return;
    }

    std::string statusLine = rx.substr(0, rx.find("\r\n"));
    int code = 0;
    sscanf(statusLine.c_str(), "HTTP/%*s %d", &code);
    resp.status = code;
    resp.raw_headers = rx.substr(rx.find("\r\n") + 2, hdrEnd - rx.find("\r\n") - 2);
    resp.headers = parseHeaders(resp.raw_headers);

    size_t bodyStart = hdrEnd + 4;
    resp.body = rx.substr(bodyStart);
    resp.dataType = deduceDataType(resp.headers);
    
    // --- Read remaining body if not all arrived yet ---
    auto it = resp.headers.find("content-length");
    if (it != resp.headers.end()) {
        size_t expected = std::stoul(it->second);
        while (resp.body.size() < expected) {
            std::string chunk;
            m_Uart.ReadFromUart(chunk);
            if (chunk.empty()) {
                usleep(1000); // tiny wait to allow next UART fragment
                continue;
            }
            resp.body += chunk;
        }
    }
    
    // --- Close link ---
    m_Uart.WriteToUart("AT+CIPCLOSE=" + std::to_string(id) + "\r\n");
    
    // --- Dispatch callback ---
    if (onResponse)
        onResponse(resp);
}
