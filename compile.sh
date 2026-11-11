#!/bin/sh
# Compile frame-eyecal on an arm64 Linux with glibc <= 2.39 (Ubuntu 24.04: the GitHub runner, or the container
# from build.sh). Needs the packages in build-deps.txt. Output: build/frame-eyecal
set -eu
cd "$(dirname "$0")"
tools/openvr-sdk.sh
sdk=build/sdk
# SteamVR ships its OpenXR loader as libopenxr_loader.so (no .so.1), so link against a copy of the system loader
# renamed to that soname. At run time both libraries come from SteamVR through the rpath.
cp -L /usr/lib/aarch64-linux-gnu/libopenxr_loader.so.1 "$sdk/libopenxr_loader.so"
patchelf --set-soname libopenxr_loader.so "$sdk/libopenxr_loader.so"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$sdk" -I/usr/include/freetype2 -o build/frame-eyecal src/*.cpp \
    -L"$sdk" -l:libopenvr_api.so -l:libopenxr_loader.so -lvulkan -lfreetype -lpng16 -lpthread \
    -Wl,-rpath,/opt/steamvr/bin/linuxarm64
