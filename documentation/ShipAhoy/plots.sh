#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/ShipAhoyExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/sa_preset.txt" -o "$HERE/sa_preset.png" --width 900 --height 260 --cols 1 \
    --legendpos "lower right"
"$PYTHON" "$PLOT" -f "$DATA/sa_random_types.txt" -o "$HERE/sa_random_types.png" \
    --labelx "time (s)" --labely "frequency (Hz)" --width 900 --height 160 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/sa_random_distribution.txt" -o "$HERE/sa_random_distribution.png" \
    --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/sa_glide.txt" -o "$HERE/sa_glide.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/sa_mix.txt" -o "$HERE/sa_mix.png" --width 700 --height 300
"$PYTHON" "$PLOT" -f "$DATA/sa_drift_time.txt" -o "$HERE/sa_drift_time.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/sa_drift_distribution.txt" -o "$HERE/sa_drift_distribution.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/sa_drift_correlation.txt" -o "$HERE/sa_drift_correlation.png" --width 900 --height 300
cat "$DATA/summary.txt"
