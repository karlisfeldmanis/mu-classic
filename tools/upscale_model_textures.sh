#!/bin/bash
# Upscale 3D model textures (OZJ/OZT) in-place as PNG using anime model
# These are hand-painted game textures — anime model preserves flat colors + edges
# Usage: cd client/build && bash ../../tools/upscale_model_textures.sh [dir] [force]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESRGAN="$SCRIPT_DIR/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR="$SCRIPT_DIR/realesrgan/models"
SCALE=2
MODEL="realesrgan-x4plus"

TARGET="${1:-all}"
FORCE="${2:-}"
TMPDIR="$(mktemp -d)"
trap "rm -rf '$TMPDIR'" EXIT

count=0
skip=0
fail=0

upscale_dir() {
  local dir="$1"
  local local_count=0
  local local_skip=0
  local local_fail=0

  [ -d "$dir" ] || return

  for f in "$dir"/*.OZJ "$dir"/*.ozj "$dir"/*.OZT "$dir"/*.ozt; do
    [ -f "$f" ] || continue
    local base=$(basename "$f")
    local name="${base%.*}"
    local ext="${base##*.}"
    local ext_lower=$(echo "$ext" | tr '[:upper:]' '[:lower:]')
    local dst="$dir/${name}.png"

    # Skip if PNG exists already
    if [ -f "$dst" ] && [ "$FORCE" != "force" ]; then
      local_skip=$((local_skip + 1))
      continue
    fi

    # Extract to temp
    local input=""
    case "$ext_lower" in
      ozj)
        python3 -c "
data=open('$f','rb').read()
off=24 if len(data)>24 and data[24]==0xFF and data[25]==0xD8 else 0
if data[off]!=0xFF: exit(1)
open('$TMPDIR/${name}.jpg','wb').write(data[off:])
" 2>/dev/null || { local_fail=$((local_fail+1)); continue; }
        input="$TMPDIR/${name}.jpg"
        ;;
      ozt)
        dd if="$f" of="$TMPDIR/${name}.tga" bs=1 skip=4 2>/dev/null
        sips -s format png "$TMPDIR/${name}.tga" --out "$TMPDIR/${name}_in.png" > /dev/null 2>&1 || { local_fail=$((local_fail+1)); continue; }
        input="$TMPDIR/${name}_in.png"
        ;;
      *) continue ;;
    esac

    [ -f "$dst" ] && rm "$dst"
    if "$ESRGAN" -i "$input" -o "$dst" -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" > /dev/null 2>&1; then
      local_count=$((local_count + 1))
    else
      local_fail=$((local_fail + 1))
    fi
    # Clean temp
    rm -f "$TMPDIR/${name}".*
  done

  echo "  $dir: $local_count upscaled, $local_skip skipped, $local_fail failed"
  count=$((count + local_count))
  skip=$((skip + local_skip))
  fail=$((fail + local_fail))
}

echo "═══════════════════════════════════════════════════"
echo "  Model Texture Upscaler (${SCALE}x ${MODEL})"
echo "═══════════════════════════════════════════════════"

if [ "$TARGET" = "all" ] || [ "$TARGET" = "item" ]; then
  echo "── Item (weapons) ──"
  upscale_dir "Data/Item"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "player" ]; then
  echo "── Player (armor) ──"
  upscale_dir "Data/Player"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "monster" ]; then
  echo "── Monster ──"
  upscale_dir "Data/Monster"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "object" ]; then
  echo "── World Objects ──"
  upscale_dir "Data/Object1"
  upscale_dir "Data/Object2"
  upscale_dir "Data/Object3"
  upscale_dir "Data/Object4"
  upscale_dir "Data/Object5"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "skill" ]; then
  echo "── Skill ──"
  upscale_dir "Data/Skill"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "effect" ]; then
  echo "── Effect ──"
  upscale_dir "Data/Effect"
fi

if [ "$TARGET" = "all" ] || [ "$TARGET" = "npc" ]; then
  echo "── NPC ──"
  upscale_dir "Data/NPC"
fi

echo ""
echo "═══════════════════════════════════════════════════"
echo "  Done: $count upscaled, $skip skipped, $fail failed"
echo "═══════════════════════════════════════════════════"
