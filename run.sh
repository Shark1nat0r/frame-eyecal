#!/bin/bash
# Development: build, package and install on the headset over ssh, the same way a release is installed.
set -euo pipefail
cd "$(dirname "$0")"
HOST=${FRAME_HOST:-frame}
./build.sh
./dist.sh "dev-$(date +%Y%m%d%H%M)"
scp -q dist/frame-eyecal.tar.gz dist/frame-eyecal.tar.gz.sha256 install.sh "$HOST:/tmp/"
ssh "$HOST" 'FRAME_EYECAL_TARBALL=/tmp/frame-eyecal.tar.gz sh /tmp/install.sh'
