// The log lines below follow the parts of the eye tracker's log that the app reads (SteamVR 2.18).
#include <stdlib.h> // NOLINT(modernize-deprecated-headers): mkstemp is POSIX, declared only there
#include <cstdio>
#include <string>
#include <unistd.h>

#include "../src/tracker_log.h"
#include "check.h"

TEST(parse_startup_with_version) {
    auto e = parse_tracker_line("Thu Nov 11 2026 10:00:00.123 - eyetracking 2.18.2 startup with PID 4242");
    CHECK(e.startup);
    CHECK(e.version == "2.18.2");
    CHECK(!e.accept && e.reject == Reject::NONE && e.fps < 0);
}

TEST(parse_accept_with_fixation) {
    auto e = parse_tracker_line("10:00:01 Accept usercal (pt 3) lf:0.9990 rf:0.9950 n:5");
    CHECK(e.accept);
    CHECK_NEAR(e.lf, 0.999, 1e-9);
    CHECK_NEAR(e.rf, 0.995, 1e-9);
    auto bare = parse_tracker_line("10:00:01 Accept usercal");
    CHECK(bare.accept && bare.lf == 1 && bare.rf == 1);
}

TEST(parse_reject_reasons) {
    CHECK(parse_tracker_line("Reject usercal: Click too slow (0.61 s)").reject == Reject::SLOW);
    CHECK(parse_tracker_line("Reject usercal: Fixation lost").reject == Reject::EYES);
    CHECK(parse_tracker_line("Reject usercal: Click down/up moved 3 px").reject == Reject::POINTER);
    CHECK(parse_tracker_line("Reject usercal: Next interaction not mouse-up").reject == Reject::OTHER);
    CHECK(parse_tracker_line("usercal state ok").reject == Reject::NONE);
}

TEST(parse_framerate_and_dropouts) {
    CHECK_NEAR(parse_tracker_line("cam: Set framerate 90.000").fps, 90, 1e-9);
    CHECK(parse_tracker_line("cam: something else").fps < 0);
    auto l = parse_tracker_line("UKF L: Large dt 0.25");
    CHECK(l.drop_l && !l.drop_r);
    CHECK(parse_tracker_line("UKF R: Large dt 0.25").drop_r);
}

static void append(const std::string &path, const char *text) {
    FILE *f = fopen(path.c_str(), "a");
    CHECK(f != nullptr);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

TEST(tracker_log_follows_the_file) {
    char p[] = "/tmp/frame-eyecal-log-XXXXXX";
    close(mkstemp(p));
    std::string path = p;
    append(path, "old line\n");
    TrackerLog log(path);
    log.skip();
    CHECK(log.poll().empty());

    append(path, "one\ntw");
    auto a = log.poll();
    CHECK(a.size() == 1 && a[0] == "one");
    append(path, "o\nthree\n");
    auto b = log.poll();
    CHECK(b.size() == 2 && b[0] == "two" && b[1] == "three");

    FILE *f = fopen(path.c_str(), "w"); // rewritten from the start (the tracker restarted)
    CHECK(f != nullptr);
    if (!f) return;
    fputs("new\n", f);
    fclose(f);
    auto c = log.poll();
    CHECK(c.size() == 1 && c[0] == "new");
    unlink(path.c_str());
}

TEST(tracker_log_waits_for_the_file) {
    TrackerLog log("/nonexistent/eyetracking.txt");
    log.skip();
    CHECK(log.poll().empty());
}
