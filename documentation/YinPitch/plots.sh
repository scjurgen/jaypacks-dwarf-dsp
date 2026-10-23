#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"
mkdir -p "$DATA"

"$BUILD_DIR/documentation/YinPitchExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/yin_cmndf.txt" -o "$HERE/yin_cmndf.png" --width 900 --height 170 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/yin_error.txt" -o "$HERE/yin_error.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/yin_step.txt" -o "$HERE/yin_step.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/yin_vibrato.txt" -o "$HERE/yin_vibrato.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/yin_noise.txt" -o "$HERE/yin_noise.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/yin_tracker_pitch.txt" -o "$HERE/yin_tracker_pitch.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/yin_tracker_level.txt" -o "$HERE/yin_tracker_level.png" --width 900 --height 200
cat "$DATA/summary.txt"
