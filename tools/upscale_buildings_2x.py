#!/usr/bin/env python3
"""
2x building/world texture upscaler for MU Online remaster.

Designed specifically for building, terrain, and world object textures.
Handles OZJ (JPEG) and OZT (TGA with alpha) formats carefully.

Key design choices:
- LANCZOS 2x + unsharp mask (no ESRGAN — avoids hallucinated stripes)
- OZT alpha channel: LANCZOS 2x (smooth edges, not nearest-neighbor)
- Preserves MU file headers exactly
- Skips light/fire/water/glow/minimap textures (ESRGAN darkens them)
- Skips textures < 16px in both dimensions
- Adjusts sharpening based on texture size (more for large, less for small)
- Pre-denoise for JPEG blocking artifacts

Usage:
  python3 upscale_buildings_2x.py <dir> [--dry-run]
  python3 upscale_buildings_2x.py client/Data/Object1 --dry-run
  python3 upscale_buildings_2x.py client/Data/World1

Processes all OZJ/OZT in the given directory (non-recursive).
"""
import struct, io, os, sys
from PIL import Image, ImageFilter, ImageStat

MIN_SIZE = 16  # Skip textures smaller than this in BOTH dimensions
MAX_SIZE = 512  # Skip textures larger than this (minimaps etc)

# Skip patterns: light maps, fire, water, glow — upscaling darkens/breaks these
SKIP_PATTERNS = [
    'terrainlight', 'light_', '_light', 'minimap', 'mini_map',
    'tilewater', 'water_', '_water',
    'fire_', '_fire', 'fire0', 'lava', 'candle',
    'glow', 'flare', 'bright', 'spark', 'lightning',
    'leaf0',  # tiny leaf particles
]


def should_skip(name):
    nl = name.lower()
    return any(p in nl for p in SKIP_PATTERNS)


def detect_blocking(img):
    """Return JPEG blocking artifact ratio. >1.2 = noticeable."""
    gray = img.convert('L')
    w, h = gray.size
    if w < 16 or h < 16:
        return 1.0
    pixels = list(gray.getdata())
    bd, nd = [], []
    for y in range(min(h, 64)):
        for x in range(w - 1):
            diff = abs(pixels[y * w + x] - pixels[y * w + x + 1])
            if x % 8 == 7:
                bd.append(diff)
            else:
                nd.append(diff)
    if not nd or not bd:
        return 1.0
    return (sum(bd) / len(bd)) / max(sum(nd) / len(nd), 0.01)


def upscale_2x(rgb):
    """LANCZOS 2x upscale with adaptive unsharp mask."""
    w, h = rgb.size
    result = rgb.resize((w * 2, h * 2), Image.LANCZOS)

    # Adaptive sharpening: larger textures can handle more
    if w >= 256 or h >= 256:
        result = result.filter(ImageFilter.UnsharpMask(radius=1.5, percent=120, threshold=2))
    elif w >= 128 or h >= 128:
        result = result.filter(ImageFilter.UnsharpMask(radius=1.2, percent=100, threshold=2))
    elif w >= 64 or h >= 64:
        result = result.filter(ImageFilter.UnsharpMask(radius=1.0, percent=80, threshold=2))
    else:
        result = result.filter(ImageFilter.UnsharpMask(radius=0.8, percent=60, threshold=3))

    return result


def process_ozj(fpath, dry_run=False):
    """Process OZJ (JPEG with optional 24-byte MU header)."""
    name = os.path.basename(fpath)
    data = open(fpath, 'rb').read()

    img = None
    hdr_off = 0
    for off in [24, 0]:
        if off >= len(data):
            continue
        try:
            img = Image.open(io.BytesIO(data[off:]))
            img.load()
            hdr_off = off
            break
        except:
            img = None

    if img is None:
        return 'skip', 'cannot load'

    w, h = img.size
    if w < MIN_SIZE and h < MIN_SIZE:
        return 'skip', f'{w}x{h} too small'
    if w > MAX_SIZE or h > MAX_SIZE:
        return 'skip', f'{w}x{h} too large'

    if dry_run:
        blocking = detect_blocking(img.convert('RGB'))
        return 'dry', f'{w}x{h} block={blocking:.2f}'

    rgb = img.convert('RGB')

    # Pre-denoise if JPEG blocking is noticeable
    blocking = detect_blocking(rgb)
    if blocking > 1.2:
        rgb = rgb.filter(ImageFilter.MedianFilter(3))

    result = upscale_2x(rgb)

    # Save back with preserved header
    hdr = data[:hdr_off]
    buf = io.BytesIO()
    result.save(buf, 'JPEG', quality=98)
    open(fpath, 'wb').write(hdr + buf.getvalue())
    return 'ok', f'{w}x{h} -> {w*2}x{h*2}'


def process_ozt(fpath, dry_run=False):
    """Process OZT (TGA with 4-byte MU header). Alpha preserved carefully."""
    name = os.path.basename(fpath)
    data = open(fpath, 'rb').read()

    mu_hdr = data[:4]
    tga = data[4:]
    if len(tga) < 18:
        return 'skip', 'too small'

    w = struct.unpack('<H', tga[12:14])[0]
    h = struct.unpack('<H', tga[14:16])[0]
    bpp = tga[16]
    img_type = tga[2]

    if w < MIN_SIZE and h < MIN_SIZE:
        return 'skip', f'{w}x{h} too small'
    if w > MAX_SIZE or h > MAX_SIZE:
        return 'skip', f'{w}x{h} too large'

    has_alpha = (bpp == 32)

    if dry_run:
        return 'dry', f'{w}x{h} bpp={bpp} alpha={"Y" if has_alpha else "N"}'

    # Decompress RLE if needed
    pixels = tga[18:]
    if img_type == 10:
        bpc = bpp // 8
        raw = bytearray()
        i = 0
        total = w * h * bpc
        while len(raw) < total and i < len(pixels):
            hdr_byte = pixels[i]; i += 1
            cnt = (hdr_byte & 0x7F) + 1
            if hdr_byte & 0x80:
                pix = pixels[i:i + bpc]; i += bpc
                raw.extend(pix * cnt)
            else:
                raw.extend(pixels[i:i + cnt * bpc]); i += cnt * bpc
        pixels = bytes(raw)

    bpc = bpp // 8
    if len(pixels) < w * h * bpc:
        return 'fail', f'pixel data too short ({len(pixels)} < {w*h*bpc})'

    # Extract RGB and Alpha
    rgb = Image.new('RGB', (w, h))
    alpha = Image.new('L', (w, h)) if has_alpha else None
    for y in range(h):
        for x in range(w):
            s = ((h - 1 - y) * w + x) * bpc
            if s + 2 >= len(pixels):
                continue
            b, g, r = pixels[s], pixels[s + 1], pixels[s + 2]
            rgb.putpixel((x, y), (r, g, b))
            if has_alpha and s + 3 < len(pixels):
                alpha.putpixel((x, y), pixels[s + 3])

    nw, nh = w * 2, h * 2

    # Upscale RGB
    result = upscale_2x(rgb)

    # Alpha: LANCZOS 2x for smooth blending edges
    # Check if alpha is binary (mask) or gradient
    if has_alpha:
        alpha_stat = ImageStat.Stat(alpha)
        alpha_vals = set(alpha.getdata())
        is_binary_mask = len(alpha_vals) <= 4  # Only a few unique alpha values

        if is_binary_mask:
            # Binary mask: use NEAREST to preserve hard edges exactly
            alpha_up = alpha.resize((nw, nh), Image.NEAREST)
        else:
            # Gradient alpha: LANCZOS for smooth transitions
            alpha_up = alpha.resize((nw, nh), Image.LANCZOS)
    else:
        alpha_up = None

    # Rebuild TGA (uncompressed, bottom-to-top)
    pixel_data = bytearray(nw * nh * bpc)
    for y in range(nh):
        for x in range(nw):
            r, g, b = result.getpixel((x, y))
            dst = ((nh - 1 - y) * nw + x) * bpc
            pixel_data[dst] = b
            pixel_data[dst + 1] = g
            pixel_data[dst + 2] = r
            if alpha_up:
                pixel_data[dst + 3] = alpha_up.getpixel((x, y))

    tga_hdr = bytearray(18)
    tga_hdr[2] = 2  # uncompressed
    struct.pack_into('<H', tga_hdr, 12, nw)
    struct.pack_into('<H', tga_hdr, 14, nh)
    tga_hdr[16] = bpp

    open(fpath, 'wb').write(mu_hdr + tga_hdr + pixel_data)
    a_mode = 'mask' if (has_alpha and is_binary_mask) else ('grad' if has_alpha else 'none')
    return 'ok', f'{w}x{h} -> {nw}x{nh} alpha={a_mode}'


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 upscale_buildings_2x.py <dir_or_file> [--dry-run]")
        print("  Processes OZJ/OZT in the given directory.")
        print("  Use --dry-run to preview without modifying files.")
        sys.exit(1)

    target = sys.argv[1]
    dry_run = '--dry-run' in sys.argv

    if os.path.isfile(target):
        files = [target]
    elif os.path.isdir(target):
        files = sorted([
            os.path.join(target, f) for f in os.listdir(target)
            if f.upper().endswith(('.OZJ', '.OZT'))
        ])
    else:
        print(f"Not found: {target}")
        sys.exit(1)

    total = len(files)
    mode = "DRY RUN" if dry_run else "UPSCALE 2x"
    dirname = os.path.basename(target.rstrip('/'))
    print(f"=== Building Texture {mode}: {dirname} ({total} files) ===")
    print(f"  Pipeline: denoise(if needed) -> LANCZOS 2x -> adaptive sharpen")
    print(f"  Alpha: LANCZOS (gradient) or NEAREST (binary mask)")
    print(f"  Skip: <{MIN_SIZE}px, >{MAX_SIZE}px, light/fire/water/glow")

    ok = skip = fail = 0
    for i, fpath in enumerate(files):
        name = os.path.basename(fpath)
        ext = name.rsplit('.', 1)[1].upper()

        if should_skip(name):
            skip += 1
            if dry_run:
                print(f"  [{i+1:3d}/{total}] - {name:<32} skip pattern")
            continue

        if ext == 'OZJ':
            status, info = process_ozj(fpath, dry_run)
        elif ext == 'OZT':
            status, info = process_ozt(fpath, dry_run)
        else:
            skip += 1
            continue

        sym = {'ok': '+', 'skip': '-', 'fail': 'X', 'dry': '?'}[status]
        print(f"  [{i+1:3d}/{total}] {sym} {name:<32} {info}")

        if status in ('ok', 'dry'):
            ok += 1
        elif status == 'skip':
            skip += 1
        else:
            fail += 1

    print(f"\n{'Would process' if dry_run else 'Done'}: {ok} processed, {skip} skipped, {fail} failed")


if __name__ == '__main__':
    main()
