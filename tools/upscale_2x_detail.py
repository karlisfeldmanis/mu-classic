#!/usr/bin/env python3
"""
2x texture upscaler for MU Online remaster (armors, weapons, items).

Outputs textures at 2x resolution using 4x multi-pass supersample pipeline:
1. Pre-denoise JPEG blocking artifacts (if detected)
2. Upscale 4x with LANCZOS (maximum headroom)
3. Multi-pass sharpen: structure (large radius) + detail (small radius)
4. Anti-grain Gaussian to smooth sharpening artifacts
5. Downscale to 2x (not back to original — actual 2x output)
6. Save in original format with preserved headers

No ESRGAN — pure LANCZOS pipeline avoids hallucinated stripes entirely.

Usage: python3 upscale_2x_detail.py <file_or_dir> [--dry-run]
"""
import struct, io, os, sys, tempfile
from PIL import Image, ImageFilter, ImageStat

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MIN_SIZE = 32

SKIP_PATTERNS = ['light', 'fire_0', 'candle', 'brightness', '_R.', 'glow', 'flare',
                 'head_N', 'headhair', '_hair', '_wand']


def should_skip(name):
    nl = name.lower()
    return any(p.lower() in nl for p in SKIP_PATTERNS)


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
    """4x multi-pass supersample, downscale to 2x output.

    Two-pass sharpening at 4x avoids grain from single aggressive sharpen:
      Pass 1: Large radius — enhances structure (edges, shapes)
      Pass 2: Small radius — enhances fine detail (texture, surface)
    Gentle Gaussian smooths sharpening artifacts before downscale to 2x.
    """
    w, h = rgb.size

    # Upscale 4x — maximum headroom for clean sharpening
    big = rgb.resize((w * 4, h * 4), Image.LANCZOS)

    # Pass 1: Structure enhance (large radius, moderate strength, high threshold)
    if w >= 256 or h >= 256:
        big = big.filter(ImageFilter.UnsharpMask(radius=3.0, percent=70, threshold=4))
    elif w >= 128 or h >= 128:
        big = big.filter(ImageFilter.UnsharpMask(radius=2.5, percent=60, threshold=4))
    elif w >= 64 or h >= 64:
        big = big.filter(ImageFilter.UnsharpMask(radius=2.0, percent=50, threshold=3))
    else:
        big = big.filter(ImageFilter.UnsharpMask(radius=1.5, percent=40, threshold=3))

    # Pass 2: Fine detail enhance
    big = big.filter(ImageFilter.UnsharpMask(radius=1.0, percent=35, threshold=3))

    # Anti-grain: smooth sharpening micro-artifacts
    big = big.filter(ImageFilter.GaussianBlur(0.4))

    # Downscale to 2x (not back to original — actual 2x output)
    return big.resize((w * 2, h * 2), Image.LANCZOS)


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
    if w < MIN_SIZE or h < MIN_SIZE:
        return 'skip', f'{w}x{h} too small'

    if dry_run:
        return 'dry', f'{w}x{h}'

    rgb = img.convert('RGB')

    # Pre-denoise if JPEG blocking is noticeable
    blocking = detect_blocking(rgb)
    if blocking > 1.2:
        rgb = rgb.filter(ImageFilter.MedianFilter(3))

    result = upscale_2x(rgb)

    # Save back as OZJ
    hdr = data[:hdr_off]
    buf = io.BytesIO()
    result.save(buf, 'JPEG', quality=98)
    open(fpath, 'wb').write(hdr + buf.getvalue())
    return 'ok', f'{w}x{h} -> {w*2}x{h*2}'


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

    if w < MIN_SIZE or h < MIN_SIZE:
        return 'skip', f'{w}x{h} too small'

    if dry_run:
        return 'dry', f'{w}x{h} bpp={bpp}'

    # Decompress RLE
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
        return 'fail', 'pixel data too short'

    # Extract RGB and Alpha
    rgb = Image.new('RGB', (w, h))
    alpha = Image.new('L', (w, h)) if bpc == 4 else None
    for y in range(h):
        for x in range(w):
            s = ((h - 1 - y) * w + x) * bpc
            if s + 2 >= len(pixels):
                continue
            b, g, r = pixels[s], pixels[s + 1], pixels[s + 2]
            rgb.putpixel((x, y), (r, g, b))
            if alpha and s + 3 < len(pixels):
                alpha.putpixel((x, y), pixels[s + 3])

    nw, nh = w * 2, h * 2

    result = upscale_2x(rgb)

    # Alpha: detect binary vs gradient, choose appropriate method
    if alpha:
        alpha_vals = set(alpha.getdata())
        is_binary = len(alpha_vals) <= 4
        if is_binary:
            alpha_up = alpha.resize((nw, nh), Image.NEAREST)
        else:
            alpha_up = alpha.resize((nw, nh), Image.LANCZOS)
    else:
        alpha_up = None

    # Rebuild TGA at 2x
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
    return 'ok', f'{w}x{h} -> {nw}x{nh}'


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 upscale_2x_detail.py <file_or_dir> [--dry-run]")
        sys.exit(1)

    target = sys.argv[1]
    dry_run = '--dry-run' in sys.argv

    if os.path.isfile(target):
        files = [target]
    elif os.path.isdir(target):
        files = sorted([
            os.path.join(target, f) for f in os.listdir(target)
            if f.upper().endswith(('.OZJ', '.OZT'))
            and not should_skip(f)
        ])
    else:
        print(f"Not found: {target}")
        sys.exit(1)

    total = len(files)
    mode = "DRY RUN" if dry_run else "UPSCALE 2x"
    print(f"=== {mode}: {total} textures ===")
    print(f"  Pipeline: denoise -> 4x LANCZOS -> structure sharpen -> detail sharpen -> anti-grain -> 2x output")

    ok = skip = fail = 0
    for i, fpath in enumerate(files):
        name = os.path.basename(fpath)
        ext = fpath.rsplit('.', 1)[1].upper()

        if ext == 'OZJ':
            status, info = process_ozj(fpath, dry_run)
        else:
            status, info = process_ozt(fpath, dry_run)

        sym = {'ok': '+', 'skip': '-', 'fail': 'X', 'dry': '?'}[status]
        print(f"  [{i + 1:3d}/{total}] {sym} {name:<32} {info}")

        if status in ('ok', 'dry'): ok += 1
        elif status == 'skip': skip += 1
        else: fail += 1

    print(f"\n{'Would process' if dry_run else 'Done'}: {ok} processed, {skip} skipped, {fail} failed")


if __name__ == '__main__':
    main()
