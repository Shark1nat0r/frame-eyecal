// The outcome of an accuracy test, its text form, and the results file.
#pragma once

#include <string>

struct TestResult {
    double when = 0, acc = 0, prec = 0, bh = 0, bv = 0, valid = 0, worst = 0, per[9] = {};
    int drop_l = 0, drop_r = 0, unsure = 0; // unsure: bit k = position k's two looks disagreed
    std::string app_ver, et_ver; // versions of this app and of the eye tracker the test ran with ("" = not recorded)
};

// The measures of a test as text, in the same form in the scene's "result" message and in results.txt
std::string measures_str(const TestResult &x);
const char *read_measures(const char *s, TestResult &x); // returns the text after them, nullptr if incomplete

// results.txt: the newest test per condition [inserts][calibrated], and the lens inserts setting.
// A missing or unreadable file leaves res and inserts as they are.
void load_results(const std::string &path, TestResult (&res)[2][2], bool &inserts);
bool save_results(const std::string &path, const TestResult (&res)[2][2], bool inserts);

// An accuracy as the screens show it, in degrees without the unit: "%.1f"
std::string acc_str(double acc);

// How much better after is than before, in degrees, from the accuracies rounded to 0.1° as the screens show them,
// so the difference always matches the two numbers next to it. > 0: better.
double shown_change(double before, double after);

// Which condition a test made now counts as, as an index inserts * 2 + calibrated; -1 if it fits no clean one.
// "Not calibrated" needs no accepted click at all since SteamVR started, "calibrated" a full calibration (kTrackerKeeps
// clicks) made with the current lenses.
int condition_slot(bool log_seen, int accepts_since_start, bool cal_stale, bool inserts);
