// The child process: the OpenXR test room. It talks to the panel over g_link.
#pragma once

int scene(float hz); // runs until the panel quits or SteamVR ends the session; the exit code
