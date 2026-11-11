#include "link.h"

#include <string>
#include <sys/types.h>
#include <cstddef>
#include <unistd.h>
#include <vector>

#include "common.h"

Link g_link;

void Link::send(const std::string &s) const {
    std::string m = s + "\n";
    (void)!write(fd, m.data(), m.size());
}

bool Link::poll(std::vector<std::string> &out) {
    char b[512];
    for (;;) {
        ssize_t n = read(fd, b, sizeof b);
        if (n > 0) {
            buf.append(b, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) return false;
        break;
    }
    take_lines(buf, out);
    return true;
}
