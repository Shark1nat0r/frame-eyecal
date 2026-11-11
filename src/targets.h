// Where the targets are and in which order they are used.
#pragma once

#include <cmath>

inline constexpr double kDeg = M_PI / 180.0;

// The 9 positions, degrees (yaw + right, pitch + up).
struct Target {
    double yaw, pitch;
};
inline constexpr Target kTargets[9] = {{0, 0},  {-15, 10}, {15, 10}, {15, -10}, {-15, -10},
                                       {0, 10}, {15, 0},   {0, -10}, {-15, 0}};
// Calibration order: the tracker keeps the last 8, so the first one (a corner) is the one that falls out.
inline constexpr int kCalOrder[9] = {1, 5, 2, 6, 3, 7, 4, 8, 0};
// Test order: every position twice, the second round in another order. A position counts as unsure
// when its two looks are more than kUnsure degrees apart.
inline constexpr int kLooks = 18;
inline constexpr int kTestOrder[kLooks] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 6, 1, 7, 2, 8, 3, 5, 4, 0};
inline constexpr double kUnsure = 1.5;
