#!/bin/bash
# Builds everything (unit tests, plugins, host tests) and runs ctest.
# Usage: dev-test.sh [ctest-regex]   (regex filters test names, e.g. "PoleMixing|host\\.")
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

REGEX="${1:-}"

"$SCRIPT_DIR/dev-build.sh"

if [ -n "$REGEX" ]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$REGEX"
else
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
