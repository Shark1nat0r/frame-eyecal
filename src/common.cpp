#include "common.h"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string home(const char *fallback) {
    const char *h = getenv("HOME");
    return h ? h : fallback;
}

std::string fmt(const char *f, ...) {
    char b[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

void take_lines(std::string &buf, std::vector<std::string> &out) {
    size_t start = 0;
    for (size_t nl = 0; (nl = buf.find('\n', start)) != std::string::npos; start = nl + 1)
        out.push_back(buf.substr(start, nl - start));
    buf.erase(0, start);
}

uint32_t next_cp(const std::string &t, size_t &i) {
    auto c = static_cast<unsigned char>(t[i++]);
    if (c < 0x80) return c;
    int n = 1; // continuation bytes
    if (c >= 0xF0) n = 3;
    else if (c >= 0xE0) n = 2;
    uint32_t cp = c & (0x3F >> n);
    while (n-- && i < t.size()) cp = cp << 6 | (static_cast<unsigned char>(t[i++]) & 0x3F);
    return cp;
}
