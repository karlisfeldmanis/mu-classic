#!/bin/bash
# Upscale seamless tiling textures without breaking tile seams
# Method: tile 2x2 → upscale → crop center = perfectly seamless result
#
# Usage: cd client/build && bash ../../tools/upscale_tiles.sh [world|all] [force]
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

upscale_tile() {
  local src_path="$1" dst_path="$2"
  local name=$(basename "${src_path%.*}")

  # Skip data textures
  case "$name" in
    TerrainLight*|TerrainHeight*|mini_map*|map1*|rain*|leaf*|angeflo*) return ;;
    TileWater*) return ;;  # water color is correct at original
  esac

  if [ -f "$dst_path" ] && [ "$FORCE" != "force" ]; then
    skip=$((skip + 1))
    return
  fi

  # Extract JPEG from OZJ
  python3 -c "
data=open('$src_path','rb').read()
off=24 if len(data)>24 and data[24]==0xFF and data[25]==0xD8 else 0
if data[off]!=0xFF: exit(1)
open('$TMPDIR/${name}.jpg','wb').write(data[off:])
" 2>/dev/null || { fail=$((fail+1)); return; }

  # Tile 2x2, upscale, crop center
  python3 << PYEOF
from PIL import Image

img = Image.open('$TMPDIR/${name}.jpg')
w, h = img.size

# Skip tiny textures
if w < 64 or h < 64:
    exit(2)

# Tile 2x2
tiled = Image.new('RGB', (w*2, h*2))
tiled.paste(img, (0, 0))
tiled.paste(img, (w, 0))
tiled.paste(img, (0, h))
tiled.paste(img, (w, h))
tiled.save('$TMPDIR/${name}_tiled.png')
PYEOF

  local rc=$?
  if [ $rc -eq 2 ]; then skip=$((skip+1)); return; fi
  if [ $rc -ne 0 ]; then fail=$((fail+1)); return; fi

  # Upscale the tiled version
  "$ESRGAN" -i "$TMPDIR/${name}_tiled.png" -o "$TMPDIR/${name}_up.png" \
    -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" > /dev/null 2>&1 || { fail=$((fail+1)); return; }

  # Crop center tile (same size as single upscaled tile)
  python3 << PYEOF
from PIL import Image

up = Image.open('$TMPDIR/${name}_up.png')
uw, uh = up.size
# Center tile = quarter of total, at offset (uw/4, uh/4)
tw, th = uw // 2, uh // 2
cropped = up.crop((tw // 2, th // 2, tw // 2 + tw, th // 2 + th))
cropped.save('$dst_path')
PYEOF

  if [ $? -eq 0 ]; then
    count=$((count + 1))
  else
    fail=$((fail + 1))
  fi
  rm -f "$TMPDIR/${name}"*
}

echo "═══════════════════════════════════════════════════"
echo "  Seamless Tile Upscaler (${SCALE}x, tile-2x2-crop)"
echo "═══════════════════════════════════════════════════"

process_world() {
  local world_dir="$1"
  [ -d "$world_dir" ] || return
  local before=$count
  for f in "$world_dir"/Tile*.OZJ; do
    [ -f "$f" ] || continue
    local base=$(basename "$f" .OZJ)
    upscale_tile "$f" "$world_dir/${base}.png"
  done
  echo "  $world_dir: $((count - before)) upscaled"
}

case "$TARGET" in
  all)
    for w in World1 World2 World3 World4 World5; do
      process_world "Data/$w"
    done
    ;;
  *)
    process_world "Data/$TARGET"
    ;;
esac

echo ""
echo "═══════════════════════════════════════════════════"
echo "  Done: $count upscaled, $skip skipped, $fail failed"
echo "═══════════════════════════════════════════════════"
