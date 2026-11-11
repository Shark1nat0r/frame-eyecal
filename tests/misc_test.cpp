// geometry, the line link between the processes, and the small helpers
#include <cmath>
#include <cstddef>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../src/common.h"
#include "../src/geometry.h"
#include "../src/link.h"
#include "../src/targets.h"
#include "check.h"

TEST(target_point_has_the_target_direction) {
    for (const Target &t : kTargets) {
        V3 p = target_point(t, 2);
        CHECK_NEAR(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), 2, 1e-9);
        CHECK_NEAR(yaw_of(p), t.yaw, 1e-9);
        CHECK_NEAR(pitch_of(p), t.pitch, 1e-9);
    }
}

static Quat yaw_quat(double deg) { // turn to the right by deg (about -y: x right, -z forward)
    double a = -deg * kDeg / 2;
    return {0, std::sin(a), 0, std::cos(a)};
}

TEST(gaze_error_signs) {
    double h = 0, v = 0;
    gaze_error({0, 0, 0, 1}, {0, 0, 0}, kTargets[0], 2, h, v); // straight ahead at the centre target
    CHECK_NEAR(h, 0, 1e-9);
    CHECK_NEAR(v, 0, 1e-9);
    gaze_error(yaw_quat(2), {0, 0, 0}, kTargets[0], 2, h, v); // looking 2° to the right of it
    CHECK_NEAR(h, 2, 1e-9);
    CHECK_NEAR(v, 0, 1e-9);
    gaze_error(yaw_quat(15), {0, 0, 0}, {15, 0}, 2, h, v); // at the right target
    CHECK_NEAR(h, 0, 1e-9);
    // the gaze origin sits beside the head centre (one eye): the target direction is taken from the eye
    gaze_error({0, 0, 0, 1}, {0.032, 0, 0}, kTargets[0], 2, h, v);
    CHECK_NEAR(h, std::atan2(0.032, 2) / kDeg, 1e-9);
}

TEST(qrot_conj_undoes_it) {
    Quat q = yaw_quat(33);
    V3 v{0.3, -1, 2}, w = qrot(conj(q), qrot(q, v));
    CHECK_NEAR(w.x, v.x, 1e-12);
    CHECK_NEAR(w.y, v.y, 1e-12);
    CHECK_NEAR(w.z, v.z, 1e-12);
}

TEST(link_sends_lines_and_sees_the_end) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Link a, b;
    a.fd = sv[0];
    b.fd = sv[1];
    fcntl(sv[1], F_SETFL, O_NONBLOCK);
    std::vector<std::string> got;
    CHECK(b.poll(got) && got.empty()); // nothing yet, still connected
    a.send("hz 90");
    a.send("result none");
    CHECK(b.poll(got));
    CHECK(got.size() == 2 && got[0] == "hz 90" && got[1] == "result none");
    close(sv[0]);
    got.clear();
    CHECK(!b.poll(got));
    close(sv[1]);
}

TEST(take_lines_keeps_the_unfinished_rest) {
    std::string buf = "a\n\nbc\nde";
    std::vector<std::string> out;
    take_lines(buf, out);
    CHECK(out.size() == 3 && out[0] == "a" && out[1].empty() && out[2] == "bc");
    CHECK(buf == "de");
}

TEST(next_cp_decodes_utf8) {
    std::string s = "a°×•";
    size_t i = 0;
    CHECK(next_cp(s, i) == 'a');
    CHECK(next_cp(s, i) == 0xB0);
    CHECK(next_cp(s, i) == 0xD7);
    CHECK(next_cp(s, i) == 0x2022);
    CHECK(i == s.size());
}
