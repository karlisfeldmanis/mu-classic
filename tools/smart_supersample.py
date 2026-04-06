#!/usr/bin/env python3
"""
Smart Supersample — learns from original texture before upscaling.

Pipeline:
1. Analyze original (color histogram, brightness, JPEG artifacts)
2. Pre-process: median denoise if JPEG artifacts detected
3. ESRGAN 2x upscale
4. Post-process: histogram-match colors to original (prevents color drift)
5. Sharpen to recover detail lost in upscale
6. Downscale back to original size with LANCZOS
7. Re-encode in original format (OZJ/OZT) with preserved headers

Usage: python3 smart_supersample.py <file_or_dir> [--force]
"""
import struct, io, os, subprocess, sys
from PIL import Image, ImageFilter, ImageEnhance, ImageStat

ESRGAN = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/models"
MIN_SIZE = 16

# Light/glow textures — skip (ESRGAN darkens them)
SKIP_PATTERNS = ['light', 'fire_0', 'candle', 'brightness', '_R', 'glow', 'flare']


def histogram_match(source, reference):
    """Match source image histogram to reference — preserves original color distribution."""
    result = Image.new('RGB', source.size)

    for ch in range(3):
        src_ch = source.split()[ch]
        ref_ch = reference.split()[ch]

        # Build cumulative histograms
        src_hist = src_ch.histogram()
        ref_hist = ref_ch.histogram()

        src_cdf = []
        ref_cdf = []
        s = 0
        for v in src_hist:
            s += v
            src_cdf.append(s)
        s = 0
        for v in ref_hist:
            s += v
            ref_cdf.append(s)

        # Normalize
        src_total = src_cdf[-1] if src_cdf[-1] > 0 else 1
        ref_total = ref_cdf[-1] if ref_cdf[-1] > 0 else 1
        src_cdf = [v / src_total for v in src_cdf]
        ref_cdf = [v / ref_total for v in ref_cdf]

        # Build LUT: for each source value, find closest reference value
        lut = []
        for src_val in range(256):
            target_cdf = src_cdf[src_val]
            best = 0
            best_diff = abs(ref_cdf[0] - target_cdf)
            for ref_val in range(256):
                diff = abs(ref_cdf[ref_val] - target_cdf)
                if diff < best_diff:
                    best_diff = diff
                    best = ref_val
            lut.append(best)

        result_ch = src_ch.point(lut)
        result.paste(result_ch, mask=None, box=None)
        # Rebuild channel by channel

    # Merge channels properly
    r, g, b = source.split()
    for i, ch in enumerate([r, g, b]):
        ref_ch = reference.split()[i]
        src_hist = ch.histogram()
        ref_hist = ref_ch.histogram()
        src_cdf = []
        ref_cdf = []
        s = 0
        for v in src_hist:
            s += v
            src_cdf.append(s)
        s = 0
        for v in ref_hist:
            s += v
            ref_cdf.append(s)
        src_total = max(src_cdf[-1], 1)
        ref_total = max(ref_cdf[-1], 1)
        src_cdf = [v / src_total for v in src_cdf]
        ref_cdf = [v / ref_total for v in ref_cdf]
        lut = []
        for sv in range(256):
            tc = src_cdf[sv]
            best = 0
            best_d = 999
            for rv in range(256):
                d = abs(ref_cdf[rv] - tc)
                if d < best_d:
                    best_d = d
                    best = rv
            lut.append(best)
        if i == 0: r = ch.point(lut)
        elif i == 1: g = ch.point(lut)
        else: b = ch.point(lut)

    return Image.merge('RGB', (r, g, b))


def detect_jpeg_blocking(img):
    """Return artifact ratio (>1.3 = visible blocking)."""
    gray = img.convert('L')
    bh = 0; bc = 0; nbh = 0; nbc = 0
    for y in range(0, min(img.size[1], 64)):  # sample first 64 rows for speed
        for x in range(img.size[0]-1):
            p = gray.getpixel((x, y))
            pr = gray.getpixel((x+1, y))
            diff = abs(p - pr)
            if x % 8 == 7:
                bh += diff; bc += 1
            else:
                nbh += diff; nbc += 1
    return (bh / max(bc, 1)) / max(nbh / max(nbc, 1), 0.01)


def smart_supersample_ozj(fpath, force=False):
    name = os.path.basename(fpath).rsplit('.', 1)[0]
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

    if img is None or img.size[0] < MIN_SIZE or img.size[1] < MIN_SIZE:
        return 'skip'

    original = img.copy()

    # Step 1: Analyze
    blocking = detect_jpeg_blocking(img)

    # Step 2: Pre-process if needed
    if blocking > 1.3:
        img = img.filter(ImageFilter.MedianFilter(3))

    # Step 3: ESRGAN 2x
    png = f'/tmp/ss_{name}.png'
    up_png = f'/tmp/ss_{name}_up.png'
    img.save(png)

    rc = subprocess.run([ESRGAN, '-i', png, '-o', up_png, '-s', '2',
                        '-n', 'realesrgan-x4plus', '-m', MODEL_DIR],
                       capture_output=True).returncode
    if rc != 0:
        try: os.unlink(png)
        except: pass
        return 'fail'

    up = Image.open(up_png).convert('RGB')

    # Step 4: Downscale back to original size
    down = up.resize(original.size, Image.LANCZOS)

    # Step 5: Histogram match to original colors
    matched = histogram_match(down, original)

    # Step 6: Slight sharpen to recover crispness
    final = ImageEnhance.Sharpness(matched).enhance(1.15)

    # Step 7: Re-encode
    final_jpg = f'/tmp/ss_{name}_final.jpg'
    final.save(final_jpg, 'JPEG', quality=98)

    hdr = data[:hdr_off]
    jpg = open(final_jpg, 'rb').read()
    open(fpath, 'wb').write(hdr + jpg)

    for f in [png, up_png, final_jpg]:
        try: os.unlink(f)
        except: pass
    return 'ok'


def smart_supersample_ozt(fpath, force=False):
    name = os.path.basename(fpath).rsplit('.', 1)[0]
    data = open(fpath, 'rb').read()

    mu_hdr = data[:4]
    tga = data[4:]
    if len(tga) < 18:
        return 'skip'

    w = struct.unpack('<H', tga[12:14])[0]
    h = struct.unpack('<H', tga[14:16])[0]
    bpp = tga[16]
    img_type = tga[2]

    if w < MIN_SIZE or h < MIN_SIZE:
        return 'skip'

    # Decompress RLE
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
    if len(pixels) < w * h * bpc:
        return 'fail'

    # Split RGB + Alpha
    rgb = Image.new('RGB', (w, h))
    alpha = Image.new('L', (w, h)) if bpc == 4 else None
    for y in range(h):
        for x in range(w):
            s = ((h-1-y)*w + x) * bpc
            b, g, r = pixels[s], pixels[s+1], pixels[s+2]
            rgb.putpixel((x, y), (r, g, b))
            if alpha:
                alpha.putpixel((x, y), pixels[s+3])

    original_rgb = rgb.copy()

    # ESRGAN 2x on RGB only
    png = f'/tmp/ss_{name}_rgb.png'
    up_png = f'/tmp/ss_{name}_rgb_up.png'
    rgb.save(png)

    rc = subprocess.run([ESRGAN, '-i', png, '-o', up_png, '-s', '2',
                        '-n', 'realesrgan-x4plus', '-m', MODEL_DIR],
                       capture_output=True).returncode
    if rc != 0:
        try: os.unlink(png)
        except: pass
        return 'fail'

    rgb_up = Image.open(up_png).convert('RGB')
    rgb_down = rgb_up.resize((w, h), Image.LANCZOS)

    # Histogram match + sharpen
    matched = histogram_match(rgb_down, original_rgb)
    final_rgb = ImageEnhance.Sharpness(matched).enhance(1.15)

    # Rebuild TGA with original alpha (untouched)
    pixel_data = bytearray(w * h * bpc)
    for y in range(h):
        for x in range(w):
            r, g, b = final_rgb.getpixel((x, y))
            dst = ((h-1-y)*w + x) * bpc
            pixel_data[dst] = b
            pixel_data[dst+1] = g
            pixel_data[dst+2] = r
            if alpha:
                pixel_data[dst+3] = alpha.getpixel((x, y))

    tga_hdr = bytearray(18)
    tga_hdr[2] = 2  # uncompressed
    struct.pack_into('<H', tga_hdr, 12, w)
    struct.pack_into('<H', tga_hdr, 14, h)
    tga_hdr[16] = bpp

    open(fpath, 'wb').write(mu_hdr + tga_hdr + pixel_data)

    for f in [png, up_png]:
        try: os.unlink(f)
        except: pass
    return 'ok'


def should_skip(name):
    nl = name.lower()
    for p in SKIP_PATTERNS:
        if p.lower() in nl:
            return True
    return False


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 smart_supersample.py <file_or_dir> [--force]")
        sys.exit(1)

    target = sys.argv[1]
    force = '--force' in sys.argv

    if os.path.isfile(target):
        files = [target]
    elif os.path.isdir(target):
        files = sorted([os.path.join(target, f) for f in os.listdir(target)
                        if f.upper().endswith(('.OZJ', '.OZT'))])
    else:
        print(f"Not found: {target}")
        sys.exit(1)

    total = len(files)
    print(f"═══ Smart Supersample: {total} textures ═══")
    print(f"  Pipeline: analyze → denoise(if needed) → ESRGAN 2x → histogram match → sharpen → downscale")

    count = skip = fail = 0
    for i, fpath in enumerate(files):
        name = os.path.basename(fpath).rsplit('.', 1)[0]
        ext = fpath.rsplit('.', 1)[1].upper()

        if should_skip(name):
            skip += 1
            continue

        if ext == 'OZJ':
            result = smart_supersample_ozj(fpath, force)
        else:
            result = smart_supersample_ozt(fpath, force)

        if result == 'ok': count += 1
        elif result == 'skip': skip += 1
        else: fail += 1

        if (i+1) % 10 == 0:
            print(f"  {i+1}/{total}...")

    print(f"\nDone: {count} supersampled, {skip} skipped, {fail} failed")


if __name__ == '__main__':
    main()
