#!/bin/bash
# Compile and run a throwaway C++17 program against the header-only DSP code.
# Build artifacts land in explore/ (gitignored), not /tmp.
#
# Usage:
#   dev-explore.sh [source.cpp] [--plugin NAME]... [-- extra clang args...]
#
#   source.cpp    program to build (default: explore/explore.cpp)
#   --plugin N    add plugins/N to the include path, so "<Name>Pedal.h" resolves. Repeatable.
#   everything after a literal -- is forwarded verbatim to clang++.
#
# The binary is written to explore/<basename> and then run.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_env.sh"

EXPLORE_DIR="$ROOT_DIR/explore"
mkdir -p "$EXPLORE_DIR"

SRC="$EXPLORE_DIR/explore.cpp"
PLUGIN_INCLUDES=()
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --plugin)
            shift
            [[ $# -gt 0 ]] || { echo "--plugin needs a name" >&2; exit 2; }
            PLUGIN_INCLUDES+=("-I$ROOT_DIR/plugins/$1")
            shift
            ;;
        --)
            shift
            EXTRA_ARGS+=("$@")
            break
            ;;
        *)
            SRC="$1"
            shift
            ;;
    esac
done

if [[ ! -f "$SRC" ]]; then
    echo "source not found: $SRC" >&2
    exit 1
fi

BIN="$EXPLORE_DIR/$(basename "${SRC%.*}")"

# -iquote (not -I) for the explore dir: it holds built binaries whose names can
# collide with standard headers; -iquote only satisfies "quoted" includes.
clang++ -std=c++17 -O2 -Wall -Wextra \
    -I"$ROOT_DIR" \
    -iquote "$EXPLORE_DIR" \
    ${PLUGIN_INCLUDES[@]+"${PLUGIN_INCLUDES[@]}"} \
    ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
    "$SRC" -o "$BIN"

"$BIN"
