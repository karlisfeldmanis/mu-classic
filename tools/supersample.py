#!/usr/bin/env python3
"""
Supersample MU Online textures in-place (OZJ + OZT with alpha preservation).
Upscales 4x with ESRGAN then downscales back to original size → cleaner JPEG/TGA.

Usage: python3 tools/supersample.py <dir> [--skip-light]
"""
import struct, io, os, subprocess, sys
from PIL import Image

ESRGAN = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/models"
MIN_SIZE = 16

# Textures that should NOT be supersampled (light/fire/glow — ESRGAN darkens them)
SKIP_NAMES = {'light', 'light2', 'light3', 'light_02', 'fire_01', 'fire_02',
              'fire_light_01', 'candle', 'candle2', 'Mcloud_R',
              'streetlight_brightness2', 'songk3crystal_R', 'songk3matereff_R',
              'songk3mmater0302_R', 'tile_windows01'}

def esrgan_4x(input_png, output_png):
    rc = subprocess.run([ESRGAN, '-i', input_png, '-o', output_png,
                        '-s', '2', '-n', 'realesrgan-x4plus', '-m', MODEL_DIR],
                       capture_output=True).returncode
    return rc == 0

def supersample_ozj(fpath):
    """Supersample OZJ: PIL decode → ESRGAN 4x → LANCZOS downscale → JPEG re-wrap."""
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

    png = f'/tmp/ss_{name}.png'
    up_png = f'/tmp/ss_{name}_up.png'
    img.save(png)

    if not esrgan_4x(png, up_png):
        os.unlink(png)
        return 'fail'

    up = Image.open(up_png).convert('RGB')
    down = up.resize(img.size, Image.LANCZOS)

    final_jpg = f'/tmp/ss_{name}_final.jpg'
    down.save(final_jpg, 'JPEG', quality=98)

    hdr = data[:hdr_off]
    jpg = open(final_jpg, 'rb').read()
    open(fpath, 'wb').write(hdr + jpg)

    for f in [png, up_png, final_jpg]:
        try: os.unlink(f)
        except: pass
    return 'ok'

def supersample_ozt(fpath):
    """Supersample OZT: split RGB+Alpha → ESRGAN RGB → downscale → rebuild TGA."""
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

    png = f'/tmp/ss_{name}_rgb.png'
    up_png = f'/tmp/ss_{name}_rgb_up.png'
    rgb.save(png)

    if not esrgan_4x(png, up_png):
        os.unlink(png)
        return 'fail'

    # Downscale RGB back to original size
    rgb_up = Image.open(up_png).convert('RGB')
    rgb_down = rgb_up.resize((w, h), Image.LANCZOS)

    # Rebuild BGRA TGA (uncompressed, bottom-to-top)
    pixel_data = bytearray(w * h * bpc)
    for y in range(h):
        for x in range(w):
            r, g, b = rgb_down.getpixel((x, y))
            dst = ((h-1-y)*w + x) * bpc
            pixel_data[dst] = b
            pixel_data[dst+1] = g
            pixel_data[dst+2] = r
            if alpha:
                pixel_data[dst+3] = alpha.getpixel((x, y))

    # TGA header (uncompressed type 2)
    tga_hdr = bytearray(18)
    tga_hdr[2] = 2
    struct.pack_into('<H', tga_hdr, 12, w)
    struct.pack_into('<H', tga_hdr, 14, h)
    tga_hdr[16] = bpp

    open(fpath, 'wb').write(mu_hdr + tga_hdr + pixel_data)

    for f in [png, up_png]:
        try: os.unlink(f)
        except: pass
    return 'ok'

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 supersample.py <dir> [--skip-light]")
        sys.exit(1)

    target_dir = sys.argv[1]
    skip_light = '--skip-light' in sys.argv

    if not os.path.isdir(target_dir):
        print(f"Not a directory: {target_dir}")
        sys.exit(1)

    files = sorted(os.listdir(target_dir))
    ozj_files = [f for f in files if f.upper().endswith('.OZJ')]
    ozt_files = [f for f in files if f.upper().endswith('.OZT')]

    total = len(ozj_files) + len(ozt_files)
    print(f"═══ Supersample: {target_dir} ({len(ozj_files)} OZJ + {len(ozt_files)} OZT = {total}) ═══")

    count = skip = fail = 0

    for i, f in enumerate(ozj_files + ozt_files):
        name = f.rsplit('.', 1)[0]
        ext = f.rsplit('.', 1)[1].upper()
        fpath = os.path.join(target_dir, f)

        # Skip light/fire textures
        if skip_light and name.lower() in {n.lower() for n in SKIP_NAMES}:
            skip += 1
            continue

        if ext == 'OZJ':
            result = supersample_ozj(fpath)
        else:
            result = supersample_ozt(fpath)

        if result == 'ok':
            count += 1
        elif result == 'skip':
            skip += 1
        else:
            fail += 1

        if (i+1) % 20 == 0:
            print(f"  {i+1}/{total}...")

    print(f"\nDone: {count} supersampled, {skip} skipped, {fail} failed")

if __name__ == '__main__':
    main()
