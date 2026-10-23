#!/bin/bash
# Configure the test build directory (build-tests/). Safe to re-run; CMake reconfigures in place.
# Creates or updates the venv .venv first (dev-venv.sh), which the modgui images need.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

"$SCRIPT_DIR/dev-venv.sh" >/dev/null

LOG="$SCRATCH_DIR/configure.log"
if ! cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DJP_BUILD_TESTS=ON \
    > "$LOG" 2>&1; then
    echo "CONFIGURE FAILED. Last 40 lines:"
    tail -n 40 "$LOG"
    echo "Full log: $LOG"
    exit 1
fi

tail -n 5 "$LOG"
echo "Full log: $LOG"
