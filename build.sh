#!/bin/bash
# Local build: runs compile.sh in an arm64 Ubuntu 24.04 container (podman + qemu-user-static on x86 hosts).
set -euo pipefail
cd "$(dirname "$0")"
[ "$(uname -m)" = aarch64 ] || [ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ] || {
    echo "register qemu-aarch64 binfmt first" >&2
    exit 1
}
sudo podman build -q --arch arm64 -t frame-eyecal-build -f Containerfile . >/dev/null
sudo podman run --rm --arch arm64 -v "$PWD":/src:Z -w /src frame-eyecal-build ./compile.sh
sudo chown -R "$(id -u):$(id -g)" build
file build/frame-eyecal
