// The eye tracker's log (~/.local/share/Steam/logs/eyetracking.txt): following it, and what its lines mean.
#pragma once

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

struct TrackerLog {
    std::string path;
    FILE *f = nullptr;
    long pos = 0;
    std::string part;
    TrackerLog() = default;
    explicit TrackerLog(std::string p) : path(std::move(p)) {}
    TrackerLog(const TrackerLog &) = delete;
    TrackerLog &operator=(const TrackerLog &) = delete;
    TrackerLog(TrackerLog &&) = delete;
    TrackerLog &operator=(TrackerLog &&) = delete;
    ~TrackerLog() {
        if (f) fclose(f);
    }
    bool open();
    void skip(); // ignore what is already there
    std::vector<std::string> poll(); // the lines written since the last call
};

// The eye tracker keeps the last 7 accepted calibration clicks per eye (SteamVR 2.18.2).
inline constexpr int kTrackerKeeps = 7;

// The tracker's kept clicks, oldest first, as far as an app can follow them: adds an accepted click (a target
// index, or -1 for one that was not on a calibration target) and drops the oldest beyond kTrackerKeeps.
// Returns the target it dropped, -1 if none.
inline int keep_click(std::vector<int> &kept, int target) {
    kept.push_back(target);
    if (kept.size() <= static_cast<size_t>(kTrackerKeeps)) return -1;
    const int dropped = kept.front();
    kept.erase(kept.begin());
    return dropped;
}

// Why the tracker rejected a calibration click
enum class Reject { NONE, SLOW, EYES, POINTER, OTHER };

// What one log line says; most lines say nothing of interest
struct TrackerLine {
    bool startup = false; // the tracker (re)started: calibration is gone
    std::string version; // its version, from the startup line
    double fps = -1; // the eye cameras switched to this rate
    bool accept = false; // a calibration click was accepted ...
    double lf = 1, rf = 1; // ... with this fixation per eye (cosine of the eye movement during the click)
    Reject reject = Reject::NONE;
    bool drop_l = false, drop_r = false; // an eye was lost for a moment
};
TrackerLine parse_tracker_line(const std::string &l);
