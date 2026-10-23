#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/JaysTwinExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/jt_tonestack.txt" -o "$HERE/jt_tonestack.png" --width 700 --height 320 --cols 2 \
    --legendpos "lower right"
"$PYTHON" "$PLOT" -f "$DATA/jt_bright.txt" -o "$HERE/jt_bright.png" --width 900 --height 300 \
    --legendpos "upper left"
"$PYTHON" "$PLOT" -f "$DATA/jt_cabinet.txt" -o "$HERE/jt_cabinet.png" --width 900 --height 340 \
    --legendpos "lower center"
"$PYTHON" "$PLOT" -f "$DATA/jt_drive.txt" -o "$HERE/jt_drive.png" --width 600 --height 400 \
    --legendpos "lower right"
"$PYTHON" "$PLOT" -f "$DATA/jt_chain.txt" -o "$HERE/jt_chain.png" --width 900 --height 320 \
    --legendpos "lower center"
cat "$DATA/summary.txt"
