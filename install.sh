#!/bin/sh
# Install or update Frame-EyeCal (in the SteamVR dashboard's + list), for the current user only:
#   curl -fsSL https://raw.githubusercontent.com/Shark1nat0r/frame-eyecal/main/install.sh | sh
# An update replaces the installed version completely; test results are kept.
# Remove it with ~/.local/lib/frame-eyecal/uninstall.sh
set -eu

main() {
    repo=${FRAME_EYECAL_REPO:-Shark1nat0r/frame-eyecal}
    lib="$HOME/.local/lib/frame-eyecal"
    apps="$HOME/.local/share/applications"
    icons="$HOME/.local/share/icons/hicolor"
    [ "$(uname -m)" = aarch64 ] || {
        echo "Frame-EyeCal runs on the Steam Frame only" >&2
        exit 1
    }

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    # The release's SHA-256 sum comes from the same release: it catches a broken or cut-off download. Who built
    # the release can be checked with: gh attestation verify frame-eyecal.tar.gz --repo $repo
    if [ -n "${FRAME_EYECAL_TARBALL:-}" ]; then
        cp "$FRAME_EYECAL_TARBALL" "$tmp/release.tar.gz"
        [ ! -f "$FRAME_EYECAL_TARBALL.sha256" ] || cp "$FRAME_EYECAL_TARBALL.sha256" "$tmp/release.tar.gz.sha256"
    else
        url="https://github.com/$repo/releases/latest/download/frame-eyecal.tar.gz"
        curl -fsSL -o "$tmp/release.tar.gz" "$url"
        curl -fsSL -o "$tmp/release.tar.gz.sha256" "$url.sha256" 2>/dev/null || rm -f "$tmp/release.tar.gz.sha256"
    fi
    if [ -f "$tmp/release.tar.gz.sha256" ]; then
        want=$(cut -d' ' -f1 "$tmp/release.tar.gz.sha256")
        have=$(sha256sum "$tmp/release.tar.gz" | cut -d' ' -f1)
        [ "$want" = "$have" ] || {
            echo "checksum mismatch: the download is damaged, nothing was changed. Try again." >&2
            exit 1
        }
    else
        echo "note: this release has no checksum file, installing without the check" >&2
    fi
    tar -xzf "$tmp/release.tar.gz" -C "$tmp"
    [ -x "$tmp/frame-eyecal/frame-eyecal" ] || {
        echo "downloaded release is incomplete" >&2
        exit 1
    }
    old=$(cat "$lib/VERSION" 2>/dev/null || true)

    systemctl --user stop frame-eyecal 2>/dev/null || true
    # Swap the whole folder, so nothing from an older version stays behind
    mkdir -p "${lib%/*}" "$apps"
    rm -rf "$lib.new"
    cp -r "$tmp/frame-eyecal" "$lib.new"
    rm -rf "$lib"
    mv "$lib.new" "$lib"
    mkdir -p "$icons/scalable/apps"
    cp "$lib/frame-eyecal.svg" "$icons/scalable/apps/frame-eyecal.svg"
    # A running Steam (GTK) rescans an icon theme only when the theme folder itself changes, not its subfolders
    touch "$icons"
    # Started as a transient systemd user unit, so a second start while it runs does nothing.
    # Its output: journalctl --user -u frame-eyecal
    cat >"$apps/frame-eyecal.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Frame-EyeCal
Comment=Calibrate the eye tracker and test its accuracy
Exec=/usr/bin/systemd-run --user --collect --quiet --unit=frame-eyecal $lib/frame-eyecal
Icon=frame-eyecal
Terminal=false
Categories=Utility;
OnlyShowIn=gamescope;
DESKTOP
    update-desktop-database "$apps" >/dev/null 2>&1 || true
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true

    new=$(cat "$lib/VERSION")
    if [ -n "$old" ] && [ "$old" != "$new" ]; then
        echo "Frame-EyeCal updated: $old -> $new"
    else echo "Frame-EyeCal $new installed"; fi
    echo "Start it from the SteamVR dashboard: + > Frame-EyeCal (a new entry can need a headset restart)."
    echo "Remove it with: ~/.local/lib/frame-eyecal/uninstall.sh"
}

main "$@"
