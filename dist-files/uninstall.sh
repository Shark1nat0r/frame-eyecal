#!/bin/sh
# Remove Frame-EyeCal and everything it and SteamVR wrote for it, including the test results:
#   ~/.local/lib/frame-eyecal/uninstall.sh
set -u
lib="$HOME/.local/lib/frame-eyecal"
apps="$HOME/.local/share/applications"
cfg="$HOME/.config/openvr/config"

systemctl --user stop frame-eyecal 2>/dev/null || true

# The app's 90 Hz setting in steamvr.vrsettings. While SteamVR runs it keeps settings in memory and rewrites
# the file, so ask SteamVR to drop it; otherwise edit the file.
# This script deletes itself below, so a failure here only reports the leftover: it is a refresh rate for an
# application that no longer exists, and affects nothing else.
if pgrep -u "$(id -u)" -x vrserver >/dev/null 2>&1; then
    "$lib/frame-eyecal" --forget-settings ||
        echo "note: could not remove Frame-EyeCal's refresh-rate setting from SteamVR (harmless, affects no other app)" >&2
elif grep -q frame-eyecal "$cfg/steamvr.vrsettings" 2>/dev/null; then
    if command -v jq >/dev/null 2>&1 &&
        jq 'with_entries(select(.key | contains("frame-eyecal") | not))' "$cfg/steamvr.vrsettings" \
            >"$cfg/steamvr.vrsettings.tmp" && [ -s "$cfg/steamvr.vrsettings.tmp" ]; then
        mv "$cfg/steamvr.vrsettings.tmp" "$cfg/steamvr.vrsettings"
    else
        rm -f "$cfg/steamvr.vrsettings.tmp"
        echo "note: could not remove Frame-EyeCal's refresh-rate setting from $cfg/steamvr.vrsettings (harmless, left unchanged)" >&2
    fi
fi
# Input bindings SteamVR generated for the app
rm -f "$cfg"/openxr/*frame-eyecal*_binding.json

rm -rf "$lib" "$HOME/.local/share/frame-eyecal" "$apps/frame-eyecal.desktop" \
    "$HOME/.local/share/icons/hicolor/scalable/apps/frame-eyecal.svg"
touch -c "$HOME/.local/share/icons/hicolor" 2>/dev/null || true # so a running Steam notices; -c: never create it as a file
update-desktop-database "$apps" >/dev/null 2>&1 || true
kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
echo "Frame-EyeCal removed"
