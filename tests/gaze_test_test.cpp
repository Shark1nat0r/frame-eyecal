#include <cstdint>

#include <cmath>
#include <vector>

#include "../src/gaze_test.h"
#include "../src/targets.h"
#include "../src/test_result.h"
#include "check.h"

// n valid samples per look, each with its own eye-sample time, at the offset off(target, look)
template <class F> static std::vector<Sample> looks(int n, F off) {
    std::vector<Sample> s;
    int64_t st = 1;
    int seen[9] = {};
    for (int v = 0; v < kLooks; v++) {
        int k = kTestOrder[v], look = seen[k]++;
        for (int i = 0; i < n; i++) {
            double h = 0, vv = 0;
            off(k, look, i, h, vv);
            s.push_back({v, st++, true, h, vv});
        }
    }
    return s;
}

TEST(evaluate_constant_offset) {
    auto s = looks(10, [](int, int, int, double &h, double &v) {
        h = 1;
        v = -0.5;
    });
    TestResult r;
    CHECK(evaluate(s, r));
    CHECK_NEAR(r.acc, std::hypot(1, 0.5), 1e-9);
    CHECK_NEAR(r.bh, 1, 1e-9);
    CHECK_NEAR(r.bv, -0.5, 1e-9);
    CHECK_NEAR(r.prec, 0, 1e-9);
    CHECK_NEAR(r.valid, 100, 1e-9);
    CHECK(r.unsure == 0);
    for (double p : r.per) CHECK_NEAR(p, std::hypot(1, 0.5), 1e-9);
}

TEST(evaluate_precision_is_spread_within_a_look) {
    auto s = looks(10, [](int, int, int i, double &h, double &v) {
        h = i % 2 ? 0.5 : -0.5;
        v = 0;
    });
    TestResult r;
    CHECK(evaluate(s, r));
    CHECK_NEAR(r.acc, 0, 1e-9);
    CHECK_NEAR(r.prec, 0.5, 1e-9);
}

TEST(evaluate_marks_targets_whose_looks_disagree) {
    // target 3: first look 0°, second 2° off (more than kUnsure apart); its error is the mean, 1°
    auto s = looks(5, [](int k, int look, int, double &h, double &v) {
        h = k == 3 && look == 1 ? 2 : 0;
        v = 0;
    });
    TestResult r;
    CHECK(evaluate(s, r));
    CHECK(r.unsure == 1 << 3);
    CHECK_NEAR(r.per[3], 1, 1e-9);
    CHECK_NEAR(r.worst, 1, 1e-9);
    CHECK_NEAR(r.acc, 1.0 / 9, 1e-9);
}

TEST(evaluate_counts_a_repeated_eye_sample_once) {
    auto s = looks(4, [](int, int, int, double &h, double &v) {
        h = 1;
        v = 0;
    });
    s.push_back(s.back()); // the same eye sample in the next frame ...
    s.back().eh = 50; // ... (its value would show if counted)
    TestResult r;
    CHECK(evaluate(s, r));
    CHECK_NEAR(r.acc, 1, 1e-9);
}

TEST(evaluate_skips_invalid_samples_and_missing_targets) {
    auto s = looks(9, [](int, int, int, double &h, double &v) {
        h = 0;
        v = 2;
    });
    std::vector<Sample> kept;
    for (const auto &x : s)
        if (kTestOrder[x.visit] != 8) kept.push_back(x); // no data for target 8
    for (int i = 0; i < 16; i++) kept.push_back({0, 0, false, 99, 99}); // tracker had no gaze
    TestResult r;
    CHECK(evaluate(kept, r));
    CHECK(r.per[8] == -1);
    CHECK_NEAR(r.acc, 2, 1e-9);
    CHECK_NEAR(r.valid, 100.0 * 144 / 160, 1e-9);
}

TEST(evaluate_without_gaze_fails) {
    TestResult r;
    CHECK(!evaluate({}, r));
    CHECK(!evaluate({{0, 1, false, 0, 0}, {1, 2, false, 0, 0}}, r));
}
