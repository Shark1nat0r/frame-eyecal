// What the panel's screens look like.
#include <algorithm>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "canvas.h"
#include "common.h"
#include "targets.h"
#include "test_result.h"
#include "tracker_log.h"
#include "ui.h"

static const char *kCond[2][2] = {{"No inserts, not calibrated", "No inserts, calibrated"},
                                  {"Inserts, not calibrated", "Inserts, calibrated"}};
static const int kMargin = 120; // text column of the reading screens: kMargin .. kTex - kMargin
static const char kLooksDiffered[] = "Amber frame: your two looks differed. Test again.";

static const char *rating(double acc) {
    if (acc < 1.5) return "Excellent";
    if (acc < 2.5) return "Good";
    if (acc < 4) return "Fair";
    return "Poor";
}
static int band(double acc) { // 0 good, 1 fair, 2 poor
    if (acc < 2.5) return 0;
    if (acc < 4) return 1;
    return 2;
}
static Col rating_col(double acc) {
    const Col c[3] = {kGood, kWarn, kBad};
    return c[band(acc)];
}
static std::string eyes_lost(int l, int r) { // the tracker's per-eye dropouts, in words
    if (!l && !r) return "never";
    if (!l || !r) return fmt("%s eye %d×", l ? "left" : "right", l ? l : r);
    return fmt("left eye %d×, right eye %d×", l, r);
}
static std::string change(double d) { return acc_str(fabs(d)) + "° " + (d >= 0 ? "better" : "worse"); } // d > 0: better
static std::string join(const std::vector<std::string> &v) { // a, b and c
    std::string s;
    for (size_t j = 0; j < v.size(); j++) {
        if (j > 0) s += j + 1 == v.size() ? " and " : ", ";
        s += v[j];
    }
    return s;
}

// 3x3 errors, laid out like the targets; text size s
static void grid(Canvas &c, const double *per, int unsure, int x0, int y0, int w, int hgt, int s) {
    const Col band_bg[3] = {{60, 110, 70}, {125, 105, 55}, {125, 65, 60}};
    for (int k = 0; k < 9; k++) {
        const int col = 1 + static_cast<int>(kTargets[k].yaw / 15), row = 1 - static_cast<int>(kTargets[k].pitch / 10);
        const int cx0 = x0 + col * (w + 6), cy0 = y0 + row * (hgt + 6);
        const double v = per[k];
        const Col bgc = v < 0 ? Col{70, 70, 74} : band_bg[band(v)];
        c.fill(cx0, cy0, cx0 + w, cy0 + hgt, bgc);
        if (unsure >> k & 1) { // the two looks at this position disagreed: amber frame
            c.fill(cx0, cy0, cx0 + w, cy0 + hgt, kWarn);
            c.fill(cx0 + 4, cy0 + 4, cx0 + w - 4, cy0 + hgt - 4, bgc);
        }
        c.text(v < 0 ? "-" : fmt("%.1f°", v), cx0 + w / 2, cy0 + hgt / 2 - 7 * s / 2 - 1, s, kText);
    }
}

void Ui::btn(int y, const std::string &id, const std::string &label, int x0, int x1, Kind k) {
    buttons.push_back({x0, y, x1, y + 76, id, label, k});
}

void Ui::two(const char *id1, const char *l1, Kind k1, const char *id2, const char *l2, Kind k2, int y) {
    btn(y, id1, l1, kMargin, kTex / 2 - 20, k1);
    btn(y, id2, l2, kTex / 2 + 20, kTex - kMargin, k2);
}

void Ui::lens(Canvas &c, int y, int x0, int x1) { // the lens inserts switch: label left, No | Yes right
    c.text("Lens inserts", x0, y + 27, 3, kText, false);
    btn(y, "ins_no", "No", x1 - 240, x1 - 120, inserts ? Kind::QUIET : Kind::SELECTED);
    btn(y, "ins_yes", "Yes", x1 - 120, x1, inserts ? Kind::SELECTED : Kind::QUIET);
}

void Ui::saved_as(Canvas &c, int y, int x) const { // what a test made now counts as
    const int s = slot_now();
    if (s >= 0) c.text("Saved as: " + std::string(kCond[s / 2][s % 2]), x, y, 3, kDim, false);
    else if (log_seen && pre_accepts == accepts_since_start)
        c.para("Not saved: a click before the app opened counted as calibration. Restart SteamVR and open the "
               "app with slow clicks, or calibrate.",
               y, 3, kWarn, x);
    else if (log_seen)
        c.para("Not saved: partly calibrated. Restart SteamVR for a not-calibrated test, or calibrate.", y, 3, kWarn,
               x);
}

Canvas Ui::draw(bool share) {
    Canvas c;
    const Col bg{40, 40, 43};
    c.fill(0, 0, kTex, kTex, bg, 240);
    if (flash_on) {
        const int w = 10;
        c.fill(0, 0, kTex, w, flash_col);
        c.fill(0, kTex - w, kTex, kTex, flash_col);
        c.fill(0, 0, w, kTex, flash_col);
        c.fill(kTex - w, 0, kTex, kTex, flash_col);
    }
    buttons.clear();
    switch (screen) {
    case Screen::MENU: draw_menu(c); break;
    case Screen::CAL_INTRO:
    case Screen::TEST_INTRO: draw_intro(c); break;
    case Screen::CAL: draw_cal(c); break;
    case Screen::CAL_DONE: draw_cal_done(c); break;
    case Screen::TEST_DONE: draw_test_done(c); break;
    case Screen::COMPARE: draw_compare(c, share); break;
    case Screen::HELP: draw_help(c); break;
    case Screen::TERMS: draw_terms(c); break;
    case Screen::MESSAGE: draw_message(c); break;
    case Screen::TESTING: break; // the panel is hidden
    }
    for (const auto &b : buttons) c.button(b, b.id == hover);
    return c;
}

Ui::MenuStatus Ui::menu_status() const { // the status, and the next step of the comparison
    if (!log_seen) return {"Status unknown", "Eye tracker log not found", "", kDim};
    if (accepts_since_start == 0) {
        if (res[inserts][0].when == 0)
            return {"Not calibrated", "Test now to measure it without calibration", "test", kText};
        return {"Not calibrated", "Calibrate, then test again", "cal", kText};
    }
    if (cal_stale) return {"Calibrated with other lenses", "Calibrate again", "cal", kWarn};
    if (slot_now() < 0)
        return {fmt("Partly calibrated: %d of %d clicks", accepts_since_start, kTrackerKeeps),
                pre_accepts == accepts_since_start ? "Clicks before the app opened counted. See Help."
                                                   : fmt("Calibrate to fill all %d", kTrackerKeeps),
                "cal", kWarn};
    if (res[inserts][1].when == 0) return {"Calibrated", "Test now to see what calibration changed", "test", kGood};
    return {"Calibrated",
            res[1 - inserts][0].when == 0 ? "Lens inserts? Switch them, restart SteamVR"
                                          : "Lasts until SteamVR restarts",
            "compare", kGood};
}

void Ui::draw_menu(Canvas &c) {
    c.text("Frame-EyeCal", kTex / 2, 60, 6, kText);
    const MenuStatus m = menu_status();
    c.text(m.status, kTex / 2, 160, 4, m.col);
    c.text(m.next, kTex / 2, 225, 3, kDim);
    lens(c, 290, 262, 762);
    auto k = [&](const char *id) { return m.primary == id ? Kind::PRIMARY : Kind::NORMAL; };
    btn(420, "test", "Test accuracy", 262, 762, k("test"));
    btn(510, "cal", "Calibrate", 262, 762, k("cal"));
    btn(600, "compare", "Compare results", 262, 762, k("compare"));
    btn(740, "help", "Help", 262, 502, Kind::QUIET);
    btn(740, "exit", "Exit", 522, 762, Kind::QUIET);
    c.text(fmt("Display %d Hz", display_hz), 70, kTex - 70, 3, kDim, false);
    c.text(fmt("Eye cameras %.0f fps", cam_fps), kTex / 2, kTex - 70, 3, cam_fps >= 60 ? kDim : kWarn);
    c.text(version, kTex - 70 - Canvas::width(version, 3), kTex - 70, 3, kDim, false);
}

void Ui::draw_intro(Canvas &c) { // before calibration or a test
    const bool cal = screen == Screen::CAL_INTRO;
    c.text(cal ? "Calibration" : "Accuracy Test", kTex / 2, 60, 6, kText);
    int y = 200;
    if (cal) {
        y = c.para("1.  Point the laser at the yellow square", y, 4, kText, kMargin);
        y = c.para("2.  Look at the laser dot", y + 14, 4, kText, kMargin);
        y = c.para("3.  Tap the trigger, keep still", y + 14, 4, kText, kMargin);
        c.para("About a minute. The panel moves with your head.", y + 30, 3, kDim, kMargin);
    } else {
        y = c.para("Look at the dot in the square. It turns green while it measures, then moves on.", y, 4, kText,
                   kMargin);
        c.para("9 points, each twice, about 45 seconds, no clicking. The panel hides until the test is done.", y + 30,
               3, kDim, kMargin);
    }
    // the setting that labels the results, where it matters
    c.fill(kMargin, 490, kTex - kMargin, 492, {70, 70, 74});
    lens(c, 515, kMargin, kTex - kMargin);
    if (!cal) saved_as(c, 625, kMargin);
    else if (accepts_since_start > 0 && log_seen)
        c.para(fmt("Replaces the %d click%s the tracker has now.", std::min(accepts_since_start, kTrackerKeeps),
                   accepts_since_start == 1 ? "" : "s"),
               625, 3, kDim, kMargin);
    two(cal ? "cal_start" : "test_start", "Start", Kind::PRIMARY, "menu", "Menu", Kind::NORMAL);
}

void Ui::draw_cal(Canvas &c) {
    for (int i = 0; i < kCalPositions; i++) { // progress, in calibration order
        const int x = kTex / 2 + (2 * i - (kCalPositions - 1)) * 20 - 14;
        c.fill(x, 30, x + 28, 46, kept(kCalOrder[i]) ? kGood : Col{80, 80, 84});
    }
    c.text(msg, kTex / 2, 76, 3, msg_col);
    if (cam_fps > 0 && cam_fps < 30)
        c.text(fmt("Eye cameras at %.0f fps, clicks will fail", cam_fps), kTex / 2, 120, 3, kWarn);
    auto [tpx, tpy] = cal_target_px();
    const int tx = static_cast<int>(lround(tpx)), ty = static_cast<int>(lround(tpy));
    c.fill(tx - 13, ty - 13, tx + 13, ty + 13, kYellow);
    c.fill(tx - 3, ty - 3, tx + 3, ty + 3, {0, 0, 0});
    btn(kTex - 96, "menu", "Menu", 24, 224, Kind::QUIET);
}

void Ui::draw_cal_done(Canvas &c) {
    c.text("Calibrated", kTex / 2, 60, 6, kGood);
    c.text(fmt("%d points in %d clicks, %.0f seconds", kCalPositions, attempts, now_s() - cal_start), kTex / 2, 180, 4,
           kText);
    std::string rej;
    auto add = [&](int n, const char *what) {
        if (n) rej += (rej.empty() ? "" : ", ") + fmt("%d %s", n, what);
    };
    add(n_eyes, "eyes moved");
    add(n_slow, "too slow");
    add(n_pointer, "hand moved");
    add(n_other, "other");
    c.text(rej.empty() ? "No clicks rejected" : "Rejected: " + rej, kTex / 2, 260, 3, kDim);
    c.text(fmt("Eye movement during clicks: left %.1f°, right %.1f° (limit 5°)", sum_l / std::max(accepted, 1),
               sum_r / std::max(accepted, 1)),
           kTex / 2, 305, 3, kDim);
    c.text("The tracker uses these clicks until SteamVR restarts.", kTex / 2, 420, 3, kText);
    c.text("Test now to see what calibration changed.", kTex / 2, 462, 3, kText);
    two("test_start", "Test accuracy", Kind::PRIMARY, "menu", "Menu", Kind::NORMAL);
}

void Ui::draw_test_done(Canvas &c) {
    c.text("Accuracy " + acc_str(last.acc) + "°", kTex / 2, 50, 6, rating_col(last.acc));
    c.text(rating(last.acc), kTex / 2, 120, 4, rating_col(last.acc));
    grid(c, last.per, last.unsure, kTex / 2 - 3 * 156 / 2 - 6, 180, 156, 64, 3);
    c.text(last.unsure ? kLooksDiffered : "Error per direction, lower is better", kTex / 2, 400, 3,
           last.unsure ? kWarn : kDim);
    int y = 455;
    auto row = [&](const char *label, const std::string &v, Col vc) {
        c.text(label, 200, y, 3, kDim, false);
        c.text(v, 470, y, 3, vc, false);
        y += 38;
    };
    row("Jitter", fmt("%.1f°", last.prec), kText);
    row("Shift",
        fmt("%.1f° %s, %.1f° %s", fabs(last.bh), last.bh >= 0 ? "right" : "left", fabs(last.bv),
            last.bv >= 0 ? "up" : "down"),
        kText);
    row("Eyes tracked", fmt("%.0f%% of the time", last.valid), kText);
    row("Lost track", eyes_lost(last.drop_l, last.drop_r), last.drop_l + last.drop_r ? kWarn : kText);
    y += 12;
    c.fill(150, y, kTex - 150, y + 2, {80, 80, 84});
    y += 26;
    bool next_cal = true;
    if (last_slot >= 0) {
        const int ins = last_slot / 2;
        c.text("Saved as: " + std::string(kCond[ins][last_slot % 2]), kTex / 2, y, 3, kText);
        next_cal = last_slot % 2 == 0;
        if (res[ins][1 - last_slot % 2].when > 0) {
            const double d = shown_change(res[ins][0].acc, res[ins][1].acc);
            c.text("Calibration: " + change(d), kTex / 2, y + 42, 3, d >= 0 ? kGood : kWarn);
        } else if (next_cal) {
            c.text("Next: calibrate, then test again", kTex / 2, y + 42, 3, kText);
        }
    } else {
        const char *why = "Not saved: calibrated with the other lenses.";
        if (!log_seen) why = "Not saved: the eye tracker's log was not found.";
        else if (accepts_since_start > 0 && accepts_since_start < kTrackerKeeps)
            why = "Not saved: partly calibrated. Restart SteamVR or calibrate.";
        c.text(why, kTex / 2, y, 3, kWarn);
    }
    const int by = kTex - 200;
    btn(by, "test_start", "Test again", 40, 250);
    btn(by, "cal", "Calibrate", 262, 472, next_cal ? Kind::PRIMARY : Kind::NORMAL);
    btn(by, "compare", "Compare results", 484, 772, next_cal ? Kind::NORMAL : Kind::PRIMARY);
    btn(by, "menu", "Menu", 784, 984);
}

// The comparison table: one column per test (no inserts | inserts, each not calibrated | calibrated), one row per
// measure.
static const int kColX0 = 196, kColW = 190, kGroupGap = 20;
static const int kRowY[4] = {452, 496, 540, 584}; // Jitter, Shift, Eyes tracked, Lost track
static int col_x(int j) { return kColX0 + j * kColW + (j / 2) * kGroupGap + kColW / 2; }
static void label(Canvas &c, const char *t, int y) { c.text(t, 40, y, 3, kDim, false); }

// One column of the comparison; false if it has no test
static bool compare_column(Canvas &c, int j, const TestResult &x, bool share) {
    c.text(j % 2 ? "Calibrated" : "Not calibrated", col_x(j), 162, 3, kDim);
    if (x.when == 0) { // what fills this column
        static const char *const how[4][2] = {{"Restart SteamVR,", "then test"},
                                              {"Calibrate,", "then test"},
                                              {"Only if you use", "lens inserts"},
                                              {"Only if you use", "lens inserts"}};
        c.fill(col_x(j) - 90, 282, col_x(j) + 90, 426, {50, 50, 54});
        c.text("No test yet", col_x(j), share ? 340 : 312, 3, kDim);
        if (!share) {
            c.text(how[j][0], col_x(j), 358, 2, kDim);
            c.text(how[j][1], col_x(j), 386, 2, kDim);
        }
        c.text("-", col_x(j), 222, 3, kDim);
        for (const int r : kRowY) c.text("-", col_x(j), r, 3, kDim);
        return false;
    }
    c.text(acc_str(x.acc) + "°", col_x(j), 206, 5, rating_col(x.acc));
    grid(c, x.per, x.unsure, col_x(j) - 90, 282, 56, 44, 2);
    const int lost = x.drop_l + x.drop_r;
    c.text(fmt("%.1f°", x.prec), col_x(j), kRowY[0], 3, kText);
    c.text(fmt("%.1f°", hypot(x.bh, x.bv)), col_x(j), kRowY[1], 3, kText);
    c.text(fmt("%.0f%%", x.valid), col_x(j), kRowY[2], 3, x.valid < 95 ? kWarn : kText);
    c.text(lost ? fmt("%d×", lost) : "never", col_x(j), kRowY[3], 3, lost ? kWarn : kText);
    return true;
}

void Ui::draw_compare(Canvas &c, bool share) {
    c.text(share ? "Steam Frame eye tracking" : "Comparison", kTex / 2, 24, 5, kText);
    auto at = [&](int j) -> const TestResult & { return res[j / 2][j % 2]; };
    for (int i = 0; i < 2; i++) {
        const int g0 = kColX0 + i * (2 * kColW + kGroupGap);
        c.text(i ? "Inserts" : "No inserts", g0 + kColW, 96, 4, kText);
        c.fill(g0 + 8, 146, g0 + 2 * kColW - 8, 148, {70, 70, 74});
    }
    label(c, "Accuracy", 222);
    label(c, "Directions", 340);
    const char *rows[4] = {"Jitter", "Shift", "Eyes tracked", "Lost track"};
    for (int r = 0; r < 4; r++) label(c, rows[r], kRowY[r]);
    bool any = false, unsure = false;
    for (int j = 0; j < 4; j++) {
        any |= compare_column(c, j, at(j), share);
        unsure |= at(j).when > 0 && at(j).unsure;
    }
    // what calibrating and inserts change in accuracy, in the column they describe
    c.fill(40, 632, kTex - 40, 634, {70, 70, 74});
    label(c, "Calibrating", 652);
    label(c, "Inserts", 696);
    auto delta = [&](int j, int y, const TestResult &base) {
        if (at(j).when == 0 || base.when == 0) {
            c.text("-", col_x(j), y, 3, kDim);
            return;
        }
        const double d = shown_change(base.acc, at(j).acc); // > 0: better
        c.text(change(d), col_x(j), y, 3, d >= 0 ? kGood : kWarn);
    };
    delta(1, 652, at(0));
    delta(3, 652, at(2));
    delta(2, 696, at(0));
    delta(3, 696, at(1));
    c.text(any ? "Lower is better" : "Run a test to fill this in", kTex / 2, 762, 3, kDim);
    if (unsure) c.text(kLooksDiffered, kTex / 2, 804, 3, kWarn);
    if (share) compare_versions(c);
    else compare_buttons(c, any);
}

void Ui::compare_versions(Canvas &c) const { // what the numbers were made with, so that shared results compare fairly
    auto list = [&](std::string TestResult::*m) {
        std::vector<std::string> v;
        for (const auto &row : res)
            for (const auto &x : row) {
                if (x.when == 0) continue;
                std::string s = (x.*m).empty() ? "unknown" : x.*m;
                if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
            }
        return v;
    };
    auto app = list(&TestResult::app_ver), et = list(&TestResult::et_ver);
    const bool mixed = app.size() > 1 || et.size() > 1;
    // et_ver is the eye tracker's version, which is the SteamVR version it ships with: the name users know
    c.text((mixed ? "Mixed versions: Frame-EyeCal " : "Frame-EyeCal ") + join(app) + ", SteamVR " + join(et), kTex / 2,
           kTex - 124, 3, mixed ? kWarn : kDim);
    char d[32];
    const time_t now = time(nullptr);
    strftime(d, sizeof d, "%Y-%m-%d", localtime(&now));
    c.text(d, kTex / 2, kTex - 80, 3, kDim);
}

void Ui::compare_buttons(Canvas &c, bool any) {
    const bool armed = clear_armed > 0;
    if (any) {
        btn(kTex - 140, "clear", armed ? "Really clear?" : "Clear results", 72, 352,
            armed ? Kind::DANGER : Kind::QUIET);
        btn(kTex - 140, "share", "Save image", 372, 652);
    }
    btn(kTex - 140, "menu", "Menu", any ? 672 : 332, any ? 952 : 692);
    if (!saved_image.empty()) c.text(saved_image, kTex / 2, kTex - 38, 3, saved_ok ? kGood : kBad);
}

void Ui::draw_help(Canvas &c) {
    c.text("Help", kTex / 2, 40, 5, kText);
    c.text("To compare", 70, 130, 4, kText, false);
    int y = 190;
    for (const char *p : {"1.  Restart SteamVR, open this app with slow clicks", "2.  Test accuracy",
                          "3.  Calibrate, then test again", "4.  With lens inserts: switch Lens inserts and repeat"})
        y = c.para(p, y, 3, kText, 70) + 6;
    y = c.para("Slow click: hold the trigger half a second. It never calibrates.", y + 4, 3, kDim, 70);
    y = c.para("Results depend on fit: reseating the headset changes them.", y + 4, 3, kDim, 70);
    c.text("How calibration works", 70, y + 40, 4, kText, false);
    y += 100;
    for (const std::string &p : {std::string("Short laser clicks on SteamVR menus calibrate the tracker"),
                                 fmt("It keeps the last %d until SteamVR restarts", kTrackerKeeps),
                                 std::string("Most fail when no app uses eye tracking (reduced fps)")}) {
        c.text("•", 90, y, 3, kText, false);
        y = c.para(p, y, 3, kText, 70, 120) + 6;
    }
    c.text("Found by testing on SteamVR 2.18. Updates can change it.", 70, y + 20, 3, kDim, false);
    two("terms", "Glossary", Kind::NORMAL, "menu", "Menu", Kind::NORMAL, kTex - 120);
}

void Ui::draw_terms(Canvas &c) {
    c.text("Glossary", kTex / 2, 60, 6, kText);
    int y = 190;
    const std::string calibrated =
        fmt("The tracker has %d good clicks. Partly: 1 to %d", kTrackerKeeps, kTrackerKeeps - 1);
    for (auto [term, what] : std::initializer_list<std::pair<const char *, const char *>>{
             {"Calibrated", calibrated.c_str()},
             {"Accuracy", "How far the gaze is from where you look"},
             {"Degrees", "2° is about your thumb's width at arm's length"},
             {"Jitter", "How much the tracked gaze wobbles"},
             {"Shift", "How far the gaze is off in one direction"},
             {"Eyes tracked", "How much of the test your eyes were seen"},
             {"Lost track", "How often an eye was briefly lost"}}) {
        c.text(term, 70, y, 3, kDim, false);
        y = c.para(what, y, 3, kText, 50, 290) + 24;
    }
    two("help", "Help", Kind::NORMAL, "menu", "Menu", Kind::NORMAL);
}

void Ui::draw_message(Canvas &c) {
    c.text(msg_title, kTex / 2, 60, 6, kWarn);
    c.para(msg_text, 200, 4, kText, kMargin);
    if (msg_fatal) btn(kTex - 200, "exit", "Exit", kTex / 2 - 180, kTex / 2 + 180, Kind::PRIMARY);
    else two("test_start", "Test again", Kind::PRIMARY, "menu", "Menu", Kind::NORMAL);
}
