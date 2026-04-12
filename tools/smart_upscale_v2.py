#!/usr/bin/env python3
"""
Smart Texture Upscaler v2 — EDSR neural super-resolution.
Uses OpenCV DNN EDSR x4 model for true AI upscaling.

Usage:
  python3 smart_upscale_v2.py <ObjectDir> [--dry-run]
"""

import cv2
import numpy as np
from PIL import Image, ImageFilter, ImageEnhance
import io, os, sys, shutil, time

SCALE = 4
MODEL_PATH = os.path.join(os.path.dirname(__file__), 'sr_models/EDSR_x4.pb')


def load_model():
    sr = cv2.dnn_superres.DnnSuperResImpl_create()
    sr.readModel(MODEL_PATH)
    sr.setModel('edsr', 4)
    return sr


def analyze_texture(arr_rgb):
    """Classify for post-processing."""
    brightness = np.max(arr_rgb, axis=2)
    dark_pct = (brightness < 30).sum() / brightness.size * 100
    bright_pct = (brightness > 200).sum() / brightness.size * 100
    r, g, b = arr_rgb[:,:,0].mean(), arr_rgb[:,:,1].mean(), arr_rgb[:,:,2].mean()
    is_warm = r > g * 1.2 and r > b * 1.3
    if dark_pct > 40 and is_warm:
        return 'GLOW'
    if dark_pct > 60:
        return 'GLOW'
    return 'NORMAL'


def upscale_rgb(sr, img_pil):
    """EDSR 4x upscale with category-specific post-processing."""
    arr = np.array(img_pil)
    category = analyze_texture(arr)
    bgr = arr[:,:,::-1]

    result_bgr = sr.upsample(bgr)
    result = Image.fromarray(result_bgr[:,:,::-1])

    # Post-processing based on texture type
    if category == 'GLOW':
        result = ImageEnhance.Contrast(result).enhance(1.15)
        result = result.filter(ImageFilter.UnsharpMask(radius=0.8, percent=50, threshold=4))
    else:
        # Light sharpen to enhance EDSR output
        result = result.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=3))

    return result, category


def write_ozt(path, img, prefix, bpp):
    arr = np.array(img)
    new_w, new_h = img.size
    tga_header = bytearray(18)
    tga_header[2] = 2
    tga_header[12] = new_w & 0xFF; tga_header[13] = (new_w >> 8) & 0xFF
    tga_header[14] = new_h & 0xFF; tga_header[15] = (new_h >> 8) & 0xFF
    tga_header[16] = bpp; tga_header[17] = 0x00

    bytes_pp = bpp // 8
    pixels = bytearray(new_w * new_h * bytes_pp)
    for y in range(new_h):
        src_y = new_h - 1 - y
        for x in range(new_w):
            idx = (y * new_w + x) * bytes_pp
            if bpp == 32:
                pixels[idx] = arr[src_y,x,2]; pixels[idx+1] = arr[src_y,x,1]
                pixels[idx+2] = arr[src_y,x,0]; pixels[idx+3] = arr[src_y,x,3]
            else:
                pixels[idx] = arr[src_y,x,2]; pixels[idx+1] = arr[src_y,x,1]
                pixels[idx+2] = arr[src_y,x,0]

    with open(path, 'wb') as f:
        if prefix: f.write(prefix)
        f.write(bytes(tga_header)); f.write(bytes(pixels))


def process_directory(obj_dir, dry_run=False):
    backup_dir = obj_dir.rstrip('/') + '_backup'
    if not dry_run:
        os.makedirs(backup_dir, exist_ok=True)

    if not dry_run:
        print('Loading EDSR model...')
        sr = load_model()
        print('Model ready.\n')
    else:
        sr = None

    upscaled = 0
    skipped = 0
    total_time = 0

    for fname in sorted(os.listdir(obj_dir)):
        is_ozj = fname.upper().endswith('.OZJ')
        is_ozt = fname.upper().endswith('.OZT')
        if not (is_ozj or is_ozt): continue

        path = os.path.join(obj_dir, fname)
        backup_path = os.path.join(backup_dir, fname)
        if not dry_run and not os.path.exists(backup_path):
            shutil.copy2(path, backup_path)

        with open(path, 'rb') as f:
            data = f.read()

        try:
            if is_ozj:
                header = data[:24]
                img = Image.open(io.BytesIO(data[24:]))
                ozt_prefix = None
            else:
                if len(data) > 6 and (data[4+2] == 2 or data[4+2] == 10):
                    ozt_prefix = data[:4]
                    img = Image.open(io.BytesIO(data[4:]))
                elif len(data) > 4 and (data[2] == 2 or data[2] == 10):
                    ozt_prefix = b''
                    img = Image.open(io.BytesIO(data))
                else:
                    skipped += 1; continue
        except:
            skipped += 1; continue

        w, h = img.size
        if w < 16 or h < 16:
            skipped += 1; continue
        if w >= 512 or h >= 512:
            skipped += 1; continue

        if dry_run:
            print(f'  {fname}: {w}x{h} -> {w*SCALE}x{h*SCALE}')
            upscaled += 1; continue

        t0 = time.time()

        rgb = img.convert('RGB')
        rgb_up, category = upscale_rgb(sr, rgb)
        new_w, new_h = w * SCALE, h * SCALE

        if is_ozt:
            if img.mode == 'RGBA':
                alpha = img.split()[3]
                alpha_up = alpha.resize((new_w, new_h), Image.LANCZOS)
                result = Image.merge('RGBA', (*rgb_up.split(), alpha_up))
                write_ozt(path, result, ozt_prefix, 32)
            else:
                write_ozt(path, rgb_up, ozt_prefix, 24)
        else:
            buf = io.BytesIO()
            rgb_up.save(buf, format='JPEG', quality=95)
            with open(path, 'wb') as f:
                f.write(header); f.write(buf.getvalue())

        elapsed = time.time() - t0
        total_time += elapsed
        print(f'  [{category:6s}] {fname}: {w}x{h} -> {new_w}x{new_h}  ({elapsed:.1f}s)')
        upscaled += 1

    print(f'\nDone: {upscaled} upscaled, {skipped} skipped, {total_time:.0f}s total')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: smart_upscale_v2.py <ObjectDir> [--dry-run]')
        sys.exit(1)

    obj_dir = sys.argv[1]
    dry_run = '--dry-run' in sys.argv
    process_directory(obj_dir, dry_run)
