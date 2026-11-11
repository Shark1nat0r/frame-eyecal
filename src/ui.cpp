#include "ui.h"

#include <cmath>
#include <cstdint>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <vector>

#include "canvas.h"
#include "common.h"
#include "link.h"
#include "test_result.h"
#include "tracker_log.h"
#include "targets.h"

int Ui::run() {
    if (!font_init()) {
        fprintf(stderr, "no font found\n");
        return 1;
    }
    if (const char *pdir = getenv("EYECAL_PREVIEW")) return preview(pdir);
    if (!connect()) return 1;
    std::string dir = home() + "/.local/share/frame-eyecal";
    mkdir(dir.c_str(), 0755);
    res_path = dir + "/results.txt";
    load_results(res_path, res, inserts);
    printf("frame-eyecal: overlay %.2f m wide at %.2f m, tracker log %s\n", width_m, dist, log.path.c_str());

    while (!quit) {
        const double t = now_s();
        place_panel(t);
        handle_overlay_events(t);
        poll_scene();
        poll_tracker(t);
        redraw(t);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    ov->DestroyOverlay(h);
    vr::VR_Shutdown();
    return 0;
}

void Ui::poll_scene() { // messages from the scene
    std::vector<std::string> lines;
    lines.swap(early);
    if (!g_link.poll(lines) && (screen != Screen::MESSAGE || !msg_fatal))
        message("Scene stopped",
                "The part of the app that shows the test room ended. Start Frame-EyeCal again from the SteamVR "
                "dashboard.",
                true);
    for (const auto &l : lines) on_scene_line(l);
}

void Ui::poll_tracker(double t) { // the eye tracker's log
    for (const auto &l : log.poll()) on_tracker_line(parse_tracker_line(l), t);
    initial = false;
    if (pending != 0 && t - pending > 1.5) {
        pending = 0;
        msg = log_seen ? "No answer from the eye tracker" : "Eye tracker log not found";
        msg_col = kWarn;
        dirty = true;
    }
}

void Ui::redraw(double t) {
    if (clear_armed > 0 && t - clear_armed > 4) {
        clear_armed = 0;
        dirty = true;
    }
    if (flash_on != (screen == Screen::CAL && t - flash_t < 1.5)) {
        flash_on = !flash_on;
        dirty = true;
    }
    if (!dirty || screen == Screen::TESTING) return;
    dirty = false;
    Canvas c = draw();
    if (use_gpu) gpu.upload(c.px, h);
    else ov->SetOverlayRaw(h, c.px.data(), kTex, kTex, 4);
}

bool Ui::connect() {
    // Connect only after the eye tracker has raised the camera rate for the scene: it decides this about a second
    // after the scene starts, and a panel connecting to SteamVR in that moment makes it keep the slow rate.
    // (Once raised, the rate stays up while the scene keeps reading the gaze.)
    log.path = home() + "/.local/share/Steam/logs/eyetracking.txt";
    {
        TrackerLog wait_log(log.path);
        wait_log.skip();
        printf("waiting for the eye cameras to speed up...\n");
        for (double t0 = now_s(); now_s() - t0 < 10;) {
            if (!g_link.poll(early)) break;
            bool up = false;
            for (const auto &l : early) up |= l.rfind("error ", 0) == 0;
            for (const auto &l : wait_log.poll()) up |= parse_tracker_line(l).fps >= 30;
            if (up) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    vr::EVRInitError err = vr::VRInitError_None;
    vr::VR_Init(&err, vr::VRApplication_Overlay);
    if (err != vr::VRInitError_None) {
        fprintf(stderr, "SteamVR not available: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }
    ov = vr::VROverlay();
    if (ov->CreateOverlay("frame-eyecal", "Frame-EyeCal", &h) != vr::VROverlayError_None) {
        fprintf(stderr, "cannot create the overlay (already running?)\n");
        return false;
    }
    ov->SetOverlayWidthInMeters(h, static_cast<float>(width_m));
    vr::HmdVector2_t mscale = {{static_cast<float>(kTex), static_cast<float>(kTex)}};
    ov->SetOverlayMouseScale(h, &mscale);
    ov->SetOverlayFlag(h, vr::VROverlayFlags_EnableClickStabilization, true);
    set_laser(true);
    use_gpu = gpu.init();
    if (!use_gpu) fprintf(stderr, "Vulkan setup for the panel failed, using SetOverlayRaw\n");
    return true;
}

void Ui::hide_panel() {
    shown = false;
    ov->HideOverlay(h);
}
void Ui::show_panel() {
    if (!shown) {
        shown = true;
        ov->ShowOverlay(h);
    }
}

// Laser clicks on any overlay reach the eye tracker as calibration clicks, whether the app uses them or not.
// Buttons therefore act on the press and switch the panel's laser off for a moment, so the release does not
// arrive and the tracker drops the click ("Next interaction not mouse-up").
void Ui::set_laser(bool on) {
    ov->SetOverlayInputMethod(h, on ? vr::VROverlayInputMethod_Mouse : vr::VROverlayInputMethod_None);
    ov->SetOverlayFlag(h, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, on);
}

// Panel placement: in front of the head; follows when you look more than 25 deg away from it.
// During calibration it is fixed to the head like the test dots, so a dot is as far out for the eyes in both.
void Ui::place_panel(double t) {
    if (screen == Screen::CAL && snap && !passthrough) {
        vr::HmdMatrix34_t pm = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, static_cast<float>(-dist)}}};
        ov->SetOverlayTransformTrackedDeviceRelative(h, vr::k_unTrackedDeviceIndex_Hmd, &pm);
        snap = false;
        show_panel();
    }
    vr::TrackedDevicePose_t pose{}; // the headset's, device 0
    if (screen != Screen::TESTING && screen != Screen::CAL && !passthrough)
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &pose, 1);
    if (pose.bPoseIsValid) {
        const auto &m = pose.mDeviceToAbsoluteTracking.m;
        double hx = m[0][3], hy = m[1][3], hz = m[2][3], fx = -m[0][2], fy = -m[1][2], fz = -m[2][2];
        double dx = hx + fx * dist, dy = hy + fy * dist, dz = hz + fz * dist; // desired centre
        bool move = snap;
        if (snap) {
            cx = dx;
            cy = dy;
            cz = dz;
            snap = following = false;
        } else {
            double vx = cx - hx, vy = cy - hy, vz = cz - hz, l = sqrt(vx * vx + vy * vy + vz * vz);
            if ((vx * fx + vy * fy + vz * fz) / l < cos(25 * kDeg) && t - last_click > 1) following = true;
            if (following) {
                cx += (dx - cx) * 0.12;
                cy += (dy - cy) * 0.12;
                cz += (dz - cz) * 0.12;
                following = hypot(hypot(dx - cx, dy - cy), dz - cz) > 0.01;
                move = true;
            }
        }
        if (move) { // panel faces the head, kept upright
            double zx = hx - cx, zy = hy - cy, zz = hz - cz, l = sqrt(zx * zx + zy * zy + zz * zz);
            zx /= l;
            zy /= l;
            zz /= l;
            double xx = zz, xz = -zx, lx = hypot(xx, xz); // x = up x z
            xx /= lx;
            xz /= lx;
            double yx = zy * xz, yy = zz * xx - zx * xz, yz = -zy * xx; // y = z x x
            vr::HmdMatrix34_t pm = {
                {{static_cast<float>(xx), static_cast<float>(yx), static_cast<float>(zx), static_cast<float>(cx)},
                 {0, static_cast<float>(yy), static_cast<float>(zy), static_cast<float>(cy)},
                 {static_cast<float>(xz), static_cast<float>(yz), static_cast<float>(zz), static_cast<float>(cz)}}};
            ov->SetOverlayTransformAbsolute(h, vr::TrackingUniverseStanding, &pm);
            show_panel();
        }
    }
}

void Ui::handle_overlay_events(double t) {
    vr::VREvent_t ev{};
    while (ov->PollNextOverlayEvent(h, &ev, sizeof ev)) on_overlay_event(ev, t);
    if (laser_back > 0 && t > laser_back) {
        laser_back = 0;
        set_laser(true);
    }
    while (vr::VRSystem()->PollNextEvent(&ev, sizeof ev)) {
        if (ev.eventType == vr::VREvent_Quit) quit = true;
        if (ev.eventType == vr::VREvent_RoomViewShown) {
            passthrough = true;
            hide_panel();
        }
        if (ev.eventType == vr::VREvent_RoomViewHidden) {
            passthrough = false;
            snap = true;
        }
    }
}

void Ui::on_overlay_event(const vr::VREvent_t &ev, double t) { // laser input on the panel (mouse y is bottom-up)
    const int x = static_cast<int>(ev.data.mouse.x), y = kTex - static_cast<int>(ev.data.mouse.y);
    std::string hit;
    for (const auto &b : buttons)
        if (x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1) hit = b.id;
    if (ev.eventType == vr::VREvent_MouseMove && hit != hover) {
        hover = hit;
        dirty = true;
    }
    if (ev.eventType == vr::VREvent_FocusLeave) {
        hover.clear();
        dirty = true;
    }
    if (ev.eventType == vr::VREvent_MouseButtonDown && !hit.empty()) { // see set_laser
        set_laser(false);
        laser_back = t + 0.4;
        printf("%ld button %s on press\n", static_cast<long>(time(nullptr)), hit.c_str());
        act(hit);
        return;
    }
    if (ev.eventType != vr::VREvent_MouseButtonUp) return;
    last_click = t;
    if (screen == Screen::CAL) on_cal_click(x, y, t);
}

void Ui::on_cal_click(int x, int y, double t) { // a click is calibration when it hits the square
    auto [tx, ty] = cal_target_px();
    if (fabs(x - tx) < 40 && fabs(y - ty) < 40) {
        pending = t;
        attempts++;
    } else {
        msg = "Click on the yellow square";
        msg_col = kWarn;
        dirty = true;
    }
}

void Ui::on_tracker_line(const TrackerLine &e, double t) {
    log_seen = true;
    if (e.startup) {
        accepts_since_start = pre_accepts = 0;
        cam_fps = 0;
        cal_stale = false;
        if (!e.version.empty()) et_version = e.version;
    }
    if (screen == Screen::TESTING) {
        test_drop_l += e.drop_l;
        test_drop_r += e.drop_r;
    }
    if (e.fps >= 0) cam_fps = e.fps;
    if (e.accept) {
        accepts_since_start++;
        pre_accepts += initial;
    }
    dirty |= e.accept || e.fps >= 0;
    if (!initial && screen == Screen::CAL) on_cal_verdict(e, t);
}

void Ui::on_cal_verdict(const TrackerLine &e, double t) {
    if (e.accept && pending == 0) { // accepted, but not a click on the square: a second tap, or one beside it
        cal_keep(-1);
        return;
    }
    if (pending == 0 || (!e.accept && e.reject == Reject::NONE)) return;
    pending = 0;
    if (e.accept) {
        sum_l += acos(std::min(e.lf, 1.0)) / kDeg;
        sum_r += acos(std::min(e.rf, 1.0)) / kDeg;
        accepted++;
        fails_here = 0;
        flash_col = kGood;
        flash_t = t;
        if (!cal_keep(cur_target())) {
            msg = fmt("Accepted (%d/%d)", kept_points(), kCalPositions);
            msg_col = kGood;
        }
        if (!next_open()) {
            cal_stale = false;
            go(Screen::CAL_DONE);
        }
    } else {
        switch (e.reject) {
        case Reject::SLOW:
            n_slow++;
            msg = "Too slow, tap quicker";
            break;
        case Reject::EYES:
            n_eyes++;
            msg = "Eyes moved, keep looking at the dot";
            break;
        case Reject::POINTER:
            n_pointer++;
            msg = "Hand moved, keep still";
            break;
        default:
            n_other++;
            msg = "Not accepted, try again";
            break;
        }
        msg_col = flash_col = kBad;
        flash_t = t;
        if (++fails_here >= 6) { // move on, come back later
            fails_here = 0;
            int before = step;
            next_open();
            if (step != before) {
                msg = "Skipped, comes back later";
                msg_col = kWarn;
            }
        }
    }
    dirty = true;
}

static_assert(kCalPositions == kTrackerKeeps, "calibrate as many positions as the tracker keeps clicks");

bool Ui::cal_keep(int target) {
    const int dropped = keep_click(cal_kept, target);
    if (dropped < 0 || kept(dropped)) return false;
    msg = "An extra click pushed out a point, it comes again";
    msg_col = kWarn;
    dirty = true;
    return true;
}

bool Ui::next_open() {
    for (int k = 1; k <= kCalPositions; k++)
        if (!kept(kCalOrder[(step + k) % kCalPositions])) {
            step = (step + k) % kCalPositions;
            return true;
        }
    return false;
}

Ui::Px Ui::cal_target_px() const {
    const Target &tg = kTargets[cur_target()];
    return {kTex / 2.0 + dist * tan(tg.yaw * kDeg) * px_per_m, kTex / 2.0 - dist * tan(tg.pitch * kDeg) * px_per_m};
}

void Ui::go(Screen s) {
    screen = s;
    dirty = true;
    snap = true;
    hover.clear();
    clear_armed = 0;
    saved_image.clear();
}

void Ui::message(const std::string &title, const std::string &text, bool fatal) {
    msg_title = title;
    msg_text = text;
    msg_fatal = fatal;
    go(Screen::MESSAGE);
}

void Ui::start_test() {
    hide_panel();
    test_drop_l = test_drop_r = 0;
    g_link.send("test");
    go(Screen::TESTING);
}

void Ui::save() { save_results(res_path, res, inserts); }

void Ui::share_image() {
    Canvas c = draw(true);
    std::string dir = home(".") + "/Pictures";
    mkdir(dir.c_str(), 0755);
    char name[64];
    time_t now = time(nullptr);
    strftime(name, sizeof name, "frame-eyecal-%Y%m%d-%H%M%S.png", localtime(&now));
    std::string err = write_png(c, dir + "/" + name);
    saved_ok = err.empty();
    if (saved_ok) {
        saved_image = std::string("Saved to Pictures/") + name;
        printf("saved %s/%s\n", dir.c_str(), name);
    } else {
        saved_image = "Not saved: " + err;
        fprintf(stderr, "saving %s/%s failed: %s\n", dir.c_str(), name, err.c_str());
    }
}

void Ui::set_inserts(bool on) {
    if (on == inserts) return;
    inserts = on;
    if (accepts_since_start > 0) cal_stale = !cal_stale; // calibrated with the other lenses (or back to them)
    save();
    dirty = true;
}

void Ui::act(const std::string &id) {
    if (id == "cal") {
        go(Screen::CAL_INTRO);
    } else if (id == "test") {
        go(Screen::TEST_INTRO);
    } else if (id == "menu") {
        go(Screen::MENU);
    } else if (id == "exit") {
        quit = true;
    } else if (id == "test_start") {
        start_test();
    } else if (id == "compare") {
        go(Screen::COMPARE);
    } else if (id == "help") {
        go(Screen::HELP);
    } else if (id == "terms") {
        go(Screen::TERMS);
    } else if (id == "ins_yes" || id == "ins_no") {
        set_inserts(id == "ins_yes");
    } else if (id == "share") {
        share_image();
        dirty = true;
    } else if (id == "clear") {
        if (clear_armed == 0) { // asks for a second press
            clear_armed = now_s();
        } else {
            for (auto &row : res)
                for (auto &x : row) x = TestResult();
            last_slot = -1;
            clear_armed = 0;
            save();
        }
        dirty = true;
    } else if (id == "cal_start") {
        cal_kept.clear();
        step = attempts = accepted = fails_here = n_slow = n_eyes = n_pointer = n_other = 0;
        pending = sum_l = sum_r = 0;
        cal_start = now_s();
        msg = "Laser on the square, look at the dot, tap";
        msg_col = kText;
        go(Screen::CAL);
    }
}

void Ui::on_scene_line(const std::string &l) { // a message from the scene
    if (l.rfind("hz ", 0) == 0) {
        display_hz = atoi(l.c_str() + 3);
        dirty = true;
    } else if (l.rfind("pid ", 0) == 0) {
        // SteamVR gives every app the Frame's default rate (72 Hz) and ignores the OpenXR request; the
        // per-application setting (VR Settings > Video > Per-Application) is honoured, also live.
        char key[vr::k_unMaxApplicationKeyLength] = "";
        vr::VRApplications()->GetApplicationKeyByProcessId(static_cast<uint32_t>(atoi(l.c_str() + 4)), key, sizeof key);
        vr::EVRSettingsError se = vr::VRSettingsError_None;
        if (key[0] && vr::VRSettings()->GetFloat(key, "preferredRefreshRate", &se) != rate_hz) {
            vr::VRSettings()->SetFloat(key, "preferredRefreshRate", rate_hz, &se);
            printf("set %s preferredRefreshRate %.0f (error %d)\n", key, rate_hz, se);
        }
    } else if (l.rfind("error ", 0) == 0) {
        std::string e = l.substr(6);
        if (e == kNoEyes)
            message("Eye tracking is off", "Turn it on in the SteamVR settings, then start Frame-EyeCal again.", true);
        else message("Cannot start", "The test room failed to start (" + e + "). Start Frame-EyeCal again.", true);
    } else if (l == "nofocus") {
        message("Test stopped",
                "The app lost focus, usually because the SteamVR dashboard opened. Close it and "
                "test again.",
                false);
    } else if (l.rfind("result", 0) == 0) {
        TestResult r;
        if (const char *rest = read_measures(l.c_str() + 6, r)) {
            sscanf(rest, "%d", &r.unsure);
            r.when = static_cast<double>(time(nullptr));
            r.drop_l = test_drop_l;
            r.drop_r = test_drop_r;
            r.app_ver = version.empty() ? "dev" : version;
            r.et_ver = et_version;
            last = r;
            last_slot = slot_now();
            if (last_slot >= 0) {
                res[last_slot / 2][last_slot % 2] = r;
                save();
            }
            printf("test: accuracy %.2f deg, precision %.2f deg, offset h %.2f v %.2f, valid %.0f%%\n", r.acc, r.prec,
                   r.bh, r.bv, r.valid);
            go(Screen::TEST_DONE);
        } else {
            message("No gaze data",
                    "The test got no gaze at all. Check that the headset sits well and eye tracking "
                    "is on in the SteamVR settings, then test again.",
                    false);
        }
    }
}
