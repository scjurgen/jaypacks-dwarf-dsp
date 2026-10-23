#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/OrnsteinUhlenbeckExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/ou_timeline.txt" -o "$HERE/ou_timeline.png" \
    --labelx "time (s)" --labely "x - mu" --width 1400 --height 160 --cols 1 --miny -0.2 --maxy 0.2
"$PYTHON" "$PLOT" -f "$DATA/ou_autocorr.txt" -o "$HERE/ou_autocorr.png" \
    --labelx "lag tau (s)" --labely "ACF" --width 900 --height 200 --cols 1 --miny -0.5 --maxy 1.05
"$PYTHON" "$PLOT" -f "$DATA/ou_distribution.txt" -o "$HERE/ou_distribution.png" \
    --labelx "deviation from mu" --labely "density" --width 900 --height 220 --cols 2
"$PYTHON" "$PLOT" -f "$DATA/ou_step_methods.txt" -o "$HERE/ou_step_methods.png" \
    --labelx "step" --labely "x - mu" --width 900 --height 220 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/ou_shipahoy.txt" -o "$HERE/ou_shipahoy.png" \
    --width 900 --height 240 --cols 1
cat "$DATA/summary.txt"
