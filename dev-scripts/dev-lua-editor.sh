#!/bin/bash
# Start lua-editor on the repo's scripts (plugins/<plugin>/scripts, shown as repo/) and on the Dwarf's
# (192.168.51.1, /data/jaypacks/scripts, shown as device/, connected on first use). Sets up .venv and the editor's
# packages first when they are missing or lua-editor/requirements.txt changed.
#
# Usage:
#   dev-lua-editor.sh            repo + device: edit in repo/, push to device/ (Ctrl+U), F1 opens the API reference
#   dev-lua-editor.sh --local    repo only, never connects to the device
#   dev-lua-editor.sh [options]  anything else is passed to lua-editor (see its --help)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

EDITOR_DIR="$ROOT_DIR/lua-editor"
PYTHON="$("$SCRIPT_DIR/dev-venv.sh" | tail -n 1)"
REQUIREMENTS="$EDITOR_DIR/requirements.txt"
INSTALLED="$ROOT_DIR/.venv/lua-editor-requirements.installed"

if ! cmp -s "$REQUIREMENTS" "$INSTALLED" 2>/dev/null; then
    LOG="$SCRATCH_DIR/lua-editor-pip.log"
    if ! { "$PYTHON" -m pip install -r "$REQUIREMENTS" && "$PYTHON" -m pip install --no-deps -e "$EDITOR_DIR"; } >"$LOG" 2>&1; then
        echo "pip install failed. Last 20 lines:" >&2
        tail -n 20 "$LOG" >&2
        echo "Full log: $LOG" >&2
        exit 1
    fi
    cp "$REQUIREMENTS" "$INSTALLED"
fi

if [[ "${1:-}" == "--local" ]]; then
    shift
    set -- --no-device "$@"
fi
exec "$PYTHON" -m lua_editor --repo "$ROOT_DIR" --apis "$EDITOR_DIR/apis" "$@"
