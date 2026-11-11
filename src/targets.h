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
// Calibration positions, in order: top left, top right, right, bottom right, bottom left, left, centre. As many as
// the tracker keeps (kTrackerKeeps, checked in ui.cpp), so afterwards all of its clicks come from the calibration.
// Top and bottom centre are only tested.
inline constexpr int kCalOrder[] = {1, 2, 6, 3, 4, 8, 0};
inline constexpr int kCalPositions = sizeof kCalOrder / sizeof kCalOrder[0];
// Test order: every position twice, the second round in another order. A position counts as unsure
// when its two looks are more than kUnsure degrees apart.
inline constexpr int kLooks = 18;
inline constexpr int kTestOrder[kLooks] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 6, 1, 7, 2, 8, 3, 5, 4, 0};
inline constexpr double kUnsure = 1.5;
