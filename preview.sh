#!/bin/bash
# Development: render every screen with sample data on the headset and fetch them as PNGs into $1 (default build/preview).
set -euo pipefail
cd "$(dirname "$0")"
HOST=${FRAME_HOST:-frame}
out=${1:-build/preview}
mkdir -p "$out"
scp -q build/frame-eyecal "$HOST:/tmp/frame-eyecal-preview"
ssh "$HOST" 'rm -rf /tmp/eyecal-preview && mkdir /tmp/eyecal-preview && EYECAL_PREVIEW=/tmp/eyecal-preview timeout 30 /tmp/frame-eyecal-preview >/dev/null 2>&1; true'
scp -q "$HOST:/tmp/eyecal-preview/*.png" "$out/"
ls "$out"/*.png
