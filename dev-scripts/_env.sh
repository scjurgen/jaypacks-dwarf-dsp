#!/bin/bash
# Shared paths for dev-scripts/*.sh. Source this, don't run it directly.
# Adapted from AbacDsp dev-scripts/_env.sh (see docs/references.md).
set -euo pipefail

DEV_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$DEV_SCRIPTS_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-tests"
SCRATCH_DIR="$BUILD_DIR/scratch"

mkdir -p "$SCRATCH_DIR"
