#!/bin/bash
# Clean rebuild of build-tests/ and report every compiler warning in our own code.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$SCRIPT_DIR/dev-configure.sh"
fi
cmake --build "$BUILD_DIR" --target clean > /dev/null 2>&1 || true

LOG="$SCRATCH_DIR/warnings_full_build.log"
cmake --build "$BUILD_DIR" > "$LOG" 2>&1 || true

echo "--- warnings in own code ---"
grep -oE "^${ROOT_DIR}/[^:]+:[0-9]+:[0-9]+: warning: .*" "$LOG" \
    | grep -v -e "/third_party/" -e "/_deps/" | sort -u || echo "(none)"

echo
echo "--- errors ---"
grep -c "error:" "$LOG" || true

echo
echo "Full log: $LOG"
