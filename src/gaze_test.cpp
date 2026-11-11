#include "gaze_test.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cstddef>

#include "targets.h"
#include "test_result.h"

namespace {
struct Look {
    double h, v, spread;
}; // one look at a target: mean offset (degrees) and spread around it
} // namespace

// Look v from its valid samples; false if it has none
static bool look_at(const std::vector<Sample> &samples, int v, Look &out) {
    std::vector<const Sample *> s;
    int64_t last = -1;
    for (const auto &x : samples) {
        if (x.visit != v || !x.valid) continue;
        if (x.st == 0 || x.st != last) s.push_back(&x); // skip repeats of the same eye sample
        last = x.st;
    }
    if (s.empty()) return false;
    const auto n = static_cast<double>(s.size());
    double mh = 0, mv = 0, ss = 0;
    for (const auto *x : s) {
        mh += x->eh;
        mv += x->ev;
    }
    mh /= n;
    mv /= n;
    for (const auto *x : s) ss += (x->eh - mh) * (x->eh - mh) + (x->ev - mv) * (x->ev - mv);
    out = {mh, mv, sqrt(ss / n)};
    return true;
}

bool evaluate(const std::vector<Sample> &samples, TestResult &r) {
    double acc_sum = 0, prec_sum = 0, bh = 0, bv = 0;
    int nt = 0;
    r.unsure = 0;
    r.worst = 0;
    for (int k = 0; k < 9; k++) {
        Look looks[2];
        int nv = 0;
        for (int v = 0; v < kLooks && nv < 2; v++)
            if (kTestOrder[v] == k && look_at(samples, v, looks[nv])) nv++;
        r.per[k] = -1;
        if (!nv) continue;
        const Look &a = looks[0], &b = looks[nv - 1]; // the same look twice if there is only one
        if (nv == 2 && hypot(a.h - b.h, a.v - b.v) > kUnsure) r.unsure |= 1 << k;
        const double mh = (a.h + b.h) / 2, mv = (a.v + b.v) / 2;
        r.per[k] = hypot(mh, mv);
        r.worst = std::max(r.worst, r.per[k]);
        acc_sum += r.per[k];
        prec_sum += (a.spread + b.spread) / 2;
        bh += mh;
        bv += mv;
        nt++;
    }
    if (!nt) return false;
    const auto valid = std::count_if(samples.begin(), samples.end(), [](const Sample &x) { return x.valid; });
    r.acc = acc_sum / nt;
    r.prec = prec_sum / nt;
    r.bh = bh / nt;
    r.bv = bv / nt;
    r.valid = 100.0 * static_cast<double>(valid) / static_cast<double>(std::max<size_t>(samples.size(), 1));
    return true;
}
