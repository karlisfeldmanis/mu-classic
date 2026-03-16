#!/bin/bash
# Visual Regression Test Script for MU Remaster BGFX Migration
# Captures screenshots at fixed camera positions and compares against baselines.
#
# Usage:
#   ./regression_test.sh baseline   # Capture GL baseline screenshots
#   ./regression_test.sh test       # Capture current screenshots and compare
#   ./regression_test.sh compare    # Compare existing captures against baselines
#
# Requirements: ImageMagick (brew install imagemagick)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASELINE_DIR="$SCRIPT_DIR/baselines"
CURRENT_DIR="$SCRIPT_DIR/current"
DIFF_DIR="$SCRIPT_DIR/diffs"
BUILD_DIR="$SCRIPT_DIR/../build"
EXECUTABLE="$BUILD_DIR/MuRemaster"
THRESHOLD="0.05"  # 5% RMSE threshold

# Test scenes: name, x, y, z positions
declare -a SCENES=(
    "lorencia_center:13500:200:13500"
    "torch_lighting:13200:200:13800"
    "indoor_building:13000:200:14200"
    "hero_equipped:12800:200:12800"
    "monster_area:14500:200:12000"
    "vfx_area:12800:200:12900"
    "npc_merchants:13300:200:13400"
    "sky_view:12800:500:12800"
)

usage() {
    echo "Usage: $0 {baseline|test|compare|benchmark}"
    echo ""
    echo "  baseline   - Capture reference screenshots (run with GL build before migration)"
    echo "  test       - Capture current screenshots and compare against baselines"
    echo "  compare    - Compare existing current/ against baselines/ (no capture)"
    echo "  benchmark  - Run performance benchmark at fixed camera position"
    exit 1
}

check_deps() {
    if ! command -v compare &>/dev/null; then
        echo "ERROR: ImageMagick not found. Install with: brew install imagemagick"
        exit 1
    fi
    if [ ! -f "$EXECUTABLE" ]; then
        echo "ERROR: Game executable not found at $EXECUTABLE"
        echo "Build first: cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja"
        exit 1
    fi
}

capture_screenshots() {
    local output_dir="$1"
    local label="$2"
    mkdir -p "$output_dir"

    echo "=== Capturing $label screenshots ==="
    for scene_spec in "${SCENES[@]}"; do
        IFS=':' read -r name x y z <<< "$scene_spec"
        echo "  Capturing: $name (pos=$x,$y,$z)"
        "$EXECUTABLE" --screenshot --pos "$x" "$y" "$z" --output "$output_dir/$name" 2>/dev/null || {
            echo "    WARNING: Failed to capture $name"
        }
    done
    echo "  Saved to: $output_dir/"
}

compare_screenshots() {
    echo "=== Comparing screenshots ==="
    mkdir -p "$DIFF_DIR"

    local pass=0
    local fail=0
    local skip=0
    local results=()

    for scene_spec in "${SCENES[@]}"; do
        IFS=':' read -r name x y z <<< "$scene_spec"
        local baseline="$BASELINE_DIR/${name}.jpg"
        local current="$CURRENT_DIR/${name}.jpg"
        local diff="$DIFF_DIR/${name}_diff.jpg"

        if [ ! -f "$baseline" ]; then
            echo "  SKIP: $name (no baseline)"
            skip=$((skip + 1))
            continue
        fi
        if [ ! -f "$current" ]; then
            echo "  SKIP: $name (no current capture)"
            skip=$((skip + 1))
            continue
        fi

        # Compare using RMSE (Root Mean Square Error)
        local rmse
        rmse=$(compare -metric RMSE "$current" "$baseline" "$diff" 2>&1 | awk -F'[()]' '{print $2}') || true

        if [ -z "$rmse" ]; then
            echo "  ERROR: $name (comparison failed)"
            fail=$((fail + 1))
            continue
        fi

        # Check if RMSE is within threshold
        local passed
        passed=$(echo "$rmse $THRESHOLD" | awk '{print ($1 <= $2) ? "1" : "0"}')

        if [ "$passed" = "1" ]; then
            echo "  PASS: $name (RMSE=$rmse)"
            pass=$((pass + 1))
        else
            echo "  FAIL: $name (RMSE=$rmse > threshold=$THRESHOLD)"
            fail=$((fail + 1))
            # Create side-by-side comparison
            montage "$baseline" "$current" "$diff" -tile 3x1 -geometry +2+2 \
                -label "Baseline" -label "Current" -label "Diff" \
                "$DIFF_DIR/${name}_comparison.jpg" 2>/dev/null || true
        fi
        results+=("$name:$rmse:$passed")
    done

    echo ""
    echo "=== Results ==="
    echo "  PASS: $pass | FAIL: $fail | SKIP: $skip"
    echo "  Diff images saved to: $DIFF_DIR/"

    if [ "$fail" -gt 0 ]; then
        echo ""
        echo "Failed scenes:"
        for r in "${results[@]}"; do
            IFS=':' read -r rname rrmse rpassed <<< "$r"
            if [ "$rpassed" = "0" ]; then
                echo "  - $rname (RMSE=$rrmse) -> $DIFF_DIR/${rname}_comparison.jpg"
            fi
        done
        return 1
    fi
    return 0
}

run_benchmark() {
    echo "=== Performance Benchmark ==="
    echo "  Running 300 frames at fixed camera position..."
    "$EXECUTABLE" --benchmark --pos 13500 200 13500 2>&1 || {
        echo "  NOTE: --benchmark flag not yet implemented"
        echo "  This will be added during the BGFX migration."
    }
}

# Main
case "${1:-}" in
    baseline)
        check_deps
        capture_screenshots "$BASELINE_DIR" "baseline"
        echo ""
        echo "Baseline screenshots captured. These will be used as reference."
        echo "Run '$0 test' after making changes to compare."
        ;;
    test)
        check_deps
        capture_screenshots "$CURRENT_DIR" "current"
        compare_screenshots
        ;;
    compare)
        compare_screenshots
        ;;
    benchmark)
        check_deps
        run_benchmark
        ;;
    *)
        usage
        ;;
esac
