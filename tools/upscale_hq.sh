#!/bin/bash
# High-quality texture upscaler with per-category model selection
# - UI/icons: anime model (clean lines, no hallucination)
# - Terrain: 2-pass (2x → 2x) for better structure preservation
# - 3D model textures: anime model (hand-painted game art)
#
# Usage: cd client/build && bash ../../tools/upscale_hq.sh [category]
# Categories: ui, terrain, skill, all
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESRGAN="$SCRIPT_DIR/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR="$SCRIPT_DIR/realesrgan/models"
TMPDIR="$(mktemp -d)"
trap "rm -rf '$TMPDIR'" EXIT

CATEGORY="${1:-all}"
FORCE="${2:-}" # pass "force" to overwrite existing PNGs

upscale_anime_4x() {
  local input="$1" output="$2"
  "$ESRGAN" -i "$input" -o "$output" -s 4 -n realesrgan-x4plus-anime -m "$MODEL_DIR" > /dev/null 2>&1
}

upscale_2pass() {
  # 2x → 2x = 4x total, better structure preservation
  local input="$1" output="$2"
  local mid="$TMPDIR/mid_$(basename "$input")"
  "$ESRGAN" -i "$input" -o "$mid" -s 2 -n realesr-animevideov3 -m "$MODEL_DIR" > /dev/null 2>&1
  "$ESRGAN" -i "$mid" -o "$output" -s 2 -n realesr-animevideov3 -m "$MODEL_DIR" > /dev/null 2>&1
  rm -f "$mid"
}

upscale_anime_8x() {
  # 4x anime → 2x general = 8x total (for tiny icons)
  local input="$1" output="$2"
  local mid="$TMPDIR/mid8_$(basename "$input")"
  "$ESRGAN" -i "$input" -o "$mid" -s 4 -n realesrgan-x4plus-anime -m "$MODEL_DIR" > /dev/null 2>&1
  "$ESRGAN" -i "$mid" -o "$output" -s 2 -n realesr-animevideov3 -m "$MODEL_DIR" > /dev/null 2>&1
  rm -f "$mid"
}

extract_ozj() {
  # Extract JPEG from OZJ (auto-detect header offset)
  local ozj="$1" output="$2"
  python3 -c "
data = open('$ozj','rb').read()
off = 0
if len(data) > 24 and data[24] == 0xFF and data[25] == 0xD8:
    off = 24
elif data[0] == 0xFF and data[1] == 0xD8:
    off = 0
else:
    exit(1)
open('$output','wb').write(data[off:])
"
}

count=0
skip=0
fail=0

do_upscale() {
  local src="$1" dst="$2" method="$3" label="$4"
  if [ -f "$dst" ] && [ "$FORCE" != "force" ]; then
    skip=$((skip + 1))
    return
  fi
  [ -f "$dst" ] && rm "$dst"
  echo -n "  $label..."
  if $method "$src" "$dst" 2>/dev/null; then
    local dims=$(python3 -c "from PIL import Image; i=Image.open('$dst'); print(f'{i.size[0]}x{i.size[1]}')" 2>/dev/null || echo "?")
    echo " $dims"
    count=$((count + 1))
  else
    echo " FAILED"
    fail=$((fail + 1))
  fi
}

# ═══════════════════════════════════════════════════
# SKILL ICONS (8x anime — tiny 20x28px icons need max upscale)
# ═══════════════════════════════════════════════════
if [ "$CATEGORY" = "skill" ] || [ "$CATEGORY" = "ui" ] || [ "$CATEGORY" = "all" ]; then
  echo "═══ SKILL ICONS (8x anime) ═══"
  SRC="Data/Interface/Skill.OZJ"
  DST="Data/Interface/Skill.png"
  if [ -f "$SRC" ]; then
    TMP_JPG="$TMPDIR/Skill.jpg"
    extract_ozj "$SRC" "$TMP_JPG"
    do_upscale "$TMP_JPG" "$DST" upscale_anime_8x "Skill.OZJ"
  fi
  echo ""
fi

# ═══════════════════════════════════════════════════
# UI TEXTURES (4x anime — clean interface graphics)
# ═══════════════════════════════════════════════════
if [ "$CATEGORY" = "ui" ] || [ "$CATEGORY" = "all" ]; then
  echo "═══ UI TEXTURES (4x anime) ═══"
  for f in Data/Interface/*.OZJ Data/Interface/*.OZT; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    name="${base%.*}"
    ext="${base##*.}"
    ext_lower=$(echo "$ext" | tr '[:upper:]' '[:lower:]')
    dst="Data/Interface/${name}.png"

    # Skip Skill.OZJ (handled above at 8x)
    [ "$name" = "Skill" ] && continue

    TMP_INPUT="$TMPDIR/${name}_input.png"
    case "$ext_lower" in
      ozj)
        extract_ozj "$f" "$TMPDIR/${name}.jpg" 2>/dev/null || continue
        TMP_INPUT="$TMPDIR/${name}.jpg"
        ;;
      ozt)
        # OZT = 4-byte header + TGA
        dd if="$f" of="$TMPDIR/${name}.tga" bs=1 skip=4 2>/dev/null
        sips -s format png "$TMPDIR/${name}.tga" --out "$TMP_INPUT" > /dev/null 2>&1 || continue
        ;;
      *) continue ;;
    esac

    do_upscale "$TMP_INPUT" "$dst" upscale_anime_4x "$name"
  done
  echo ""
fi

# ═══════════════════════════════════════════════════
# TERRAIN TILES (2-pass 2x→2x — organic textures)
# ═══════════════════════════════════════════════════
if [ "$CATEGORY" = "terrain" ] || [ "$CATEGORY" = "all" ]; then
  echo "═══ TERRAIN TILES (2-pass 2x→2x) ═══"
  for world_dir in Data/World1 Data/World2 Data/World3 Data/World4 Data/World5; do
    [ -d "$world_dir" ] || continue
    echo "  $(basename $world_dir):"
    for f in "$world_dir"/Tile*.OZJ; do
      [ -f "$f" ] || continue
      base=$(basename "$f")
      name="${base%.*}"
      dst="$world_dir/${name}.png"

      TMP_JPG="$TMPDIR/${name}.jpg"
      extract_ozj "$f" "$TMP_JPG" 2>/dev/null || continue

      do_upscale "$TMP_JPG" "$dst" upscale_2pass "  $name"
    done
  done
  echo ""
fi

echo "═══════════════════════════════════════════════════"
echo "  Done: $count upscaled, $skip skipped (existing), $fail failed"
echo "  Pass 'force' as 2nd arg to re-upscale existing"
echo "═══════════════════════════════════════════════════"
