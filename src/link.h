// Line messages between the panel and the scene process, over a non-blocking socket.
#pragma once

#include <string>
#include <vector>

struct Link {
    int fd = -1;
    std::string buf;
    void send(const std::string &s) const;
    bool poll(std::vector<std::string> &out); // appends complete lines; false once the other side is gone
};
extern Link g_link;
