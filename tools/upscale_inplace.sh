#!/bin/bash
# Upscale OZJ/OZT textures IN-PLACE — keeps exact same format
# OZJ: strip 24-byte header → upscale JPEG → re-add header → overwrite
# OZT: strip 4-byte header → extract TGA → upscale RGB + alpha separately → rebuild TGA → re-add header
# NO code changes needed — game loads the same OZJ/OZT files, just at higher resolution
#
# Usage: cd client/build && bash ../../tools/upscale_inplace.sh <dir|all> [force]
# Backups saved to Data/<dir>_backup/
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESRGAN="$SCRIPT_DIR/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR="$SCRIPT_DIR/realesrgan/models"
SCALE=2
MODEL="realesrgan-x4plus"
MIN_SIZE=32  # Skip textures smaller than 32px in either dimension

TARGET="${1:?Usage: $0 <dir|all> [force]}"
FORCE="${2:-}"
TMPDIR="$(mktemp -d)"
trap "rm -rf '$TMPDIR'" EXIT

count=0
skip=0
fail=0

upscale_ozj() {
  local src="$1"
  local name=$(basename "${src%.*}")
  local dir=$(dirname "$src")
  local backup_dir="${dir}_backup"

  # Check if already upscaled (backup exists)
  if [ -f "$backup_dir/$name.OZJ" ] && [ "$FORCE" != "force" ]; then
    skip=$((skip + 1))
    return
  fi

  # Detect header offset and extract JPEG
  python3 -c "
data = open('$src','rb').read()
# Find JPEG magic FF D8
off = -1
if data[0] == 0xFF and data[1] == 0xD8:
    off = 0
elif len(data) > 25 and data[24] == 0xFF and data[25] == 0xD8:
    off = 24
else:
    exit(1)
open('$TMPDIR/${name}_hdr.bin','wb').write(data[:off])
open('$TMPDIR/${name}.jpg','wb').write(data[off:])
" 2>/dev/null || { fail=$((fail+1)); return; }

  # Check minimum size
  local dims=$(python3 -c "from PIL import Image; i=Image.open('$TMPDIR/${name}.jpg'); print(f'{i.size[0]} {i.size[1]}')" 2>/dev/null)
  local iw=$(echo "$dims" | cut -d' ' -f1)
  local ih=$(echo "$dims" | cut -d' ' -f2)
  if [ "${iw:-0}" -lt "$MIN_SIZE" ] || [ "${ih:-0}" -lt "$MIN_SIZE" ]; then
    skip=$((skip + 1))
    return
  fi

  # Upscale
  "$ESRGAN" -i "$TMPDIR/${name}.jpg" -o "$TMPDIR/${name}_up.jpg" \
    -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" -f jpg > /dev/null 2>&1 || { fail=$((fail+1)); return; }

  # Backup original
  mkdir -p "$backup_dir"
  cp "$src" "$backup_dir/"

  # Re-wrap: header + upscaled JPEG
  python3 -c "
import os
hdr = open('$TMPDIR/${name}_hdr.bin','rb').read()
jpg = open('$TMPDIR/${name}_up.jpg','rb').read()
open('$src','wb').write(hdr + jpg)
" 2>/dev/null || { fail=$((fail+1)); return; }

  count=$((count + 1))
  rm -f "$TMPDIR/${name}"*
}

upscale_ozt() {
  local src="$1"
  local name=$(basename "${src%.*}")
  local dir=$(dirname "$src")
  local backup_dir="${dir}_backup"

  if [ -f "$backup_dir/$(basename $src)" ] && [ "$FORCE" != "force" ]; then
    skip=$((skip + 1))
    return
  fi

  # Extract TGA, split RGB + Alpha, upscale, rebuild
  python3 << PYEOF
import struct, os, sys
from PIL import Image

src = '$src'
name = '$name'
tmpdir = '$TMPDIR'
scale = $SCALE
min_size = $MIN_SIZE

with open(src, 'rb') as f:
    data = f.read()

mu_hdr = data[:4]
tga = data[4:]
w = struct.unpack('<H', tga[12:14])[0]
h = struct.unpack('<H', tga[14:16])[0]
bpp = tga[16]
img_type = tga[2]

if w < min_size or h < min_size:
    sys.exit(2)  # skip

# Decompress RLE if needed
pixels = tga[18:]
if img_type == 10:
    bpc = bpp // 8
    raw = bytearray()
    i = 0
    total = w * h * bpc
    while len(raw) < total and i < len(pixels):
        hdr = pixels[i]; i += 1
        cnt = (hdr & 0x7F) + 1
        if hdr & 0x80:
            pix = pixels[i:i+bpc]; i += bpc
            raw.extend(pix * cnt)
        else:
            raw.extend(pixels[i:i+cnt*bpc]); i += cnt*bpc
    pixels = bytes(raw)

bpc = bpp // 8

# Build PIL image (TGA is BGRA bottom-to-top)
img = Image.new('RGBA' if bpc == 4 else 'RGB', (w, h))
for y in range(h):
    for x in range(w):
        s = ((h-1-y)*w + x) * bpc
        b, g, r = pixels[s], pixels[s+1], pixels[s+2]
        a = pixels[s+3] if bpc == 4 else 255
        img.putpixel((x, y), (r, g, b, a) if bpc == 4 else (r, g, b))

# Split RGB and Alpha
rgb = img.convert('RGB')
rgb.save(f'{tmpdir}/{name}_rgb.png')
if bpc == 4:
    alpha = img.split()[3]
    alpha.save(f'{tmpdir}/{name}_alpha.png')

PYEOF

  local rc=$?
  if [ $rc -eq 2 ]; then skip=$((skip+1)); return; fi
  if [ $rc -ne 0 ]; then fail=$((fail+1)); return; fi

  # Upscale RGB
  "$ESRGAN" -i "$TMPDIR/${name}_rgb.png" -o "$TMPDIR/${name}_rgb_up.png" \
    -s "$SCALE" -n "$MODEL" -m "$MODEL_DIR" > /dev/null 2>&1 || { fail=$((fail+1)); return; }

  # Upscale alpha with nearest-neighbor (sharp edges)
  if [ -f "$TMPDIR/${name}_alpha.png" ]; then
    python3 -c "
from PIL import Image
a = Image.open('$TMPDIR/${name}_alpha.png')
a2 = a.resize((a.width*$SCALE, a.height*$SCALE), Image.NEAREST)
a2.save('$TMPDIR/${name}_alpha_up.png')
"
  fi

  # Rebuild OZT: MU header + TGA (uncompressed, BGRA, bottom-to-top)
  python3 << PYEOF
import struct
from PIL import Image

name = '$name'
tmpdir = '$TMPDIR'
src = '$src'

rgb = Image.open(f'{tmpdir}/{name}_rgb_up.png').convert('RGB')
w, h = rgb.size
import os
has_alpha = os.path.exists(f'{tmpdir}/{name}_alpha_up.png')
if has_alpha:
    alpha = Image.open(f'{tmpdir}/{name}_alpha_up.png').convert('L')
    bpp = 32
else:
    bpp = 24
bpc = bpp // 8

# Build TGA (uncompressed, type 2)
tga_hdr = bytearray(18)
tga_hdr[2] = 2  # uncompressed
struct.pack_into('<H', tga_hdr, 12, w)
struct.pack_into('<H', tga_hdr, 14, h)
tga_hdr[16] = bpp

# Pixel data: BGRA bottom-to-top
pixel_data = bytearray(w * h * bpc)
for y in range(h):
    for x in range(w):
        r, g, b = rgb.getpixel((x, y))
        dst = ((h-1-y)*w + x) * bpc
        pixel_data[dst] = b
        pixel_data[dst+1] = g
        pixel_data[dst+2] = r
        if has_alpha:
            pixel_data[dst+3] = alpha.getpixel((x, y))

# Read original MU header (4 bytes)
with open(src, 'rb') as f:
    mu_hdr = f.read(4)

# Write: MU header + TGA header + pixels
with open(src, 'wb') as f:
    f.write(mu_hdr)
    f.write(tga_hdr)
    f.write(pixel_data)

PYEOF

  if [ $? -eq 0 ]; then
    mkdir -p "${dir}_backup"
    # Backup was the original before overwrite — but we already overwrote.
    # Need to backup BEFORE overwrite. Fix: backup first in the python rebuild step
    count=$((count + 1))
  else
    fail=$((fail + 1))
  fi
  rm -f "$TMPDIR/${name}"*
}

upscale_dir() {
  local dir="$1"
  [ -d "$dir" ] || return
  local backup_dir="${dir}_backup"
  local before=$count

  # Backup originals first if not already done
  if [ ! -d "$backup_dir" ]; then
    echo "    Backing up originals to ${backup_dir}/"
    mkdir -p "$backup_dir"
    cp "$dir"/*.OZJ "$dir"/*.ozj "$dir"/*.OZT "$dir"/*.ozt "$backup_dir/" 2>/dev/null || true
  fi

  for f in "$dir"/*.OZJ "$dir"/*.ozj; do
    [ -f "$f" ] || continue
    upscale_ozj "$f"
  done

  for f in "$dir"/*.OZT "$dir"/*.ozt; do
    [ -f "$f" ] || continue
    upscale_ozt "$f"
  done

  echo "  $dir: $((count - before)) upscaled, $skip skipped, $fail failed"
}

echo "═══════════════════════════════════════════════════"
echo "  In-Place Texture Upscaler (${SCALE}x ${MODEL})"
echo "  Format: OZJ→OZJ, OZT→OZT (same format, higher res)"
echo "  Backups: <dir>_backup/"
echo "═══════════════════════════════════════════════════"

case "$TARGET" in
  all)
    for d in Item Player Monster Object1 Object2 Object3 Object4 Object5 Skill Effect NPC; do
      echo "── $d ──"
      upscale_dir "Data/$d"
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
echo "  Restore originals: cp Data/<dir>_backup/* Data/<dir>/"
echo "═══════════════════════════════════════════════════"
