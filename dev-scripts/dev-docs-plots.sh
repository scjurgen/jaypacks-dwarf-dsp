#!/bin/bash
# Build the documentation explore programs, run them and render the charts.
#
# Usage:
#   dev-docs-plots.sh [folder]...
#
#   folder   documentation subfolder with a plots.sh, e.g. OrnsteinUhlenbeck or
#            Filters/PoleMixing; default: every folder that has one.
#
# Builds into build-docs/ (Release, JP_BUILD_DOCS=ON, no tests), creates or updates the
# repository venv .venv (dev-venv.sh, needs network once), then runs <folder>/plots.sh with
# the build directory and the venv's python. Plot data goes
# to <folder>/generated/ (gitignored); the PNGs next to the README are the checked-in result.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

DOCS_DIR="$ROOT_DIR/documentation"
DOCS_BUILD_DIR="$ROOT_DIR/build-docs"

FOLDERS=("$@")
if [[ ${#FOLDERS[@]} -eq 0 ]]; then
    while IFS= read -r script; do
        folder="${script#"$DOCS_DIR"/}"
        FOLDERS+=("${folder%/plots.sh}")
    done < <(find "$DOCS_DIR" -name plots.sh | sort)
fi

if [[ ! -f "$DOCS_BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -S "$ROOT_DIR" -B "$DOCS_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DJP_BUILD_DOCS=ON -DJP_BUILD_TESTS=OFF -DJP_BUILD_PLUGINS=OFF >"$SCRATCH_DIR/docs_configure.log"
fi
cmake --build "$DOCS_BUILD_DIR" --target docs_explore

PYTHON="$("$SCRIPT_DIR/dev-venv.sh" | tail -n 1)"

for folder in "${FOLDERS[@]}"; do
    plots="$DOCS_DIR/$folder/plots.sh"
    if [[ ! -f "$plots" ]]; then
        echo "no plots.sh in documentation/$folder" >&2
        exit 1
    fi
    echo "=== documentation/$folder"
    mkdir -p "$DOCS_DIR/$folder/generated"
    bash "$plots" "$DOCS_BUILD_DIR" "$PYTHON"
done
