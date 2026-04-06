#!/bin/bash
# Improved texture upscaling pipeline:
# 1. Extract OZJ/OZT → PNG (separate RGB + alpha for TGA textures)
# 2. Upscale at 2x with realesrgan-x4plus (photo model, less hallucination)
# 3. Recombine alpha for TGA-sourced textures
# 4. Output as PNG (game loads via TextureLoader)
#
# Key improvements over previous approach:
# - 2x scale (not 3x/4x) — preserves original detail, less AI hallucination
# - Alpha separated before upscale, recombined after (no edge bleed)
# - Skips tiny textures (<64px) and non-visual textures (lightmaps, minimaps)
# - Photo model for all textures (preserves grain/darkness)
#
# Usage: cd client/build && bash ../../tools/upscale_pipeline.sh <dir> [force]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESRGAN="$SCRIPT_DIR/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR="$SCRIPT_DIR/realesrgan/models"
SCALE=2
MODEL="realesrgan-x4plus"
MIN_SIZE=64  # Skip textures smaller than 64px

TARGET="${1:?Usage: $0 <dir|all> [force]}"
FORCE="${2:-}"
TMPDIR="$(mktemp -d)"
trap "rm -rf '$TMPDIR'" EXIT

count=0
skip=0
fail=0

# Skip list: textures that shouldn't be upscaled
should_skip() {
  local name="$1"
  case "$name" in
    TerrainLight*|TerrainHeight*|mini_map*|map1*) return 0 ;;  # data textures
    rain*|leaf0*|angeflo*) return 0 ;;  # tiny particles
    *_grass) return 0 ;;  # grass billboard (needs OZT alpha)
  esac
  return 1
}

upscale_file() {
  local src="$1" dst="$2" is_alpha="$3"
  local name=$(basename "${src%.*}")

  # Skip blacklisted textures
  if should_skip "$name"; then
    skip=$((skip + 1))
    return
  fi

  # Skip if output exists and not forcing
  if [ -f "$dst" ] && [ "$FORCE" != "force" ]; then
    skip=$((skip + 1))
    return
  fi

  local ext="${src##*.}"
  local ext_lower=$(echo "$ext" | tr '[:upper:]' '[:lower:]')
  local input=""

  case "$ext_lower" in
    ozj)
      # Extract JPEG from OZJ
      python3 -c "
data=open('$src','rb').read()
off=24 if len(data)>24 and data[24]==0xFF and data[25]==0xD8 else 0
if data[off]!=0xFF: exit(1)
open('$TMPDIR/${name}.jpg','wb').write(data[off:])
" 2>/dev/null || { fail=$((fail+1)); return; }
      input="$TMPDIR/${name}.jpg"
      ;;
    ozt)
      if [ "$is_alpha" = "true" ]; then
        # Split TGA into RGB + Alpha PNGs
        python3 << PYEOF
import struct
from PIL import Image
with open('$src', 'rb') as f:
    data = f.read()
tga = data[4:]
w = struct.unpack('<H', tga[12:14])[0]
h = struct.unpack('<H', tga[14:16])[0]
bpp = tga[16]
img_type = tga[2]
pixels = tga[18:]
if img_type == 10:
    bpc = bpp // 8
    raw = bytearray()
    i = 0
    total = w * h * bpc
    while len(raw) < total and i < len(pixels):
        hdr = pixels[i]; i += 1
        count = (hdr & 0x7F) + 1
        if hdr & 0x80:
            pix = pixels[i:i+bpc]; i += bpc
            raw.extend(pix * count)
        else:
            raw.extend(pixels[i:i+count*bpc]); i += count*bpc
    pixels = bytes(raw)
bpc = bpp // 8
if w < $MIN_SIZE or h < $MIN_SIZE:
    exit(2)  # too small
rgb = Image.new('RGB', (w, h))
alpha = Image.new('L', (w, h))
for y in range(h):
    for x in range(w):
        s = ((h-1-y)*w + x) * bpc
        b,g,r = pixels[s], pixels[s+1], pixels[s+2]
        a = pixels[s+3] if bpc == 4 else 255
        rgb.putpixel((x, y), (r, g, b))
        alpha.putpixel((x, y), a)
rgb.save('$TMPDIR/${name}_rgb.png')
alpha.save('$TMPDIR/${name}_alpha.png')
PYEOF
        if [ $? -eq 2 ]; then skip=$((skip+1)); return; fi
        if [ $? -ne 0 ]; then fail=$((fail+1)); return; fi

        # Upscale RGB
        "$ESRGAN" -i "$TMPDIR/${name}_rgb.png" -o "$TMPDIR/${name}_rgb_up.png" \
          -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" > /dev/null 2>&1 || { fail=$((fail+1)); return; }

        # Upscale alpha with nearest-neighbor (sharp edges, no bleed)
        python3 -c "
from PIL import Image
a = Image.open('$TMPDIR/${name}_alpha.png')
a2 = a.resize((a.width*$SCALE, a.height*$SCALE), Image.NEAREST)
a2.save('$TMPDIR/${name}_alpha_up.png')
"
        # Recombine
        python3 -c "
from PIL import Image
rgb = Image.open('$TMPDIR/${name}_rgb_up.png').convert('RGB')
alpha = Image.open('$TMPDIR/${name}_alpha_up.png').convert('L')
rgba = Image.new('RGBA', rgb.size)
rgba.paste(rgb)
rgba.putalpha(alpha)
rgba.save('$dst')
"
        if [ $? -eq 0 ]; then count=$((count+1)); else fail=$((fail+1)); fi
        return
      else
        # Non-alpha OZT: convert to PNG, upscale normally
        dd if="$src" of="$TMPDIR/${name}.tga" bs=1 skip=4 2>/dev/null
        sips -s format png "$TMPDIR/${name}.tga" --out "$TMPDIR/${name}_in.png" > /dev/null 2>&1 || { fail=$((fail+1)); return; }
        input="$TMPDIR/${name}_in.png"
      fi
      ;;
    *) return ;;
  esac

  # Check minimum size
  local dims=$(python3 -c "from PIL import Image; i=Image.open('$input'); print(f'{i.size[0]} {i.size[1]}')" 2>/dev/null)
  local iw=$(echo "$dims" | cut -d' ' -f1)
  local ih=$(echo "$dims" | cut -d' ' -f2)
  if [ "${iw:-0}" -lt "$MIN_SIZE" ] || [ "${ih:-0}" -lt "$MIN_SIZE" ]; then
    skip=$((skip + 1))
    return
  fi

  [ -f "$dst" ] && rm "$dst"
  if "$ESRGAN" -i "$input" -o "$dst" -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" > /dev/null 2>&1; then
    count=$((count + 1))
  else
    fail=$((fail + 1))
  fi
  rm -f "$TMPDIR/${name}".*
}

upscale_dir() {
  local dir="$1"
  [ -d "$dir" ] || return
  local local_count=$count local_skip=$skip local_fail=$fail

  for f in "$dir"/*.OZJ "$dir"/*.ozj "$dir"/*.OZT "$dir"/*.ozt; do
    [ -f "$f" ] || continue
    local base=$(basename "$f")
    local name="${base%.*}"
    local ext="${base##*.}"
    local ext_lower=$(echo "$ext" | tr '[:upper:]' '[:lower:]')
    local dst="$dir/${name}.png"
    local is_alpha="false"
    [ "$ext_lower" = "ozt" ] && is_alpha="true"

    upscale_file "$f" "$dst" "$is_alpha"
  done

  local dc=$((count - local_count))
  local ds=$((skip - local_skip))
  local df=$((fail - local_fail))
  echo "  $dir: $dc upscaled, $ds skipped, $df failed"
}

echo "═══════════════════════════════════════════════════"
echo "  Texture Upscale Pipeline (${SCALE}x ${MODEL})"
echo "  Alpha: separated + nearest-neighbor upscale"
echo "  Min size: ${MIN_SIZE}px (smaller textures skipped)"
echo "═══════════════════════════════════════════════════"

case "$TARGET" in
  all)
    for d in Item Player Monster Object1 Object2 Object3 Object4 Object5 Skill Effect NPC; do
      echo "── $d ──"
      upscale_dir "Data/$d"
    done
    ;;
  terrain)
    for w in World1 World2 World3 World4 World5; do
      echo "── $w ──"
      for f in "Data/$w"/Tile*.OZJ; do
        [ -f "$f" ] || continue
        base=$(basename "$f" .OZJ)
        [ "$base" = "TileWater01" ] && continue
        upscale_file "$f" "Data/$w/${base}.png" "false"
      done
      echo "  Data/$w: done"
    done
    ;;
  *)
    echo "── $TARGET ──"
    upscale_dir "Data/$TARGET"
    ;;
esac

echo ""
echo "═══════════════════════════════════════════════════"
echo "  Done: $count upscaled, $skip skipped, $fail failed"
echo "═══════════════════════════════════════════════════"
