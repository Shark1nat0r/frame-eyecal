#!/bin/bash
# Formatting, static checks and unit tests; the pre-commit hook runs this (.githooks/pre-commit).
#   tools/lint.sh          check
#   tools/lint.sh --fix    reformat the C++ and shell sources first (clang-format, shfmt)
# Needs: clang-format, clang-tidy, shfmt, shellcheck, hadolint, semgrep, g++ with libasan/libubsan, curl. Install the
# pinned versions CI uses with tools/lint-deps.sh (into build/lint-tools, used before anything on PATH).
# semgrep downloads its registry rulesets, so it needs the network.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
if [ -x build/lint-tools/bin/semgrep ]; then
    PATH="$PWD/build/lint-tools/bin:$PATH"
else
    echo "lint: note: build/lint-tools is missing, using tools from PATH, whose versions can differ from CI." >&2
    echo "lint: install the pinned ones with tools/lint-deps.sh" >&2
fi

missing=()
for t in clang-format clang-tidy shfmt shellcheck hadolint semgrep g++; do command -v "$t" >/dev/null || missing+=("$t"); done
if ((${#missing[@]})); then
    echo "lint: missing ${missing[*]}; see the top of tools/lint.sh" >&2
    exit 1
fi

failed=()
step() { # step NAME COMMAND...: run, remember failures, go on with the rest
    local name=$1
    shift
    echo "== $name"
    "$@" || failed+=("$name")
}

mapfile -t shell_files < <(git ls-files '*.sh' .githooks)
mapfile -t cpp_files < <(git ls-files '*.cpp' '*.h')
if [ "${1:-}" = --fix ]; then
    clang-format -i "${cpp_files[@]}"
    shfmt -w "${shell_files[@]}"
fi

# Formatting: .clang-format and .editorconfig (shfmt). Fix with: tools/lint.sh --fix
step clang-format clang-format --dry-run --Werror "${cpp_files[@]}"
step shfmt shfmt -d "${shell_files[@]}"
step shellcheck shellcheck "${shell_files[@]}"
step hadolint hadolint Containerfile

# clang-tidy reads compile_flags.txt, which points at the OpenVR header in build/sdk
run_clang_tidy() {
    tools/openvr-sdk.sh || return 1
    git ls-files 'src/*.cpp' 'tests/*.cpp' | xargs -P "$(nproc)" -n 1 clang-tidy --quiet 2>/dev/null
}
step clang-tidy run_clang_tidy

# Rulesets: Semgrep's default, Trail of Bits' and the CWE Top 25 (as in Trail of Bits' Testing Handbook), plus the
# workflow, Containerfile and secrets packs and the project rules. Semgrep's free engine treats C++ as experimental,
# so few registry rules reach src/; .semgrep.yml adds C++ rules for this code.
# semgrep-core needs a large stack (it crashes with SIGSEGV at the default 8 MB on some kernels).
# The registry's mutable-tag rule is excluded: this repo uses GitHub's own actions by major version on purpose.
run_semgrep() (
    ulimit -s unlimited
    semgrep scan --metrics=off --error --quiet \
        --config .semgrep.yml --config p/default --config p/trailofbits --config p/cwe-top-25 \
        --config p/github-actions --config p/dockerfile --config p/secrets \
        --exclude-rule yaml.github-actions.security.github-actions-mutable-action-tag.github-actions-mutable-action-tag
)
step semgrep run_semgrep

step tests tests/run.sh

if ((${#failed[@]})); then
    echo "lint: FAILED: ${failed[*]}" >&2
    exit 1
fi
echo "lint: all checks passed"
