#!/bin/bash
# upscale_textures.sh — Batch upscale MU Online OZJ/OZT textures using Real-ESRGAN
#
# Usage:
#   ./tools/upscale_textures.sh <input_dir> <output_dir> [scale] [model]
#
# Examples:
#   ./tools/upscale_textures.sh client/Data/Monster client/data_glb/Monster
#   ./tools/upscale_textures.sh client/Data/Player client/data_glb/Player 2
#   ./tools/upscale_textures.sh client/Data/NPC client/data_glb/NPC 4 realesrgan-x4plus
#
# Supports: OZJ (JPEG with 24-byte header), OZT (TGA with 4-byte header), JPG, PNG
# Output: PNG files in the output directory

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESRGAN="$SCRIPT_DIR/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR="$SCRIPT_DIR/realesrgan/models"

INPUT_DIR="${1:?Usage: $0 <input_dir> <output_dir> [scale] [model]}"
OUTPUT_DIR="${2:?Usage: $0 <input_dir> <output_dir> [scale] [model]}"
SCALE="${3:-4}"
MODEL="${4:-realesrgan-x4plus}"

if [ ! -x "$ESRGAN" ]; then
    echo "Error: Real-ESRGAN binary not found at $ESRGAN"
    echo "Download from: https://github.com/xinntao/Real-ESRGAN/releases"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

TMPDIR_WORK="$(mktemp -d)"
trap "rm -rf '$TMPDIR_WORK'" EXIT

count=0
skipped=0
failed=0

echo "=== MU Online Texture Upscaler ==="
echo "Input:  $INPUT_DIR"
echo "Output: $OUTPUT_DIR"
echo "Scale:  ${SCALE}x"
echo "Model:  $MODEL"
echo ""

for file in "$INPUT_DIR"/*; do
    [ -f "$file" ] || continue

    base="$(basename "$file")"
    ext="${base##*.}"
    name="${base%.*}"
    ext_lower="$(echo "$ext" | tr '[:upper:]' '[:lower:]')"

    tmp_input=""

    case "$ext_lower" in
        ozj)
            # OZJ = 24-byte header + JPEG data
            tmp_input="$TMPDIR_WORK/${name}.jpg"
            dd if="$file" of="$tmp_input" bs=1 skip=24 2>/dev/null
            ;;
        ozt)
            # OZT = 4-byte header + TGA data → convert to PNG first
            tmp_tga="$TMPDIR_WORK/${name}.tga"
            dd if="$file" of="$tmp_tga" bs=1 skip=4 2>/dev/null
            tmp_input="$TMPDIR_WORK/${name}.png"
            sips -s format png "$tmp_tga" --out "$tmp_input" >/dev/null 2>&1 || {
                echo "  SKIP $base (TGA conversion failed)"
                ((skipped++))
                continue
            }
            ;;
        jpg|jpeg|png)
            tmp_input="$file"
            ;;
        bmd|txt|dat|wav|mp3|ogg|bmd)
            # Skip non-texture files silently
            continue
            ;;
        *)
            continue
            ;;
    esac

    if [ -z "$tmp_input" ] || [ ! -f "$tmp_input" ]; then
        continue
    fi

    output_file="$OUTPUT_DIR/${name}.png"

    if [ -f "$output_file" ]; then
        echo "  EXIST $base → ${name}.png (skipping)"
        ((skipped++))
        continue
    fi

    echo -n "  UP    $base → ${name}.png ... "

    if "$ESRGAN" -i "$tmp_input" -o "$output_file" -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" >/dev/null 2>&1; then
        # Get dimensions
        dims=$(sips -g pixelWidth -g pixelHeight "$output_file" 2>/dev/null | grep pixel | awk '{print $2}' | tr '\n' 'x' | sed 's/x$//')
        echo "done (${dims})"
        ((count++))
    else
        echo "FAILED"
        ((failed++))
    fi
done

echo ""
echo "=== Done ==="
echo "Upscaled: $count"
echo "Skipped:  $skipped"
echo "Failed:   $failed"
