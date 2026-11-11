#!/bin/sh
# Install the pinned lint tools (requirements-lint.txt, plus shfmt) into build/lint-tools, which tools/lint.sh
# uses before anything on PATH. Same versions locally and in CI, on any x86_64 or arm64 Linux. Needs Python 3.10
# to 3.14 (semgrep has no wheels for newer Pythons; on Fedora 45: sudo dnf install python3.14) and curl.
# The C++ checks also need g++ with libasan/libubsan and the freetype, libpng, Vulkan and OpenXR headers.
set -eu
cd "$(dirname "$0")/.."
SHFMT=v3.14.1 # not on PyPI under its own version; tools/check-updates.sh reports newer releases
SHFMT_SHA256_AMD64=76e77641faa025814b77f153b29796b8e6fa2fca03e0c76a691608b86c7ea7bf
SHFMT_SHA256_ARM64=5f2db09dae91fca848f7adbdd014632e921a383863a2ad7e0450ad3aba0c6489
dir=build/lint-tools

py=
for v in 3.14 3.13 3.12 3.11 3.10; do
    if command -v "python$v" >/dev/null; then
        py=python$v
        break
    fi
done
[ -n "$py" ] || {
    echo "lint-deps: needs Python 3.10 to 3.14 (Fedora 45: sudo dnf install python3.14)" >&2
    exit 1
}
rm -rf "$dir"
"$py" -m venv "$dir"
"$dir/bin/pip" install -q --disable-pip-version-check -r requirements-lint.txt

case $(uname -m) in
x86_64) arch=amd64 sum=$SHFMT_SHA256_AMD64 ;;
aarch64) arch=arm64 sum=$SHFMT_SHA256_ARM64 ;;
*)
    echo "lint-deps: no shfmt for $(uname -m)" >&2
    exit 1
    ;;
esac
curl -fsSL -o "$dir/bin/shfmt" "https://github.com/mvdan/sh/releases/download/$SHFMT/shfmt_${SHFMT}_linux_$arch"
echo "$sum  $dir/bin/shfmt" | sha256sum -c --quiet -
chmod +x "$dir/bin/shfmt"
echo "lint-deps: tools installed in $dir"
