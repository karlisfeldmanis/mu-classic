#!/usr/bin/env python3
"""
Texture quality enhancer for MU Online remaster.

Supersample pipeline — improves quality while keeping ORIGINAL dimensions:
1. Pre-denoise JPEG blocking artifacts (if detected)
2. Upscale 2x with LANCZOS (creates headroom for sharpening)
3. Apply unsharp mask at 2x (detail enhancement without aliasing)
4. Downscale back to original size with LANCZOS (anti-aliased result)
5. Save in original format with preserved headers

Result: Same dimensions, cleaner textures, sharper detail, no JPEG blocking.

Works on OZJ (JPEG) and OZT (TGA with alpha) formats.
Handles all texture sizes, binary vs gradient alpha, non-square dimensions.

Usage:
  python3 enhance_textures.py <file_or_dir> [--dry-run]

Examples:
  python3 enhance_textures.py client/Data/Object1 --dry-run
  python3 enhance_textures.py client/Data/Player/skin_wizard_01.OZJ
  python3 enhance_textures.py client/Data/Item
"""
import struct, io, os, sys
from PIL import Image, ImageFilter, ImageStat

MIN_SIZE = 16   # Skip textures smaller than this in BOTH dimensions

# Skip patterns: lightmaps, fire, water, glow, minimaps — enhancing these breaks them
SKIP_PATTERNS = [
    'terrainlight', 'light_', '_light',
    'minimap', 'mini_map',
    'tilewater', 'water_', '_water',
    'fire_', '_fire', 'fire0', 'lava', 'candle',
    'glow', 'flare', 'bright', 'spark', 'lightning',
    'leaf',  # tiny particle textures with alpha — enhancing corrupts colors
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


def enhance_rgb(rgb):
    """Supersample enhance: up 4x, multi-pass sharpen, anti-grain, down to original.

    Two-pass sharpening at 4x avoids the grain from single aggressive sharpen:
      Pass 1: Large radius — enhances structure (edges, shapes)
      Pass 2: Small radius — enhances fine detail (texture grain, surface)
    Followed by a gentle Gaussian to smooth sharpening artifacts before downscale.
    """
    w, h = rgb.size

    # Upscale 4x — maximum headroom for clean sharpening
    big = rgb.resize((w * 4, h * 4), Image.LANCZOS)

    # Pass 1: Structure enhance (large radius, moderate strength, high threshold)
    # High threshold prevents sharpening noise/flat areas — only real edges
    if w >= 256 or h >= 256:
        big = big.filter(ImageFilter.UnsharpMask(radius=3.0, percent=70, threshold=4))
    elif w >= 128 or h >= 128:
        big = big.filter(ImageFilter.UnsharpMask(radius=2.5, percent=60, threshold=4))
    elif w >= 64 or h >= 64:
        big = big.filter(ImageFilter.UnsharpMask(radius=2.0, percent=50, threshold=3))
    else:
        big = big.filter(ImageFilter.UnsharpMask(radius=1.5, percent=40, threshold=3))

    # Pass 2: Fine detail enhance (small radius, gentle, moderate threshold)
    big = big.filter(ImageFilter.UnsharpMask(radius=1.0, percent=35, threshold=3))

    # Anti-grain: very slight blur to smooth sharpening micro-artifacts
    big = big.filter(ImageFilter.GaussianBlur(0.4))

    # Downscale back to original — anti-aliased, cleaner than original
    return big.resize((w, h), Image.LANCZOS)


def process_ozj(fpath, dry_run=False):
    """Process OZJ (JPEG with optional 24-byte MU header)."""
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

    if dry_run:
        blocking = detect_blocking(img.convert('RGB'))
        return 'dry', f'{w}x{h} block={blocking:.2f}'

    rgb = img.convert('RGB')

    # Pre-denoise if JPEG blocking is noticeable
    blocking = detect_blocking(rgb)
    if blocking > 1.2:
        rgb = rgb.filter(ImageFilter.MedianFilter(3))

    result = enhance_rgb(rgb)

    # Save back with preserved header at ORIGINAL size
    hdr = data[:hdr_off]
    buf = io.BytesIO()
    result.save(buf, 'JPEG', quality=98)
    open(fpath, 'wb').write(hdr + buf.getvalue())
    return 'ok', f'{w}x{h}'


def process_ozt(fpath, dry_run=False):
    """Process OZT (TGA with 4-byte MU header). Alpha preserved carefully."""
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

    has_alpha = (bpp == 32)

    if dry_run:
        return 'dry', f'{w}x{h} bpp={bpp} alpha={"Y" if has_alpha else "N"}'

    # Decompress RLE if needed
    pixels = tga[18:]
    if img_type == 10:
        bpc = bpp // 8
        raw = bytearray()
        i = 0
        total_bytes = w * h * bpc
        while len(raw) < total_bytes and i < len(pixels):
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

    # Enhance RGB (supersample: up, sharpen, down — same size)
    result = enhance_rgb(rgb)

    # Alpha: supersample enhance too (smooth edges)
    if has_alpha:
        alpha_vals = set(alpha.getdata())
        is_binary = len(alpha_vals) <= 4
        if not is_binary:
            # Gradient alpha: supersample for smoother transitions
            big_a = alpha.resize((w * 4, h * 4), Image.LANCZOS)
            alpha = big_a.resize((w, h), Image.LANCZOS)
        # Binary alpha: leave as-is (preserve hard edges exactly)

    # Rebuild TGA at ORIGINAL size (uncompressed, bottom-to-top)
    pixel_data = bytearray(w * h * bpc)
    for y in range(h):
        for x in range(w):
            r, g, b = result.getpixel((x, y))
            dst = ((h - 1 - y) * w + x) * bpc
            pixel_data[dst] = b
            pixel_data[dst + 1] = g
            pixel_data[dst + 2] = r
            if alpha:
                pixel_data[dst + 3] = alpha.getpixel((x, y))

    tga_hdr = bytearray(18)
    tga_hdr[2] = 2  # uncompressed
    struct.pack_into('<H', tga_hdr, 12, w)
    struct.pack_into('<H', tga_hdr, 14, h)
    tga_hdr[16] = bpp

    open(fpath, 'wb').write(mu_hdr + tga_hdr + pixel_data)
    a_info = 'mask' if (has_alpha and is_binary) else ('grad' if has_alpha else 'none')
    return 'ok', f'{w}x{h} alpha={a_info}'


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 enhance_textures.py <file_or_dir> [--dry-run]")
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
    mode = "DRY RUN" if dry_run else "ENHANCE"
    dirname = os.path.basename(target.rstrip('/'))
    print(f"=== Texture {mode}: {dirname} ({total} files) ===")
    print(f"  Pipeline: denoise -> supersample 2x -> sharpen -> downscale (same size)")

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
