#!/usr/bin/env python3
"""
Smart texture upscaler for MU Online OZJ/OZT assets.
Analyzes each texture and picks the best upscaling strategy.

Categories:
  1. ORGANIC  - trees, leaves, grass, moss (soft detail, natural)
  2. HARD     - stone, brick, metal, wood planks (sharp edges, geometric)
  3. GLOW     - fire, lava, magic effects (bright center, dark edges)
  4. FLAT     - UI elements, signs, simple shapes (minimal detail)
  5. DETAILED - faces, statues, complex objects (preserve fine detail)

Strategies:
  ORGANIC:  8x lanczos -> mild sharpen -> 2x down (soft, no ringing)
  HARD:     4x lanczos -> strong sharpen -> 2x down (crisp edges)
  GLOW:     4x lanczos -> contrast boost -> 2x down (preserve glow)
  FLAT:     2x lanczos -> light sharpen (minimal processing)
  DETAILED: 8x lanczos -> multi-pass sharpen -> 2x down (max detail)
"""

from PIL import Image, ImageFilter, ImageEnhance, ImageStat
import io, os, sys, numpy as np, shutil

def analyze_texture(img):
    """Classify texture by analyzing pixel statistics."""
    arr = np.array(img.convert('RGB')).astype(float)
    h, w = arr.shape[:2]
    
    # Basic stats
    brightness = np.max(arr, axis=2)
    mean_bright = brightness.mean()
    std_bright = brightness.std()
    
    # Color saturation (max - min channel per pixel)
    saturation = np.max(arr, axis=2) - np.min(arr, axis=2)
    mean_sat = saturation.mean()
    
    # Edge density (Laplacian-like measure)
    gray = np.mean(arr, axis=2)
    if h > 2 and w > 2:
        dx = np.abs(np.diff(gray, axis=1))
        dy = np.abs(np.diff(gray, axis=0))
        edge_density = (dx.mean() + dy.mean()) / 2
    else:
        edge_density = 0
    
    # Dark percentage (glow detection)
    dark_pct = (brightness < 30).sum() / brightness.size * 100
    bright_pct = (brightness > 200).sum() / brightness.size * 100
    
    # Dominant color hue
    r_mean, g_mean, b_mean = arr[:,:,0].mean(), arr[:,:,1].mean(), arr[:,:,2].mean()
    is_warm = r_mean > g_mean * 1.2 and r_mean > b_mean * 1.3  # fire/lava
    is_green = g_mean > r_mean * 1.1 and g_mean > b_mean * 1.1  # vegetation
    
    # Classification logic
    if dark_pct > 40 and bright_pct > 5 and is_warm:
        return 'GLOW', f'dark={dark_pct:.0f}% warm, fire/lava'
    
    if dark_pct > 60:
        return 'GLOW', f'dark={dark_pct:.0f}%, dark glow/effect'
    
    if is_green and edge_density < 15:
        return 'ORGANIC', f'green, soft edges={edge_density:.1f}'
    
    if edge_density > 20 and std_bright > 50:
        return 'DETAILED', f'edges={edge_density:.1f} std={std_bright:.0f}'
    
    if edge_density > 15 and mean_sat < 40:
        return 'HARD', f'edges={edge_density:.1f} low_sat={mean_sat:.0f}'
    
    if edge_density < 8 and std_bright < 30:
        return 'FLAT', f'edges={edge_density:.1f} std={std_bright:.0f}'
    
    if is_green or mean_sat > 30:
        return 'ORGANIC', f'sat={mean_sat:.0f} green={is_green}'
    
    if edge_density > 12:
        return 'HARD', f'edges={edge_density:.1f}'
    
    return 'DETAILED', f'default edges={edge_density:.1f} std={std_bright:.0f}'


SCALE = 4  # 4x upscale for maximum sharpness


def upscale_organic(img, w, h):
    """Soft upscale for vegetation - natural, no ringing."""
    s = SCALE
    big = img.resize((w*s*4, h*s*4), Image.LANCZOS)
    big = big.filter(ImageFilter.UnsharpMask(radius=4.0, percent=100, threshold=4))
    result = big.resize((w*s, h*s), Image.LANCZOS)
    result = result.filter(ImageFilter.UnsharpMask(radius=1.0, percent=80, threshold=3))
    return result


def upscale_hard(img, w, h):
    """Crisp upscale for stone/metal - strong edge preservation."""
    s = SCALE
    big = img.resize((w*s*4, h*s*4), Image.LANCZOS)
    big = big.filter(ImageFilter.EDGE_ENHANCE)
    big = big.filter(ImageFilter.UnsharpMask(radius=2.0, percent=200, threshold=2))
    result = big.resize((w*s, h*s), Image.LANCZOS)
    result = result.filter(ImageFilter.UnsharpMask(radius=1.0, percent=150, threshold=2))
    return result


def upscale_glow(img, w, h):
    """Preserve glow/fire - boost contrast, soft glow."""
    s = SCALE
    big = img.resize((w*s*4, h*s*4), Image.LANCZOS)
    big = big.filter(ImageFilter.UnsharpMask(radius=3.0, percent=120, threshold=5))
    result = big.resize((w*s, h*s), Image.LANCZOS)
    result = ImageEnhance.Contrast(result).enhance(1.2)
    result = result.filter(ImageFilter.UnsharpMask(radius=0.8, percent=60, threshold=4))
    return result


def upscale_flat(img, w, h):
    """Clean upscale for simple shapes - minimal artifacts."""
    s = SCALE
    big = img.resize((w*s*4, h*s*4), Image.LANCZOS)
    big = big.filter(ImageFilter.UnsharpMask(radius=2.0, percent=100, threshold=3))
    result = big.resize((w*s, h*s), Image.LANCZOS)
    result = result.filter(ImageFilter.UnsharpMask(radius=0.5, percent=60, threshold=4))
    return result


def upscale_detailed(img, w, h):
    """Maximum detail preservation for complex textures."""
    s = SCALE
    big = img.resize((w*s*4, h*s*4), Image.LANCZOS)
    big = big.filter(ImageFilter.DETAIL)
    big = big.filter(ImageFilter.UnsharpMask(radius=3.0, percent=180, threshold=2))
    big = big.filter(ImageFilter.UnsharpMask(radius=1.0, percent=100, threshold=3))
    result = big.resize((w*s, h*s), Image.LANCZOS)
    result = result.filter(ImageFilter.UnsharpMask(radius=0.8, percent=120, threshold=2))
    return result


STRATEGIES = {
    'ORGANIC':  upscale_organic,
    'HARD':     upscale_hard,
    'GLOW':     upscale_glow,
    'FLAT':     upscale_flat,
    'DETAILED': upscale_detailed,
}


def write_ozt(path, img, prefix, bpp):
    """Write OZT with correct format (bottom-left origin, BGRA)."""
    arr = np.array(img)
    new_w, new_h = img.size
    
    tga_header = bytearray(18)
    tga_header[2] = 2  # Uncompressed true-color
    tga_header[12] = new_w & 0xFF; tga_header[13] = (new_w >> 8) & 0xFF
    tga_header[14] = new_h & 0xFF; tga_header[15] = (new_h >> 8) & 0xFF
    tga_header[16] = bpp
    tga_header[17] = 0x00  # Bottom-left origin
    
    bytes_pp = bpp // 8
    pixels = bytearray(new_w * new_h * bytes_pp)
    for y in range(new_h):
        src_y = new_h - 1 - y  # Flip for bottom-left
        for x in range(new_w):
            idx = (y * new_w + x) * bytes_pp
            if bpp == 32:
                pixels[idx] = arr[src_y, x, 2]
                pixels[idx+1] = arr[src_y, x, 1]
                pixels[idx+2] = arr[src_y, x, 0]
                pixels[idx+3] = arr[src_y, x, 3]
            else:
                pixels[idx] = arr[src_y, x, 2]
                pixels[idx+1] = arr[src_y, x, 1]
                pixels[idx+2] = arr[src_y, x, 0]
    
    with open(path, 'wb') as f:
        if prefix:
            f.write(prefix)
        f.write(bytes(tga_header))
        f.write(bytes(pixels))


def process_directory(obj_dir, dry_run=False):
    """Process all textures in a directory."""
    backup_dir = obj_dir.rstrip('/') + '_backup'
    if not dry_run:
        os.makedirs(backup_dir, exist_ok=True)
    
    stats = {'ORGANIC': 0, 'HARD': 0, 'GLOW': 0, 'FLAT': 0, 'DETAILED': 0}
    upscaled = 0
    skipped = 0
    
    for fname in sorted(os.listdir(obj_dir)):
        is_ozj = fname.upper().endswith('.OZJ')
        is_ozt = fname.upper().endswith('.OZT')
        if not (is_ozj or is_ozt):
            continue
        
        path = os.path.join(obj_dir, fname)
        
        # Backup
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
        
        # Analyze and classify
        rgb = img.convert('RGB')
        category, reason = analyze_texture(rgb)
        stats[category] += 1
        
        if dry_run:
            print(f'  [{category:8s}] {fname}: {w}x{h} -> {w*SCALE}x{h*SCALE}  ({reason})')
            continue

        # Upscale RGB
        strategy = STRATEGIES[category]
        rgb_up = strategy(rgb, w, h)
        new_w, new_h = w * SCALE, h * SCALE
        
        if is_ozt:
            has_alpha = img.mode == 'RGBA'
            if has_alpha:
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
                f.write(header)
                f.write(buf.getvalue())
        
        print(f'  [{category:8s}] {fname}: {w}x{h} -> {new_w}x{new_h}')
        upscaled += 1
    
    print(f'\nStats: {stats}')
    print(f'Total: {upscaled} upscaled, {skipped} skipped')
    return upscaled


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: smart_upscale.py <ObjectDir> [--dry-run]')
        print('  e.g.: smart_upscale.py client/Data/Object1')
        sys.exit(1)
    
    obj_dir = sys.argv[1]
    dry_run = '--dry-run' in sys.argv
    
    if dry_run:
        print(f'DRY RUN — analyzing {obj_dir}:\n')
    else:
        print(f'Upscaling {obj_dir}:\n')
    
    process_directory(obj_dir, dry_run)
