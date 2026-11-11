// Small helpers and names shared by the panel and the scene.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The scene's OpenXR application name. It must differ from the panel's app key, or SteamVR binds the gaze action
// to the panel. SteamVR keeps the scene's settings under a key made from it (see --forget-settings).
inline constexpr char kSceneApp[] = "frame-eyecal-scene";
// Sent by the scene when the runtime has eye tracking switched off, recognised by the panel
inline constexpr char kNoEyes[] = "eye tracking is off (SteamVR settings)";

double now_s(); // monotonic seconds
std::string home(const char *fallback = "");
std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
// Moves the complete lines of buf to out and keeps the unfinished rest
void take_lines(std::string &buf, std::vector<std::string> &out);
// The UTF-8 code point at t[i], advancing i past it
uint32_t next_cp(const std::string &t, size_t &i);
