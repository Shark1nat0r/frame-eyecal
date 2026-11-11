#include "tracker_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common.h"

bool TrackerLog::open() {
    if (!f) f = fopen(path.c_str(), "r");
    return f != nullptr;
}

void TrackerLog::skip() {
    if (!open()) return;
    fseek(f, 0, SEEK_END);
    pos = ftell(f);
}

std::vector<std::string> TrackerLog::poll() {
    std::vector<std::string> out;
    if (!open()) return out;
    fseek(f, 0, SEEK_END);
    if (ftell(f) < pos) {
        pos = 0;
        part.clear();
    } // rewritten
    fseek(f, pos, SEEK_SET);
    char b[8192];
    for (size_t n = 0; (n = fread(b, 1, sizeof b, f)) > 0;) part.append(b, n);
    pos = ftell(f);
    take_lines(part, out);
    return out;
}

// The text after key in l, nullptr if l does not have it
static const char *after(const std::string &l, const char *key) {
    size_t p = l.find(key);
    return p == std::string::npos ? nullptr : l.c_str() + p + strlen(key);
}

TrackerLine parse_tracker_line(const std::string &l) {
    TrackerLine r;
    if (size_t sp = l.find(" startup with PID"); sp != std::string::npos) {
        r.startup = true;
        size_t v = l.rfind("eyetracking ", sp);
        if (v != std::string::npos) r.version = l.substr(v + 12, sp - v - 12);
    }
    if (const char *fr = after(l, "Set framerate ")) r.fps = atof(fr);
    if (l.find("Accept usercal") != std::string::npos) {
        r.accept = true;
        if (const char *p = strstr(l.c_str(), "lf:")) sscanf(p, "lf:%lf rf:%lf", &r.lf, &r.rf);
    }
    if (const char *why = after(l, "Reject usercal: ")) {
        auto starts = [&](const char *s) { return strncmp(why, s, strlen(s)) == 0; };
        if (starts("Click too slow")) r.reject = Reject::SLOW;
        else if (starts("Fixation")) r.reject = Reject::EYES;
        else if (starts("Click down/up")) r.reject = Reject::POINTER;
        else r.reject = Reject::OTHER;
    }
    r.drop_l = l.find("UKF L: Large dt") != std::string::npos;
    r.drop_r = l.find("UKF R: Large dt") != std::string::npos;
    return r;
}
