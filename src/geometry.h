// Vectors in OpenXR's view space: x right, y up, -z forward.
#pragma once

#include <cmath>

#include "targets.h"

struct V3 {
    double x, y, z;
};
struct Quat {
    double x, y, z, w;
};

inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

inline V3 qrot(Quat q, V3 v) { // v rotated by the unit quaternion q
    V3 u{q.x, q.y, q.z};
    double s = q.w, duv = u.x * v.x + u.y * v.y + u.z * v.z, duu = u.x * u.x + u.y * u.y + u.z * u.z;
    V3 c{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    return {2 * duv * u.x + (s * s - duu) * v.x + 2 * s * c.x, 2 * duv * u.y + (s * s - duu) * v.y + 2 * s * c.y,
            2 * duv * u.z + (s * s - duu) * v.z + 2 * s * c.z};
}
inline Quat conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline V3 target_point(const Target &t, double d) { // the target at distance d from the origin
    return {d * sin(t.yaw * kDeg) * cos(t.pitch * kDeg), d * sin(t.pitch * kDeg),
            -d * cos(t.yaw * kDeg) * cos(t.pitch * kDeg)};
}
inline double yaw_of(V3 d) { return atan2(d.x, -d.z) / kDeg; }
inline double pitch_of(V3 d) { return atan2(d.y, sqrt(d.x * d.x + d.z * d.z)) / kDeg; }

// How far a gaze ray (from origin, oriented by q, looking along -z) points beside the target at distance d,
// in degrees: h > 0 right of it, v > 0 above it.
inline void gaze_error(Quat q, V3 origin, const Target &t, double d, double &h, double &v) {
    V3 g = qrot(q, {0, 0, -1}), to = target_point(t, d) - origin;
    h = yaw_of(g) - yaw_of(to);
    v = pitch_of(g) - pitch_of(to);
}
