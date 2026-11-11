#include "test_result.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common.h"
#include "tracker_log.h"

std::string measures_str(const TestResult &x) {
    std::string s = fmt("%.3f %.3f %.3f %.3f %.1f %.3f", x.acc, x.prec, x.bh, x.bv, x.valid, x.worst);
    for (double v : x.per) s += fmt(" %.3f", v);
    return s;
}

const char *read_measures(const char *s, TestResult &x) {
    double *v[15] = {&x.acc, &x.prec, &x.bh, &x.bv, &x.valid, &x.worst};
    for (int k = 0; k < 9; k++) v[6 + k] = &x.per[k];
    for (double *p : v) {
        char *e = nullptr;
        *p = strtod(s, &e);
        if (e == s) return nullptr;
        s = e;
    }
    return s;
}

void load_results(const std::string &path, TestResult (&res)[2][2], bool &inserts) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        int i = 0, c = 0, ins = 0, n = 0;
        TestResult x;
        if (sscanf(line, "inserts %d", &ins) == 1) {
            inserts = ins;
            continue;
        }
        if (sscanf(line, "%d %d %lf%n", &i, &c, &x.when, &n) != 3 || i < 0 || i > 1 || c < 0 || c > 1) continue;
        const char *rest = read_measures(line + n, x);
        if (rest && sscanf(rest, "%d %d", &x.drop_l, &x.drop_r) == 2) {
            char v[64];
            if (const char *p = strstr(line, " app="); p && (sscanf(p, " app=%63s", v) == 1)) x.app_ver = v;
            if (const char *p = strstr(line, " et="); p && (sscanf(p, " et=%63s", v) == 1)) x.et_ver = v;
            if (const char *p = strstr(line, " unsure=")) sscanf(p, " unsure=%d", &x.unsure);
            res[i == 1][c == 1] = x; // i and c are 0 or 1 (checked above)
        }
    }
    fclose(f);
}

bool save_results(const std::string &path, const TestResult (&res)[2][2], bool inserts) {
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "inserts %d\n", inserts);
    for (int i = 0; i < 2; i++)
        for (int c = 0; c < 2; c++) {
            const TestResult &x = res[i][c];
            if (x.when == 0) continue;
            fprintf(f, "%d %d %.0f %s %d %d", i, c, x.when, measures_str(x).c_str(), x.drop_l, x.drop_r);
            if (!x.app_ver.empty()) fprintf(f, " app=%s", x.app_ver.c_str());
            if (!x.et_ver.empty()) fprintf(f, " et=%s", x.et_ver.c_str());
            if (x.unsure) fprintf(f, " unsure=%d", x.unsure);
            fprintf(f, "\n");
        }
    return fclose(f) == 0;
}

std::string acc_str(double acc) { return fmt("%.1f", acc); }

double shown_change(double before, double after) {
    auto shown = [](double v) { return std::lround(atof(acc_str(v).c_str()) * 10); }; // in tenths, as printed
    return static_cast<double>(shown(before) - shown(after)) / 10;
}

int condition_slot(bool log_seen, int accepts_since_start, bool cal_stale, bool inserts) {
    if (!log_seen) return -1;
    if (accepts_since_start == 0) return inserts * 2;
    if (accepts_since_start >= kTrackerKeeps && !cal_stale) return inserts * 2 + 1;
    return -1;
}
