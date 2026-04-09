#!/usr/bin/env python3
"""Supersample terrain tiles in-place using Real-ESRGAN x4plus.
Method: tile 2x2 → upscale 4x → crop center → downsample back to ORIGINAL size.
This denoises, removes JPEG artifacts, and sharpens detail while keeping
the exact same pixel dimensions. No engine changes needed.
Re-wraps result back into OZJ format (original header + enhanced JPEG).
"""
import os, sys, io, shutil, subprocess, tempfile
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ESRGAN = os.path.join(SCRIPT_DIR, "realesrgan", "realesrgan-ncnn-vulkan")
MODEL_DIR = os.path.join(SCRIPT_DIR, "realesrgan", "models")
# No fixed target — each tile keeps its original resolution

def extract_ozj(path):
    """Returns (header_bytes, PIL.Image) from OZJ file."""
    data = open(path, 'rb').read()
    if len(data) > 25 and data[24] == 0xFF and data[25] == 0xD8:
        off = 24
    elif data[0] == 0xFF and data[1] == 0xD8:
        off = 0
    else:
        raise ValueError(f"Can't find JPEG in {path}")
    hdr = data[:off]
    img = Image.open(io.BytesIO(data[off:]))
    return hdr, img

def wrap_ozj(path, hdr, img, quality=95):
    """Write PIL Image back as OZJ (header + JPEG)."""
    buf = io.BytesIO()
    img.save(buf, format='JPEG', quality=quality)
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(buf.getvalue())

def upscale_tile(src_path, tmpdir):
    """Upscale a single tile OZJ to TARGET_RES x TARGET_RES, seamless, in-place."""
    name = os.path.splitext(os.path.basename(src_path))[0]

    hdr, orig = extract_ozj(src_path)
    w, h = orig.size

    if w < 32 or h < 32:
        return None, "too small"

    # Tile 2x2 for seamless edges
    tiled = Image.new('RGB', (w * 2, h * 2))
    tiled.paste(orig, (0, 0))
    tiled.paste(orig, (w, 0))
    tiled.paste(orig, (0, h))
    tiled.paste(orig, (w, h))

    tiled_path = os.path.join(tmpdir, f"{name}_tiled.png")
    up_path = os.path.join(tmpdir, f"{name}_up.png")
    tiled.save(tiled_path)

    # Always upscale 4x with x4plus (best quality for organic textures)
    result = subprocess.run([
        ESRGAN, "-i", tiled_path, "-o", up_path,
        "-s", "4", "-n", "realesrgan-x4plus", "-m", MODEL_DIR
    ], capture_output=True)

    if result.returncode != 0:
        return None, "esrgan failed"

    # Crop center tile from 4x upscaled 2x2 grid
    up = Image.open(up_path)
    uw, uh = up.size
    tw, th = uw // 2, uh // 2
    cropped = up.crop((tw // 2, th // 2, tw // 2 + tw, th // 2 + th))
    # cropped is now 4x the original single tile

    # Downsample back to original size (supersample = sharper than original)
    if cropped.width != w or cropped.height != h:
        cropped = cropped.resize((w, h), Image.LANCZOS)

    # Write back as OZJ
    wrap_ozj(src_path, hdr, cropped)

    return (w, h), None

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 upscale_world_tiles.py <WorldDir> [WorldDir2 ...]")
        print("Example: python3 upscale_world_tiles.py Data/World1 Data/World7 Data/World9")
        sys.exit(1)

    worlds = sys.argv[1:]
    tmpdir = tempfile.mkdtemp()

    total_ok = 0
    total_skip = 0
    total_fail = 0

    for world_dir in worlds:
        if not os.path.isdir(world_dir):
            print(f"  {world_dir}: not found, skipping")
            continue

        # Create backup
        backup_dir = world_dir + "_backup"
        if not os.path.isdir(backup_dir):
            os.makedirs(backup_dir, exist_ok=True)
            print(f"  Backing up {world_dir} → {backup_dir}/")

        tiles = sorted(f for f in os.listdir(world_dir)
                       if f.startswith('Tile') and f.upper().endswith('.OZJ'))

        print(f"\n── {world_dir} ({len(tiles)} tiles) ──")

        for fname in tiles:
            src = os.path.join(world_dir, fname)
            name = os.path.splitext(fname)[0]
            bak = os.path.join(backup_dir, fname)

            # Check original size
            try:
                _, orig = extract_ozj(src)
                orig_size = f"{orig.width}x{orig.height}"
            except:
                print(f"  {name}: can't read, skipping")
                total_fail += 1
                continue

            # Skip if already processed (backup exists)
            if os.path.exists(bak):
                print(f"  {name}: already processed ({orig_size})")
                total_skip += 1
                continue

            # Backup before modifying
            if not os.path.exists(bak):
                shutil.copy2(src, bak)

            # Upscale
            new_size, err = upscale_tile(src, tmpdir)
            if err:
                print(f"  {name}: {err}")
                total_fail += 1
            else:
                print(f"  {name}: {orig_size} → {new_size[0]}x{new_size[1]}")
                total_ok += 1

            # Clean temp files
            for f in os.listdir(tmpdir):
                os.remove(os.path.join(tmpdir, f))

    shutil.rmtree(tmpdir)
    print(f"\nDone: {total_ok} upscaled, {total_skip} skipped, {total_fail} failed")
    if total_ok > 0:
        print("Originals backed up to *_backup/ directories")

if __name__ == "__main__":
    main()
