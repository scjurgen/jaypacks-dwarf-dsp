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

"$BUILD_DIR/documentation/WavetablesExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/wt_shapes.txt" -o "$HERE/wt_shapes.png" \
    --labelx "phase (cycles)" --labely "level" --width 900 --height 180 --cols 2
"$PYTHON" "$PLOT" -f "$DATA/wt_levels.txt" -o "$HERE/wt_levels.png" \
    --width 900 --height 320 --cols 1 --logx --legendpos "lower left"
"$PYTHON" "$PLOT" -f "$DATA/wt_aliasing.txt" -o "$HERE/wt_aliasing.png" \
    --width 900 --height 300 --cols 1 --logx --hzticks --legendpos "center right"
"$PYTHON" "$PLOT" -f "$DATA/wt_bend.txt" -o "$HERE/wt_bend.png" \
    --width 900 --height 260 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/wt_pwm.txt" -o "$HERE/wt_pwm.png" \
    --width 900 --height 280 --cols 1
cat "$DATA/summary.txt"
