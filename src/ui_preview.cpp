// Development: every screen with sample data, as PNG files (see preview.sh). Needs no SteamVR.
#include <algorithm>
#include <string>

#include "canvas.h"
#include "common.h"
#include "targets.h"
#include "test_result.h"
#include "tracker_log.h"
#include "ui.h"

int Ui::preview(const std::string &pdir) {
    auto dump = [&](const char *name, bool share = false) {
        Canvas c = draw(share);
        write_png(c, std::string(pdir) + "/" + name + ".png");
    };
    log_seen = true;
    accepts_since_start = 3;
    display_hz = 90;
    cam_fps = 90;
    version = "v1.0";
    screen = Screen::MENU;
    dump("menu");
    screen = Screen::CAL_INTRO;
    dump("cal_intro");
    screen = Screen::TEST_INTRO;
    dump("test_intro");
    cal_kept = {kCalOrder[0], kCalOrder[1]};
    step = 2;
    attempts = 5;
    msg = "Eyes moved, keep looking at the dot";
    msg_col = flash_col = kBad;
    flash_on = true;
    screen = Screen::CAL;
    dump("cal");
    flash_on = false;
    attempts = 14;
    n_eyes = 3;
    n_slow = 1;
    n_pointer = 1;
    accepted = kTrackerKeeps;
    sum_l = kTrackerKeeps * 1.2;
    sum_r = kTrackerKeeps * 1.6;
    cal_start = now_s() - 48;
    screen = Screen::CAL_DONE;
    dump("cal_done");
    double per[9] = {1.8, 2.2, 3.1, 1.4, 1.1, 2.6, 2.9, 4.4, 2.0};
    TestResult r;
    r.when = 1;
    r.acc = 2.4;
    r.prec = 0.4;
    r.bh = 0.6;
    r.bv = -0.3;
    r.valid = 98;
    std::copy(per, per + 9, r.per);
    r.drop_r = 2;
    last = r;
    res[0][0] = r;
    res[0][0].drop_l = 3;
    res[0][0].drop_r = 12;
    res[0][1] = r;
    res[0][1].acc = 1.6;
    res[1][0] = r;
    res[1][0].acc = 3.4;
    res[1][0].unsure = 1 << 3 | 1 << 2;
    last.unsure = 1 << 3;
    for (int k = 0; k < 9; k++) {
        res[0][1].per[k] *= 0.7;
        res[1][0].per[k] *= 1.4;
    }
    last_slot = 0;
    screen = Screen::TEST_DONE;
    dump("test_done");
    screen = Screen::COMPARE;
    dump("compare");
    clear_armed = 1;
    dump("compare_clear");
    clear_armed = 0;
    for (auto &row : res)
        for (auto &x : row) {
            x.app_ver = "v1.0";
            x.et_ver = "2.18.2";
        }
    res[1][1] = res[0][1];
    res[1][1].acc = 2.1;
    saved_image = "Saved to Pictures/frame-eyecal-20261111-152000.png";
    saved_ok = true;
    dump("compare_full");
    saved_image.clear();
    dump("share", true);
    res[1][1].app_ver = "v1.1";
    dump("share_mixed", true);
    res[1][1] = TestResult();
    on_scene_line("nofocus");
    dump("message");
    on_scene_line(std::string("error ") + kNoEyes);
    dump("message_fatal");
    for (auto &row : res)
        for (auto &x : row) x = TestResult();
    screen = Screen::COMPARE;
    dump("compare_empty");
    accepts_since_start = 0;
    screen = Screen::MENU;
    dump("menu_start");
    accepts_since_start = kTrackerKeeps;
    screen = Screen::TEST_INTRO;
    dump("test_intro_cal");
    accepts_since_start = pre_accepts = 1;
    screen = Screen::MENU;
    dump("menu_preclick");
    screen = Screen::TEST_INTRO;
    dump("test_intro_preclick");
    screen = Screen::HELP;
    dump("help");
    screen = Screen::TERMS;
    dump("terms");
    return 0;
}
