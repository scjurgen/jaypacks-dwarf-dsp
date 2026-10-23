#!/usr/bin/env bash
# Recreate the modgui images (face, knob filmstrip, footswitch, screenshot, thumbnail)
# of every plugin in plugins/*/<name>.json with generator/graphics.py.
#
# Usage:
#   generator/render-graphics.sh [-o OUT_DIR] [plugin...]
#
# Without plugin names all plugins are rendered. Images go to
# OUT_DIR/<name>/modgui/ (default: build-graphics/ in the repository, gitignored).
# Uses .venv (dev-scripts/dev-venv.sh) when it exists, else python3 from the PATH.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
OUT_DIR="$REPO_DIR/build-graphics"

usage()
{
    sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while getopts "o:h" opt; do
    case "$opt" in
        o) OUT_DIR="$OPTARG" ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

PYTHON="$REPO_DIR/.venv/bin/python"
if [ ! -x "$PYTHON" ]; then
    PYTHON="python3"
fi

if [ "$#" -gt 0 ]; then
    names=("$@")
else
    names=()
    for json in "$REPO_DIR"/plugins/*/*.json; do
        dir="$(basename "$(dirname "$json")")"
        if [ "$(basename "$json" .json)" = "$dir" ]; then
            names+=("$dir")
        fi
    done
fi

failed=0
for name in "${names[@]}"; do
    json="$REPO_DIR/plugins/$name/$name.json"
    if [ ! -f "$json" ]; then
        echo "render-graphics: no such plugin: $name" >&2
        failed=1
        continue
    fi
    echo "render-graphics: $name"
    if ! "$PYTHON" "$SCRIPT_DIR/graphics.py" "$json" --bundle-dir "$OUT_DIR/$name"; then
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "render-graphics: some plugins failed" >&2
    exit 1
fi
echo "render-graphics: done, images in $OUT_DIR"
