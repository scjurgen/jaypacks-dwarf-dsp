#!/bin/bash
# Renders the charts of this folder. Called by dev-scripts/dev-docs-plots.sh with the docs
# build directory and the venv python; run that script instead of this one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$1"
PYTHON="$2"
PLOT="$HERE/../../Plot/PyConPlot.py"
DATA="$HERE/generated"

"$BUILD_DIR/documentation/PoleMixingExplore" "$DATA"

"$PYTHON" "$PLOT" -f "$DATA/pm_menu_response.txt" -o "$HERE/pm_menu_response.png" \
    --labelx "frequency (Hz)" --labely "magnitude (dB)" --width 700 --height 320 --cols 2 --legendpos "lower left"
"$PYTHON" "$PLOT" -f "$DATA/pm_phaser_phase.txt" -o "$HERE/pm_phaser_phase.png" \
    --labelx "frequency (Hz)" --labely "phase (degrees)" --width 900 --height 320
"$PYTHON" "$PLOT" -f "$DATA/pm_menu_mapping.txt" -o "$HERE/pm_menu_mapping.png" \
    --labelx "frequency (Hz)" --labely "magnitude (dB)" --width 700 --height 300 --cols 2 --legendpos "lower left"
"$PYTHON" "$PLOT" -f "$DATA/pm_stage.txt" -o "$HERE/pm_stage.png" --width 900 --height 300 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/pm_threshold.txt" -o "$HERE/pm_threshold.png" --width 900 --height 320 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/pm_resonance.txt" -o "$HERE/pm_resonance.png" --width 900 --height 300 --cols 1
"$PYTHON" "$PLOT" -f "$DATA/pm_glide.txt" -o "$HERE/pm_glide.png" --width 900 --height 300
"$PYTHON" "$PLOT" -f "$DATA/pm_clamp.txt" -o "$HERE/pm_clamp.png" --width 900 --height 300 --cols 1
cat "$DATA/summary.txt"
