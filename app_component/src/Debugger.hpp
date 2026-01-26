// ===== Debug helpers =====
#include <iomanip>
#include <sstream>
#include <chrono>

//#define NOELDEBUG 1

static std::string nowMs() {
    using namespace std::chrono;
    //auto t = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    std::ostringstream os;
    //os << "[" << t << "ms]";
    return os.str();
}
static std::string printable(const std::string& s, size_t maxLen = 200) {
    std::ostringstream os;
    os << '"';
    for (size_t i = 0; i < s.size() && i < maxLen; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') os << "\\r";
        else if (c == '\n') os << "\\n";
        else if (c == '\t') os << "\\t";
        else if (c < 32 || c >= 127) os << "\\x" << std::hex << std::uppercase << (int)c << std::dec;
        else os << (char)c;
    }
    if (s.size() > maxLen) os << "\"...(" << (s.size() - maxLen) << " more)";
    else os << '"';
    return os.str();
}
static std::string hexDump(const std::string& s, size_t maxLen = 128) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    size_t n = (std::min)(maxLen, s.size());
    for (size_t i = 0; i < n; ++i) {
        os << std::setw(2) << (int)(unsigned char)s[i] << ' ';
    }
    if (s.size() > maxLen) os << "...";
    return os.str();
}
#ifdef NOELDEBUG
#define DBG(msg) do { std::cerr << nowMs() << " " << msg << std::endl; } while(0)
#else
#define DBG(msg) do {} while(0)
#endif
