#!/usr/bin/env bash
# Upscale all armor set textures using smart_supersample.py
# Excludes: already-done sets (02/03), character skin, hair, test textures, light/glow
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLAYER_DIR="$SCRIPT_DIR/../client/Data/Player"
SS="$SCRIPT_DIR/smart_supersample.py"

cd "$PLAYER_DIR"

# Build file list: armor piece textures only
files=()
for f in $(ls *.OZJ *.OZT *.ozj *.ozt 2>/dev/null | \
    grep -iE '^(upper_|lower_|lower2_|head_|boots_|boot_|gloves_|SkinClass|Skinclass)' | \
    grep -ivE '_(02|03)[._b]|_(02|03)\.' | \
    grep -ivE '(_E1|_bd|_bld|_s\.|_a0[1-6]|Test|head_N|headhair|_R\.|_wand|_hair)' | \
    sort); do
    files+=("$PLAYER_DIR/$f")
done

total=${#files[@]}
echo "=== Smart Supersample: $total armor textures ==="
echo "  Pipeline: analyze -> denoise(if needed) -> ESRGAN 2x -> histogram match -> sharpen -> downscale"
echo ""

ok=0; skip=0; fail=0
for i in "${!files[@]}"; do
    f="${files[$i]}"
    name=$(basename "$f")
    n=$((i + 1))
    echo -n "[$n/$total] $name ... "
    result=$(python3 "$SS" "$f" 2>&1 | grep -oE '(supersampled|skipped|failed)' | head -1)
    case "$result" in
        *supersampled*) echo "OK"; ((ok++)) ;;
        *skipped*) echo "skip"; ((skip++)) ;;
        *) echo "FAIL"; ((fail++)) ;;
    esac
done

echo ""
echo "=== Done: $ok supersampled, $skip skipped, $fail failed ==="
