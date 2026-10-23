#!/bin/bash
# Create or update the repository venv .venv from requirements.txt (needs network on first use
# and after requirements.txt changes). Used by dev-configure.sh, dev-docs-plots.sh and
# tools/jp.py; CMake takes the Python for generator/graphics.py from here.
#
# Usage:
#   dev-venv.sh        prints the venv's python on the last line
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

VENV_DIR="$ROOT_DIR/.venv"
REQUIREMENTS="$ROOT_DIR/requirements.txt"
INSTALLED="$VENV_DIR/requirements.installed"

if [[ ! -x "$VENV_DIR/bin/python3" ]]; then
    python3 -m venv "$VENV_DIR"
fi
if ! cmp -s "$REQUIREMENTS" "$INSTALLED"; then
    LOG="$SCRATCH_DIR/venv.log"
    if ! { "$VENV_DIR/bin/pip" install --upgrade pip && "$VENV_DIR/bin/pip" install -r "$REQUIREMENTS"; } >"$LOG" 2>&1; then
        echo "pip install failed. Last 20 lines:" >&2
        tail -n 20 "$LOG" >&2
        echo "Full log: $LOG" >&2
        exit 1
    fi
    cp "$REQUIREMENTS" "$INSTALLED"
fi
echo "$VENV_DIR/bin/python3"
