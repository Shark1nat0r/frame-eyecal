// frame-eyecal: eye-tracking calibration and accuracy test for the Steam Frame (native Linux ARM64).
//
// The Frame's eye tracker calibrates itself from laser clicks ("usercal"): when you click something, it assumes
// you were looking at the click point. It keeps the last 8 good clicks, in memory only (lost when SteamVR
// restarts), and it rejects almost every click while the eye cameras run at 15 fps, which they do whenever
// no app reads the gaze.
//
// Two processes:
//  - parent: the UI as a SteamVR overlay (OpenVR). Calibration clicks on it feed the tracker; the verdicts are
//    read from the tracker's log.
//  - child: an OpenXR scene (grey room, test targets). It reads the gaze every frame, which keeps the eye
//    cameras at the display rate, and runs the accuracy test.
//
// Usage: frame-eyecal [--hz N]   (display rate to request, default 90)
//
// Source layout: ui* (the panel), scene (the test room), and around them the parts that need no SteamVR and are
// unit-tested (tests/): test_result, gaze_test, tracker_log, link, geometry, common.

#include <openvr.h>

#include <fcntl.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): SIGPIPE is POSIX, declared only there
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "link.h"
#include "scene.h"
#include "ui.h"

// For the uninstaller while SteamVR runs (it keeps its settings in memory and rewrites the file): remove the
// refresh-rate setting written for the scene's application key (see "pid" in Ui::on_scene_line).
static int forget_settings() {
    vr::EVRInitError e = vr::VRInitError_None;
    vr::VR_Init(&e, vr::VRApplication_Background);
    if (e != vr::VRInitError_None) return 1;
    vr::EVRSettingsError se = vr::VRSettingsError_None;
    const std::string section = std::string("system.generated.openxr.") + kSceneApp + ".." + kSceneApp;
    vr::VRSettings()->RemoveSection(section.c_str(), &se);
    vr::VR_Shutdown();
    return se == vr::VRSettingsError_None ? 0 : 1;
}

namespace {
struct Args {
    float hz = 90; // display rate to request
    int scene_fd = -1; // set in the scene process: its end of the link
};
} // namespace
static bool parse_args(int argc, char *const *argv, Args &a) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hz") && i + 1 < argc) a.hz = static_cast<float>(atof(argv[++i]));
        else if (!strcmp(argv[i], "--scene-fd") && i + 1 < argc) a.scene_fd = atoi(argv[++i]);
        else return false;
    }
    return true;
}

// The scene runs as its own program: when both processes run the same executable, SteamVR gives them one
// application identity and the eye tracker does not raise the camera rate for the scene's gaze queries.
// A hard link next to the binary gives it a second name without a second file.
static bool link_scene_exe(const char *self, const std::string &scene_exe) {
    struct stat a{}, b{};
    if (!stat(scene_exe.c_str(), &b) && !stat(self, &a) && a.st_ino == b.st_ino) return true;
    unlink(scene_exe.c_str());
    if (!link(self, scene_exe.c_str())) return true;
    perror(("cannot create " + scene_exe).c_str());
    return false;
}

// Starts the scene detached (double fork, own session): as a child of the panel process it does not get the faster
// eye cameras. It exits when the link to the panel closes. Returns the panel's end of the link, -1 on failure.
static int start_scene(const std::string &scene_exe, float hz) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) {
        perror("socketpair");
        return -1;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        setsid();
        if (fork() != 0) _exit(0);
        const std::string fd = std::to_string(sv[1]), hzs = std::to_string(hz);
        execl(scene_exe.c_str(), kSceneApp, "--scene-fd", fd.c_str(), "--hz", hzs.c_str(),
              static_cast<char *>(nullptr));
        perror("exec scene");
        _exit(1);
    }
    close(sv[1]);
    waitpid(pid, nullptr, 0);
    return sv[0];
}

static std::string read_version(const std::string &dir) { // VERSION next to the binary, written by the release build
    std::string version;
    if (FILE *f = fopen((dir + "/VERSION").c_str(), "r")) {
        char v[64] = "";
        if (fgets(v, sizeof v, f)) version = std::string(v, strcspn(v, "\n"));
        fclose(f);
    }
    return version;
}

int main(int argc, char **argv) { // NOLINT(misc-const-correctness): the signature of main is fixed
    if (argc == 2 && !strcmp(argv[1], "--forget-settings")) return forget_settings();
    Args args;
    if (!parse_args(argc, argv, args)) {
        fprintf(stderr, "usage: %s [--hz N]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    if (args.scene_fd >= 0) { // the child, started below
        g_link.fd = args.scene_fd;
        fcntl(args.scene_fd, F_SETFL, O_NONBLOCK);
        return scene(args.hz);
    }
    char self[4096];
    const ssize_t sl = readlink("/proc/self/exe", self, sizeof self - 1);
    if (sl <= 0) {
        perror("readlink");
        return 1;
    }
    self[sl] = 0;
    const std::string dir(self, strrchr(self, '/') - self), scene_exe = dir + "/." + kSceneApp;
    if (!link_scene_exe(self, scene_exe)) return 1;
    g_link.fd = start_scene(scene_exe, args.hz);
    if (g_link.fd < 0) return 1;
    fcntl(g_link.fd, F_SETFL, O_NONBLOCK);
    const int r = Ui(args.hz, read_version(dir)).run();
    g_link.send("quit");
    std::vector<std::string> rest; // the scene closes the link when it has ended its session
    for (int i = 0; i < 300 && g_link.poll(rest); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return r;
}
