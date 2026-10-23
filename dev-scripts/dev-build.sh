#!/bin/bash
# Build one or more CMake targets in build-tests/ (configures first if needed).
# Usage: dev-build.sh [target...]   (defaults to all)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$SCRIPT_DIR/dev-configure.sh"
fi

TARGETS=("${@:-all}")

LOG="$SCRATCH_DIR/build.log"
if ! cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" > "$LOG" 2>&1; then
    echo "BUILD FAILED. Last 60 lines:"
    tail -n 60 "$LOG"
    echo "Full log: $LOG"
    exit 1
fi

echo "--- warnings/errors (own code only) ---"
grep -oE "^${ROOT_DIR}/[^:]+:[0-9]+:[0-9]+: (warning|error): .*" "$LOG" \
    | grep -v -e "/third_party/" -e "/_deps/" | sort -u || echo "(none)"

echo "Full log: $LOG"
