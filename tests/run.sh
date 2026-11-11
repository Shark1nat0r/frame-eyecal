#!/bin/sh
# Build and run the unit tests natively (any Linux with g++): the tested sources need no SteamVR, OpenXR or Vulkan.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o build/tests tests/*.cpp \
    src/common.cpp src/link.cpp src/test_result.cpp src/gaze_test.cpp src/tracker_log.cpp
build/tests
