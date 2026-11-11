#!/bin/bash
# Report newer versions of the pinned dependencies; the pre-push hook runs this (.githooks/pre-push).
#   tools/check-updates.sh     exit 0: all current, 1: updates available, 2: could not check
#   tools/check-updates.sh --shipped     only what is compiled into the release (the OpenVR SDK); the weekly CI run
# Checked: the OpenVR SDK tag (tools/openvr-sdk.sh), shfmt (tools/lint-deps.sh) and the GitHub Actions major
# versions (.github/workflows). Dependabot also watches the actions and requirements-lint.txt.
# Not checked on purpose: Ubuntu 24.04 (build image and runner): the binary must run with SteamOS's glibc, so the
# build stays on glibc <= 2.39. The apt packages come from that release and need no pin.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

status=0
latest_tag() { # latest_tag OWNER/REPO REGEX: the highest tag matching REGEX
    git ls-remote --tags --refs "https://github.com/$1" 2>/dev/null | sed 's|.*refs/tags/||' | grep -E "$2" | sort -V | tail -n 1
}

# OpenVR SDK
have=$(sed -n 's/^OPENVR=//p' tools/openvr-sdk.sh)
want=$(latest_tag ValveSoftware/openvr '^v[0-9]+\.[0-9]+\.[0-9]+$')
if [ -z "$want" ]; then
    echo "check-updates: could not reach github.com for ValveSoftware/openvr" >&2
    status=2
elif [ "$want" != "$have" ]; then
    echo "update: OpenVR SDK $have -> $want (set OPENVR and the three SHA-256 sums in tools/openvr-sdk.sh)"
    status=1
fi

[ "${1:-}" = --shipped ] && {
    [ "$status" = 0 ] && echo "check-updates: OpenVR SDK is current"
    exit "$status"
}

# shfmt, pinned in tools/lint-deps.sh (the other lint tools are in requirements-lint.txt, which Dependabot watches)
have=$(sed -n 's/^SHFMT=\(v[0-9.]*\).*/\1/p' tools/lint-deps.sh)
want=$(latest_tag mvdan/sh '^v[0-9]+\.[0-9]+\.[0-9]+$')
if [ -z "$want" ]; then
    echo "check-updates: could not reach github.com for mvdan/sh" >&2
    status=2
elif [ "$want" != "$have" ]; then
    echo "update: shfmt $have -> $want (SHFMT and both SHA-256 sums in tools/lint-deps.sh)"
    [ "$status" = 2 ] || status=1
fi

# GitHub Actions, used by major version
while read -r action major; do
    want=$(latest_tag "$action" '^v[0-9]+$')
    if [ -z "$want" ]; then
        echo "check-updates: could not look up $action" >&2
        status=2
    elif [ "${want#v}" -gt "${major#v}" ]; then
        echo "update: $action $major -> $want (.github/workflows)"
        [ "$status" = 2 ] || status=1
    fi
done < <(grep -ho 'uses: *[A-Za-z0-9_.-]*/[A-Za-z0-9_.-]*@v[0-9]*$' .github/workflows/*.yml | sed 's/uses: *//; s/@/ /' | sort -u)

[ "$status" = 0 ] && echo "check-updates: dependencies are current"
exit "$status"
