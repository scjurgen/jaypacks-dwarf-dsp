#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/EigenghostExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/eg_impulse.txt" -o "$HERE/eg_impulse.png" --width 900 --height 180 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/eg_density.txt" -o "$HERE/eg_density.png" --width 900 --height 260 \
    --legendpos "lower right"
"$PYTHON" "$PLOT" -f "$DATA/eg_decay.txt" -o "$HERE/eg_decay.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/eg_damping.txt" -o "$HERE/eg_damping.png" --width 900 --height 300 \
    --legendpos "lower left"
"$PYTHON" "$PLOT" -f "$DATA/eg_shimmer.txt" -o "$HERE/eg_shimmer.png" --width 900 --height 220 --cols 1 \
    --legendpos "upper right"
"$PYTHON" "$PLOT" -f "$DATA/eg_size_wave.txt" -o "$HERE/eg_size_wave.png" --width 900 --height 180 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/eg_size_band.txt" -o "$HERE/eg_size_band.png" --width 900 --height 240
"$PYTHON" "$PLOT" -f "$DATA/eg_lengths.txt" -o "$HERE/eg_lengths.png" --width 700 --height 300 \
    --legendpos "upper left"
"$PYTHON" "$PLOT" -f "$DATA/eg_mod_pitch.txt" -o "$HERE/eg_mod_pitch.png" --width 900 --height 240 \
    --legendpos "upper right"
"$PYTHON" "$PLOT" -f "$DATA/eg_mod_tail.txt" -o "$HERE/eg_mod_tail.png" --width 900 --height 300 \
    --legendpos "upper right"
cat "$DATA/summary.txt"
