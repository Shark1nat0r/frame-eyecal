#!/bin/sh
# Package build/frame-eyecal as dist/frame-eyecal.tar.gz, the release asset install.sh downloads, and its
# SHA-256 sum as dist/frame-eyecal.tar.gz.sha256, which install.sh checks.
#   ./dist.sh [version]
set -eu
cd "$(dirname "$0")"
d=dist/frame-eyecal
rm -rf dist && mkdir -p "$d"
cp build/frame-eyecal dist-files/uninstall.sh dist-files/frame-eyecal.svg LICENSE "$d/"
cp build/sdk/LICENSE "$d/LICENSE.openvr"
echo "${1:-dev}" >"$d/VERSION"
tar -C dist -czf dist/frame-eyecal.tar.gz frame-eyecal
(cd dist && sha256sum frame-eyecal.tar.gz >frame-eyecal.tar.gz.sha256)
