#!/bin/sh
# Fetch the OpenVR SDK files the build needs into build/sdk (BSD-3-Clause): header, import library and license,
# from Valve's repo. The SHA-256 sums pin the files of that tag; update them together with OPENVR (see
# tools/check-updates.sh). Cached files are checked too.
set -eu
cd "$(dirname "$0")/.."
OPENVR=v2.15.6
sdk=build/sdk
mkdir -p "$sdk"
for f in headers/openvr.h:1e6ed57199896cc1f7c5484e50fa18955e97be15be690beb28d998c877ead7fd \
    bin/linuxarm64/libopenvr_api.so:cc3671d24dd23fb61494e8a8327cf4fe7f638a60508ff7225984a03f2f5947e9 \
    LICENSE:f56ff606104d4ef18e617921a75c73ad73b5a1a1d70c69590c29de16919e04ad; do
    path=${f%%:*} sum=${f##*:} out="$sdk/$(basename "${f%%:*}")"
    [ -f "$out" ] || curl -sSfL -o "$out" "https://raw.githubusercontent.com/ValveSoftware/openvr/$OPENVR/$path"
    if ! echo "$sum  $out" | sha256sum -c --quiet -; then
        echo "checksum mismatch for $out (OpenVR $OPENVR $path), removed it" >&2
        rm -f "$out"
        exit 1
    fi
done
