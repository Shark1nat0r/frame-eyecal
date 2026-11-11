#include <stdlib.h> // NOLINT(modernize-deprecated-headers): mkstemp is POSIX, declared only there
#include <cstdio>
#include <string>
#include <unistd.h>

#include "../src/test_result.h"
#include "check.h"

static std::string temp_path() {
    char p[] = "/tmp/frame-eyecal-test-XXXXXX";
    int fd = mkstemp(p);
    close(fd);
    return p;
}

static TestResult sample_result() {
    TestResult r;
    r.when = 1762870000;
    r.acc = 2.345;
    r.prec = 0.4;
    r.bh = -0.6;
    r.bv = 0.25;
    r.valid = 97.5;
    r.worst = 4.4;
    for (int k = 0; k < 9; k++) r.per[k] = k == 4 ? -1 : 0.5 * k;
    r.drop_l = 3;
    r.drop_r = 12;
    r.unsure = 1 << 3 | 1 << 7;
    r.app_ver = "v1.2";
    r.et_ver = "2.18.2";
    return r;
}

TEST(measures_round_trip) {
    TestResult a = sample_result(), b;
    std::string s = measures_str(a) + " 42";
    const char *rest = read_measures(s.c_str(), b);
    CHECK(rest != nullptr);
    if (!rest) return;
    CHECK(std::string(rest) == " 42");
    CHECK_NEAR(b.acc, a.acc, 1e-3);
    CHECK_NEAR(b.bh, a.bh, 1e-3);
    CHECK_NEAR(b.valid, a.valid, 0.05);
    CHECK_NEAR(b.worst, a.worst, 1e-3);
    for (int k = 0; k < 9; k++) CHECK_NEAR(b.per[k], a.per[k], 1e-3);
}

TEST(read_measures_rejects_incomplete) {
    TestResult x;
    CHECK(read_measures(" none", x) == nullptr); // the scene's "result none"
    CHECK(read_measures("1 2 3 4 5 6 7 8 9 10 11 12 13 14", x) == nullptr); // 14 of 15
    CHECK(read_measures("", x) == nullptr);
}

TEST(results_file_round_trip) {
    std::string path = temp_path();
    TestResult res[2][2];
    res[1][0] = sample_result();
    res[0][1] = sample_result();
    res[0][1].app_ver.clear(); // not recorded: no app= field
    res[0][1].unsure = 0;
    CHECK(save_results(path, res, true));

    TestResult got[2][2];
    bool inserts = false;
    load_results(path, got, inserts);
    CHECK(inserts);
    CHECK(got[0][0].when == 0); // no test in this condition
    CHECK(got[1][1].when == 0);
    CHECK(got[1][0].when == res[1][0].when);
    CHECK_NEAR(got[1][0].acc, res[1][0].acc, 1e-3);
    CHECK(got[1][0].drop_l == 3 && got[1][0].drop_r == 12);
    CHECK(got[1][0].unsure == res[1][0].unsure);
    CHECK(got[1][0].app_ver == "v1.2" && got[1][0].et_ver == "2.18.2");
    CHECK(got[0][1].app_ver.empty() && got[0][1].et_ver == "2.18.2" && got[0][1].unsure == 0);
    unlink(path.c_str());
}

TEST(results_file_reads_old_lines_and_skips_bad_ones) {
    std::string path = temp_path();
    FILE *f = fopen(path.c_str(), "w");
    CHECK(f != nullptr);
    if (!f) return;
    fputs("inserts 0\n", f);
    // a line from before app=, et= and unsure= existed
    fputs("0 1 1700000000 1.500 0.300 0.100 -0.200 99.0 2.000 1 1 1 1 1 1 1 1 2 0 4\n", f);
    fputs("2 0 1700000000 1.500 0.300 0.100 -0.200 99.0 2.000 1 1 1 1 1 1 1 1 2 0 4\n", f); // no such condition
    fputs("0 0 1700000000 1.500 0.300\n", f); // cut off
    fputs("garbage\n", f);
    fclose(f);
    TestResult got[2][2];
    bool inserts = true;
    load_results(path, got, inserts);
    CHECK(!inserts);
    CHECK_NEAR(got[0][1].acc, 1.5, 1e-9);
    CHECK_NEAR(got[0][1].per[8], 2, 1e-9);
    CHECK(got[0][1].drop_r == 4 && got[0][1].unsure == 0 && got[0][1].app_ver.empty());
    CHECK(got[0][0].when == 0 && got[1][0].when == 0 && got[1][1].when == 0);
    unlink(path.c_str());
}

TEST(load_results_without_file_keeps_defaults) {
    TestResult got[2][2];
    bool inserts = true;
    load_results("/nonexistent/frame-eyecal/results.txt", got, inserts);
    CHECK(inserts && got[0][0].when == 0);
}

TEST(condition_slot_rules) {
    CHECK(condition_slot(false, 0, false, false) == -1); // no tracker log: unknown
    CHECK(condition_slot(true, 0, false, false) == 0); // not calibrated, no inserts
    CHECK(condition_slot(true, 0, false, true) == 2); // not calibrated, inserts
    CHECK(condition_slot(true, 3, false, false) == -1); // partly calibrated
    CHECK(condition_slot(true, 8, false, false) == 1); // calibrated
    CHECK(condition_slot(true, 12, false, true) == 3); // more than 8 clicks: still calibrated
    CHECK(condition_slot(true, 8, true, true) == -1); // calibrated with the other lenses
}
