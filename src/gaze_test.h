// Scoring the accuracy test from the gaze samples recorded during its looks.
#pragma once

#include <cstdint>
#include <vector>

#include "test_result.h"

struct Sample {
    int visit; // index into kTestOrder
    int64_t st; // the eye sample's own time (0 = unknown): repeated frames of one eye sample count once
    bool valid; // the tracker had a gaze
    double eh, ev; // gaze minus target, degrees (right, up)
};

// Per target: accuracy is the offset of the mean of its two looks, precision the spread within a look. Fills
// acc, prec, bh, bv, valid, worst, per (-1 = no data) and unsure; false if no target got any valid sample.
bool evaluate(const std::vector<Sample> &samples, TestResult &r);
