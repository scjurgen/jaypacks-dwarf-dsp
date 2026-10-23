#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/AnalysisExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/an_frequency.txt" -o "$HERE/an_frequency.png" \
    --labelx "frequency (Hz)" --width 900 --height 240 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/an_goertzel.txt" -o "$HERE/an_goertzel.png" \
    --width 900 --height 260 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/an_tonefit.txt" -o "$HERE/an_tonefit.png" \
    --width 900 --height 260 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/an_transfer_blocks.txt" -o "$HERE/an_transfer_blocks.png" \
    --labelx "frequency (Hz)" --width 900 --height 260 --cols 1
cat "$DATA/summary.txt"
