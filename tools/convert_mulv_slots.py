#!/usr/bin/env python3
"""
Convert mu.lv equipment slot OZT files to PNG and copy to client Interface dir.
"""
import struct, os

SRC = "/Users/karlisfeldmanis/Desktop/mu_remaster/references/mu.lv/Data/Interface"
DST = "/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/Interface"

# Map: output PNG name -> source OZT name in mu.lv
SLOT_MAP = {
    "newui_item_weapon_r.png": "newui_item_weapon(R).OZT",
    "newui_item_weapon_l.png": "newui_item_weapon(L).OZT",
    "newui_item_cap.png":      "newui_item_cap.OZT",
    "newui_item_upper.png":    "newui_item_upper.OZT",
    "newui_item_lower.png":    "newui_item_lower.OZT",
    "newui_item_gloves.png":   "newui_item_gloves.OZT",
    "newui_item_boots.png":    "newui_item_boots.OZT",
    "newui_item_wing.png":     "newui_item_wing.OZT",
    "newui_item_fairy.png":    "newui_item_fairy.OZT",
    "newui_item_necklace.png": "newui_item_necklace.OZT",
    "newui_item_ring.png":     "newui_item_ring.OZT",
}

from PIL import Image
import numpy as np


def decode_ozt(path):
    """Decode MU Online OZT (TGA with 4-byte header) to RGBA PIL Image."""
    with open(path, "rb") as f:
        data = f.read()

    # Find TGA header offset
    offset = 0
    if len(data) > 24:
        if data[4+2] in (2, 10):
            offset = 4
        elif data[24+2] in (2, 10):
            offset = 24

    if offset + 18 > len(data):
        raise ValueError(f"Too small: {path}")

    hdr = data[offset:]
    img_type = hdr[2]
    w = hdr[12] | (hdr[13] << 8)
    h = hdr[14] | (hdr[15] << 8)
    bpp = hdr[16]

    if bpp not in (24, 32):
        raise ValueError(f"Unsupported bpp={bpp} in {path}")

    bpp_bytes = bpp // 8
    pixel_offset = offset + 18
    expected = w * h * bpp_bytes

    if img_type == 10:
        # RLE compressed
        pixels = bytearray(expected)
        src = pixel_offset
        dst = 0
        while dst < expected and src < len(data):
            ph = data[src]; src += 1
            count = (ph & 0x7F) + 1
            if ph & 0x80:
                pixel = data[src:src+bpp_bytes]; src += bpp_bytes
                for _ in range(count):
                    if dst + bpp_bytes <= expected:
                        pixels[dst:dst+bpp_bytes] = pixel
                        dst += bpp_bytes
            else:
                nbytes = count * bpp_bytes
                if src + nbytes <= len(data) and dst + nbytes <= expected:
                    pixels[dst:dst+nbytes] = data[src:src+nbytes]
                    src += nbytes
                    dst += nbytes
    else:
        pixels = bytearray(data[pixel_offset:pixel_offset+expected])

    # Convert to numpy
    arr = np.frombuffer(bytes(pixels), dtype=np.uint8).reshape((h, w, bpp_bytes))
    
    # Flip vertically (TGA is bottom-to-top)
    arr = arr[::-1].copy()
    
    # Convert BGR(A) to RGBA
    if bpp == 32:
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:,:,0] = arr[:,:,2]  # R from B
        rgba[:,:,1] = arr[:,:,1]  # G
        rgba[:,:,2] = arr[:,:,0]  # B from R
        rgba[:,:,3] = arr[:,:,3]  # A
    else:
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:,:,0] = arr[:,:,2]
        rgba[:,:,1] = arr[:,:,1]
        rgba[:,:,2] = arr[:,:,0]
        rgba[:,:,3] = 255

    return Image.fromarray(rgba, "RGBA"), w, h


def main():
    os.makedirs(DST, exist_ok=True)
    
    for png_name, ozt_name in SLOT_MAP.items():
        src_path = os.path.join(SRC, ozt_name)
        dst_path = os.path.join(DST, png_name)
        
        if not os.path.exists(src_path):
            # Try case-insensitive
            found = False
            for f in os.listdir(SRC):
                if f.lower() == ozt_name.lower():
                    src_path = os.path.join(SRC, f)
                    found = True
                    break
            if not found:
                print(f"  ✗ {ozt_name} — NOT FOUND in mu.lv")
                continue
        
        try:
            img, w, h = decode_ozt(src_path)
            img.save(dst_path, "PNG")
            print(f"  ✓ {png_name} ({w}x{h}) — from mu.lv {ozt_name}")
        except Exception as e:
            print(f"  ✗ {png_name} — ERROR: {e}")

    print(f"\nDone! Converted to: {DST}")


if __name__ == "__main__":
    main()
