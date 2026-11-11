// The parent process: the UI as a SteamVR overlay panel (OpenVR). Calibration clicks on it feed the eye tracker;
// the tracker's verdicts are read from its log. The accuracy test runs in the scene process (see scene.h).
#pragma once

#include <openvr.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "canvas.h"
#include "targets.h"
#include "test_result.h"
#include "tracker_log.h"
#include "vk_panel.h"

enum class Screen { MENU, CAL_INTRO, CAL, CAL_DONE, TEST_INTRO, TESTING, TEST_DONE, COMPARE, MESSAGE, HELP, TERMS };

class Ui {
  public:
    Ui(float hz, std::string version) : rate_hz(hz), version(std::move(version)) {}
    int run(); // until Exit or SteamVR quits; the exit code

  private:
    // ui.cpp: SteamVR, input, the scene and the tracker log
    bool connect();
    void place_panel(double t);
    void handle_overlay_events(double t);
    void on_overlay_event(const vr::VREvent_t &ev, double t);
    void on_cal_click(int x, int y, double t);
    void poll_scene();
    void poll_tracker(double t);
    void redraw(double t);
    void on_scene_line(const std::string &l);
    void on_tracker_line(const TrackerLine &e, double t);
    void hide_panel();
    void show_panel();
    void set_laser(bool on);
    void act(const std::string &id);
    void go(Screen s);
    void message(const std::string &title, const std::string &text, bool fatal);
    void start_test();
    void set_inserts(bool on);
    void save();
    void share_image();
    [[nodiscard]] int slot_now() const { return condition_slot(log_seen, accepts_since_start, cal_stale, inserts); }
    [[nodiscard]] int cur_target() const { return kCalOrder[step]; }
    bool next_open(); // to the next position not yet accepted, cycling; false when all are done
    struct Px {
        double x, y;
    };
    [[nodiscard]] Px cal_target_px() const; // where the current calibration target is on the panel
    // ui_draw.cpp. share: the comparison as the image to share, without buttons but with versions and date
    Canvas draw(bool share = false);
    struct MenuStatus {
        std::string status, next, primary;
        Col col;
    };
    [[nodiscard]] MenuStatus menu_status() const;
    void btn(int y, const std::string &id, const std::string &label, int x0, int x1, Kind k = Kind::NORMAL);
    void two(const char *id1, const char *l1, Kind k1, const char *id2, const char *l2, Kind k2, int y = kTex - 200);
    void lens(Canvas &c, int y, int x0, int x1);
    void saved_as(Canvas &c, int y, int x) const;
    void draw_menu(Canvas &c);
    void draw_intro(Canvas &c);
    void draw_cal(Canvas &c);
    void draw_cal_done(Canvas &c);
    void draw_test_done(Canvas &c);
    void draw_compare(Canvas &c, bool share);
    void compare_versions(Canvas &c) const;
    void compare_buttons(Canvas &c, bool any);
    void draw_help(Canvas &c);
    void draw_terms(Canvas &c);
    void draw_message(Canvas &c);
    // ui_preview.cpp: development, every screen with sample data as PNG files in dir
    int preview(const std::string &dir);

    const float rate_hz; // display rate the scene asks for
    std::string version; // of this app, from the VERSION file ("" for a dev build)
    const double dist = 1.5; // panel distance, m
    const double width_m = 2 * dist * tan(22 * kDeg), px_per_m = kTex / width_m;

    vr::IVROverlay *ov = nullptr;
    vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
    VkPanel gpu;
    bool use_gpu = false;
    bool shown = false;
    double laser_back = 0; // time to switch the laser on again after a button press
    TrackerLog log;
    std::vector<std::string> early; // scene messages that arrived before the panel connected

    // Newest test per condition [inserts][calibrated], kept in a file: a "not calibrated" test needs a SteamVR
    // restart, which also ends this app.
    TestResult last, res[2][2];
    int last_slot = -1; // index into res of the newest test, -1 if it fits no condition
    bool inserts = false, cal_stale = false; // cal_stale: lenses switched since the tracker was calibrated
    std::string res_path;
    int test_drop_l = 0, test_drop_r = 0; // the tracker's per-eye dropouts during the running test

    Screen screen = Screen::MENU;
    std::string msg_title, msg_text; // MESSAGE screen
    bool msg_fatal = false; // the scene is gone: only Exit helps
    double clear_armed = 0; // Compare: time of the first press on "Clear results"
    std::string saved_image; // Compare: the outcome of "Save image", shown under the buttons
    bool saved_ok = false;
    std::string et_version; // the eye tracker's version, from its log
    std::vector<Button> buttons;
    std::string hover;
    int accepts_since_start = 0, display_hz = 0;
    int pre_accepts = 0; // accepted before this app opened: usually the dashboard clicks that started it
    double cam_fps = 0;
    bool log_seen = false, initial = true, quit = false, dirty = true, snap = true, following = false;
    double cx = 0, cy = 0, cz = 0, last_click = 0; // panel centre while it follows the head
    bool passthrough = false; // the panel hides while passthrough (room view) is on

    // calibration state
    std::vector<int> cal_kept; // the tracker's kept clicks since the calibration started (keep_click)
    // a target is done while its click is among the tracker's kept ones
    [[nodiscard]] bool kept(int target) const {
        return std::find(cal_kept.begin(), cal_kept.end(), target) != cal_kept.end();
    }
    [[nodiscard]] int kept_points() const {
        return static_cast<int>(std::count_if(cal_kept.begin(), cal_kept.end(), [](int k) { return k >= 0; }));
    }
    // an accepted click (-1: not on the square); true, with a warning shown, when it pushed out a point
    bool cal_keep(int target);
    void on_cal_verdict(const TrackerLine &e, double t); // a tracker log line during calibration
    // accepted: all accepted clicks of this calibration, also those pushed out since (eye movement average)
    int step = 0, attempts = 0, accepted = 0, fails_here = 0, n_slow = 0, n_eyes = 0, n_pointer = 0, n_other = 0;
    double pending = 0, cal_start = 0, sum_l = 0, sum_r = 0;
    std::string msg;
    Col msg_col = kText, flash_col = kGood;
    double flash_t =
        -10; // feedback border: shown for 1.5 s after each verdict (redrawing more often makes the overlay flicker)
    bool flash_on = false;
};
