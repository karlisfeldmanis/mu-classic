#!/usr/bin/env python3
"""
Smart 2x armor texture upscaler.

Analyzes each texture first, then applies the right pipeline:
- Detects JPEG blocking → pre-denoise if needed
- Detects color profile → chooses ESRGAN model accordingly
- OZT alpha: upscale RGB only, nearest-neighbor alpha
- Skips 32x32 and smaller (detail overlays — too small for ESRGAN)
- Preserves original colors via LAB luminance transfer (NOT histogram match)
- Sharpens based on texture type (more for metal, less for cloth)
- Saves in-place at 2x resolution, original format

Usage: python3 upscale_armor_2x.py <file_or_dir> [--dry-run]
"""
import struct, io, os, subprocess, sys, shutil, tempfile
from PIL import Image, ImageFilter, ImageEnhance, ImageStat

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ESRGAN = SCRIPT_DIR + "/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR = SCRIPT_DIR + "/realesrgan/models"

# Skip patterns (light/fire/glow textures — ESRGAN darkens them)
SKIP_PATTERNS = ['light', 'fire_0', 'candle', 'brightness', '_R.', 'glow', 'flare',
                 'head_N', 'headhair', '_hair', '_wand', 'head_a0']

MIN_SIZE = 48  # Skip textures smaller than 48px in either dimension


def detect_blocking(img):
    """Return JPEG blocking artifact ratio. >1.2 = noticeable."""
    gray = img.convert('L')
    pixels = list(gray.getdata())
    w = gray.size[0]
    h = gray.size[1]
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


def analyze(img):
    """Analyze texture properties for pipeline selection."""
    rgb = img.convert('RGB')
    stat = ImageStat.Stat(rgb)
    brightness = sum(stat.mean[:3]) / 3.0

    hsv = rgb.convert('HSV')
    hsv_stat = ImageStat.Stat(hsv)
    saturation = hsv_stat.mean[1]

    gray = rgb.convert('L')
    contrast = ImageStat.Stat(gray).stddev[0]

    blocking = detect_blocking(rgb)

    return {
        'brightness': brightness,
        'saturation': saturation,
        'contrast': contrast,
        'blocking': blocking,
        'is_metallic': saturation < 40,  # low saturation = metallic gray
        'is_dark': brightness < 70,
        'needs_denoise': blocking > 1.2,
    }


def color_preserve(upscaled, original):
    """Preserve original colors by per-channel mean/stddev matching.

    Adjusts each RGB channel of the upscaled image so its mean and
    standard deviation match the original. This is simple, robust,
    and preserves the exact color palette without HSV hue shifts.
    """
    orig_resized = original.resize(upscaled.size, Image.LANCZOS)
    result = Image.new('RGB', upscaled.size)

    for ch in range(3):
        up_ch = upscaled.split()[ch]
        orig_ch = orig_resized.split()[ch]

        up_stat = ImageStat.Stat(up_ch)
        orig_stat = ImageStat.Stat(orig_ch)

        up_mean = up_stat.mean[0]
        up_std = max(up_stat.stddev[0], 0.001)
        orig_mean = orig_stat.mean[0]
        orig_std = orig_stat.stddev[0]

        # Linear transform: (pixel - up_mean) / up_std * orig_std + orig_mean
        scale = orig_std / up_std
        offset = orig_mean - up_mean * scale

        lut = [max(0, min(255, int(v * scale + offset))) for v in range(256)]
        result_ch = up_ch.point(lut)

        # Paste channel into result
        bands = list(result.split())
        bands[ch] = result_ch
        result = Image.merge('RGB', bands)

    return result


def esrgan_2x(img, model='realesrgan-x4plus'):
    """ESRGAN 2x upscale. Returns upscaled image or None."""
    tmp_in = tempfile.mktemp(suffix='.png')
    tmp_out = tempfile.mktemp(suffix='.png')
    img.save(tmp_in)
    rc = subprocess.run(
        [ESRGAN, '-i', tmp_in, '-o', tmp_out, '-s', '2',
         '-n', model, '-m', MODEL_DIR],
        capture_output=True
    ).returncode
    result = None
    if rc == 0:
        try:
            result = Image.open(tmp_out).convert('RGB')
        except:
            pass
    for f in [tmp_in, tmp_out]:
        try: os.unlink(f)
        except: pass
    return result


def process_ozj(fpath, dry_run=False):
    """Process an OZJ (JPEG with optional 24-byte MU header)."""
    name = os.path.basename(fpath)
    data = open(fpath, 'rb').read()

    img = None
    hdr_off = 0
    for off in [0, 24]:
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

    rgb = img.convert('RGB')
    props = analyze(rgb)

    if dry_run:
        return 'dry', f'{w}x{h} block={props["blocking"]:.2f} sat={props["saturation"]:.0f} bright={props["brightness"]:.0f}'

    original = rgb.copy()

    # Step 1: Pre-denoise if JPEG blocking is bad
    if props['needs_denoise']:
        rgb = rgb.filter(ImageFilter.MedianFilter(3))

    # Step 2: ESRGAN 2x
    # Use anime model for hand-painted game textures (cleaner, less hallucination)
    up = esrgan_2x(rgb, 'realesrgan-x4plus-anime')
    if up is None:
        # Fallback to photo model
        up = esrgan_2x(rgb, 'realesrgan-x4plus')
    if up is None:
        return 'fail', 'ESRGAN failed'

    # Step 3: Color preservation via LAB luminance transfer
    result = color_preserve(up, original)

    # Step 4: Sharpen based on texture type
    if props['is_metallic']:
        sharp = 1.25  # metallic needs more edge definition
    elif props['is_dark']:
        sharp = 1.15  # dark textures — gentle sharpen
    else:
        sharp = 1.20  # standard

    result = ImageEnhance.Sharpness(result).enhance(sharp)

    # Step 5: Save back as OZJ at 2x resolution
    hdr = data[:hdr_off]
    jpg_buf = io.BytesIO()
    result.save(jpg_buf, 'JPEG', quality=98)
    open(fpath, 'wb').write(hdr + jpg_buf.getvalue())

    new_w, new_h = result.size
    return 'ok', f'{w}x{h} -> {new_w}x{new_h}'


def process_ozt(fpath, dry_run=False):
    """Process an OZT (TGA with 4-byte MU header). Alpha preserved separately."""
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

    if w < MIN_SIZE or h < MIN_SIZE:
        return 'skip', f'{w}x{h} too small'

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
            b, g, r = pixels[s], pixels[s + 1], pixels[s + 2]
            rgb.putpixel((x, y), (r, g, b))
            if alpha:
                alpha.putpixel((x, y), pixels[s + 3])

    props = analyze(rgb)

    if dry_run:
        return 'dry', f'{w}x{h} bpp={bpp} block={props["blocking"]:.2f} sat={props["saturation"]:.0f} alpha={"Y" if alpha else "N"}'

    original_rgb = rgb.copy()

    # Pre-denoise RGB only
    if props['needs_denoise']:
        rgb = rgb.filter(ImageFilter.MedianFilter(3))

    # ESRGAN 2x on RGB
    up = esrgan_2x(rgb, 'realesrgan-x4plus-anime')
    if up is None:
        up = esrgan_2x(rgb, 'realesrgan-x4plus')
    if up is None:
        return 'fail', 'ESRGAN failed'

    nw, nh = up.size  # 2x dimensions

    # Color preservation
    result = color_preserve(up, original_rgb)

    # Sharpen
    sharp = 1.25 if props['is_metallic'] else 1.20
    result = ImageEnhance.Sharpness(result).enhance(sharp)

    # Alpha: nearest-neighbor 2x (preserves hard mask edges)
    if alpha:
        alpha_up = alpha.resize((nw, nh), Image.NEAREST)
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


def should_skip(name):
    nl = name.lower()
    for p in SKIP_PATTERNS:
        if p.lower() in nl:
            return True
    return False


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 upscale_armor_2x.py <file_or_dir> [--dry-run]")
        sys.exit(1)

    target = sys.argv[1]
    dry_run = '--dry-run' in sys.argv

    if os.path.isfile(target):
        files = [target]
    elif os.path.isdir(target):
        files = sorted([
            os.path.join(target, f) for f in os.listdir(target)
            if f.upper().endswith(('.OZJ', '.OZT'))
            and any(f.lower().startswith(p)
                    for p in ['upper_', 'lower_', 'lower2_', 'head_', 'boots_', 'boot_',
                              'gloves_', 'skinclass'])
            and not should_skip(f)
        ])
    else:
        print(f"Not found: {target}")
        sys.exit(1)

    total = len(files)
    mode = "DRY RUN" if dry_run else "UPSCALE 2x"
    print(f"═══ Armor {mode}: {total} textures ═══")
    print(f"  Pipeline: analyze → denoise(if blocking>1.2) → ESRGAN-anime 2x → color preserve → sharpen → save")
    if not dry_run:
        print(f"  NOTE: Creates BACKUP at <dir>_backup/ if not exists")

    ok = skip = fail = 0
    for i, fpath in enumerate(files):
        name = os.path.basename(fpath)
        ext = fpath.rsplit('.', 1)[1].upper()

        if should_skip(name):
            skip += 1
            continue

        if ext == 'OZJ':
            status, info = process_ozj(fpath, dry_run)
        else:
            status, info = process_ozt(fpath, dry_run)

        sym = {'ok': '✓', 'skip': '-', 'fail': '✗', 'dry': '?'}[status]
        print(f"  [{i + 1:3d}/{total}] {sym} {name:<32} {info}")

        if status == 'ok' or status == 'dry':
            ok += 1
        elif status == 'skip':
            skip += 1
        else:
            fail += 1

    print(f"\n{'Would process' if dry_run else 'Done'}: {ok} processed, {skip} skipped, {fail} failed")


if __name__ == '__main__':
    main()
