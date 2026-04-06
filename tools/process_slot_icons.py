#!/usr/bin/env python3
"""
Post-process AI-generated equipment slot icons:
1. Remove black background → transparent
2. Reduce opacity to ~40% for subtle silhouette effect 
3. Resize to exact game dimensions
4. For missing icons (quota exhausted), generate enhanced PIL versions

Output goes to client/Data/Interface/
"""

from PIL import Image, ImageDraw, ImageFilter, ImageEnhance
import numpy as np
import os, math

BRAIN_DIR = os.path.expanduser("~/.gemini/antigravity/brain/f7ad7bd5-509c-44e9-aa1c-98e24561bb55")
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "client", "Data", "Interface")

# Target dimensions for each slot
SLOT_DIMS = {
    "weapon_r":  (46, 66),
    "weapon_l":  (46, 66),
    "cap":       (46, 46),
    "upper":     (46, 66),
    "lower":     (46, 46),
    "gloves":    (46, 46),
    "boots":     (46, 46),
    "wing":      (61, 46),
    "fairy":     (46, 46),
    "necklace":  (28, 28),
    "ring":      (28, 28),
}

# Map from slot name to generated image file (partial name match)
AI_GENERATED = {
    "weapon_r": "slot_weapon_r",
    "weapon_l": "slot_weapon_l",
    "cap":      "slot_helmet",
    "upper":    "slot_armor",
    "lower":    "slot_pants",
    "gloves":   "slot_gloves",
}

SILHOUETTE_ALPHA = 100  # Target alpha for the silhouette (out of 255)


def find_generated_image(prefix):
    """Find the AI-generated image file by prefix in the brain dir."""
    for f in os.listdir(BRAIN_DIR):
        if f.startswith(prefix) and f.endswith(".png"):
            return os.path.join(BRAIN_DIR, f)
    return None


def remove_black_bg_and_process(img_path, target_w, target_h):
    """Load image, remove black background, make semi-transparent, resize."""
    img = Image.open(img_path).convert("RGBA")
    pixels = np.array(img, dtype=np.float32)
    
    r, g, b, a = pixels[:,:,0], pixels[:,:,1], pixels[:,:,2], pixels[:,:,3]
    
    # Calculate brightness (perceived luminance)
    brightness = 0.299 * r + 0.587 * g + 0.114 * b
    
    # Black background removal: pixels with low brightness become transparent
    # Use a smooth transition for anti-aliasing
    threshold_low = 25.0   # Below this = fully transparent
    threshold_high = 60.0  # Above this = keep as-is
    
    # Create alpha mask based on brightness
    alpha_factor = np.clip((brightness - threshold_low) / (threshold_high - threshold_low), 0.0, 1.0)
    
    # Also factor in existing alpha
    alpha_factor *= (a / 255.0)
    
    # Apply the silhouette alpha level
    new_alpha = (alpha_factor * SILHOUETTE_ALPHA).astype(np.uint8)
    
    # Build output image
    result = np.zeros_like(pixels, dtype=np.uint8)
    result[:,:,0] = np.clip(r, 0, 255).astype(np.uint8)
    result[:,:,1] = np.clip(g, 0, 255).astype(np.uint8)
    result[:,:,2] = np.clip(b, 0, 255).astype(np.uint8)
    result[:,:,3] = new_alpha
    
    out = Image.fromarray(result, "RGBA")
    
    # Resize with high-quality downsampling
    out = out.resize((target_w, target_h), Image.LANCZOS)
    
    return out


# ─── Enhanced PIL fallback generators for missing icons ───

def make_boots_enhanced(w=46, h=46):
    """Enhanced boots silhouette with more detail."""
    # Work at 4x then downscale for anti-aliasing
    s = 4
    img = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    
    col = (170, 175, 185)
    col2 = (140, 145, 155)
    
    # Boot shaft
    d.rounded_rectangle([14*s, 3*s, 32*s, 26*s], radius=2*s, fill=col+(70,), outline=col+(100,), width=s)
    # Ankle area
    pts = [(10*s, 26*s), (34*s, 26*s), (38*s, 34*s), (6*s, 34*s)]
    d.polygon(pts, fill=col+(70,), outline=col+(100,))
    # Foot/sole
    d.rounded_rectangle([4*s, 34*s, 42*s, (h-3)*s], radius=3*s, fill=col+(70,), outline=col+(100,), width=s)
    # Top cuff detail
    d.rounded_rectangle([12*s, 3*s, 34*s, 8*s], radius=s, fill=col2+(90,), outline=col+(110,), width=s)
    # Buckle
    d.rounded_rectangle([18*s, 16*s, 28*s, 20*s], radius=s, fill=col2+(50,), outline=col+(80,), width=s)
    # Sole line
    d.line([(6*s, (h-5)*s), (40*s, (h-5)*s)], fill=col+(60,), width=s)
    
    return img.resize((w, h), Image.LANCZOS)


def make_wing_enhanced(w=61, h=46):
    """Enhanced wings silhouette."""
    s = 4
    img = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    
    col = (170, 175, 185)
    cx, cy = w*s//2, h*s//2
    
    # Left wing feathers (multiple layers)
    for i, (dx, dy, spread) in enumerate([(0, 0, 1.0), (-2*s, 2*s, 0.8), (2*s, -2*s, 0.9)]):
        pts_l = [
            (cx - 2*s + dx, cy + 6*s + dy),
            (cx - 6*s + dx, cy - 4*s + dy),
            (int(4*s*spread) + dx, int(3*s*spread) + dy),
            (2*s + dx, int(cy - 6*s*spread) + dy),
            (int(6*s*spread) + dx, cy + int(6*s*spread) + dy),
            (cx - 4*s + dx, h*s - 6*s + dy),
        ]
        alpha = 70 - i * 15
        d.polygon(pts_l, fill=col+(alpha,), outline=col+(alpha+30,))
    
    # Right wing (mirror)
    for i, (dx, dy, spread) in enumerate([(0, 0, 1.0), (2*s, 2*s, 0.8), (-2*s, -2*s, 0.9)]):
        pts_r = [
            (cx + 2*s + dx, cy + 6*s + dy),
            (cx + 6*s + dx, cy - 4*s + dy),
            (w*s - int(4*s*spread) + dx, int(3*s*spread) + dy),
            (w*s - 2*s + dx, int(cy - 6*s*spread) + dy),
            (w*s - int(6*s*spread) + dx, cy + int(6*s*spread) + dy),
            (cx + 4*s + dx, h*s - 6*s + dy),
        ]
        alpha = 70 - i * 15
        d.polygon(pts_r, fill=col+(alpha,), outline=col+(alpha+30,))
    
    # Center body
    d.ellipse([cx-3*s, cy-3*s, cx+3*s, cy+10*s], fill=col+(90,), outline=col+(110,))
    
    # Feather lines
    for angle in range(-60, -10, 15):
        rad = math.radians(angle)
        d.line([(cx-2*s, cy), (cx - int(20*s*math.cos(rad)), cy - int(20*s*math.sin(rad)))],
               fill=col+(40,), width=s)
    for angle in range(190, 250, 15):
        rad = math.radians(angle)
        d.line([(cx+2*s, cy), (cx - int(20*s*math.cos(rad)), cy - int(20*s*math.sin(rad)))],
               fill=col+(40,), width=s)
    
    return img.resize((w, h), Image.LANCZOS)


def make_fairy_enhanced(w=46, h=46):
    """Enhanced pet/fairy silhouette."""
    s = 4
    img = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    
    col = (170, 175, 185)
    cx, cy = w*s//2, h*s//2
    
    # Body
    d.ellipse([cx-7*s, cy, cx+7*s, cy+14*s], fill=col+(70,), outline=col+(100,), width=s)
    # Head
    d.ellipse([cx-7*s, cy-14*s, cx+7*s, cy+3*s], fill=col+(70,), outline=col+(100,), width=s)
    # Eyes (subtle)
    d.ellipse([cx-4*s, cy-8*s, cx-2*s, cy-5*s], fill=col+(50,))
    d.ellipse([cx+2*s, cy-8*s, cx+4*s, cy-5*s], fill=col+(50,))
    
    # Left wing
    pts = [(cx-7*s, cy-4*s), (4*s, cy-14*s), (3*s, cy-2*s), (6*s, cy+4*s)]
    d.polygon(pts, fill=col+(50,), outline=col+(80,), width=s)
    # Right wing
    pts = [(cx+7*s, cy-4*s), (w*s-4*s, cy-14*s), (w*s-3*s, cy-2*s), (w*s-6*s, cy+4*s)]
    d.polygon(pts, fill=col+(50,), outline=col+(80,), width=s)
    
    # Halo
    d.arc([cx-10*s, cy-22*s, cx+10*s, cy-14*s], 0, 360, fill=col+(90,), width=2*s)
    
    # Small sparkles
    for sx, sy in [(cx-12*s, cy-6*s), (cx+14*s, cy-8*s), (cx, cy+16*s)]:
        d.line([(sx-2*s, sy), (sx+2*s, sy)], fill=col+(60,), width=s)
        d.line([(sx, sy-2*s), (sx, sy+2*s)], fill=col+(60,), width=s)
    
    return img.resize((w, h), Image.LANCZOS)


def make_necklace_enhanced(w=28, h=28):
    """Enhanced necklace/pendant silhouette."""
    s = 4
    img = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    
    col = (170, 175, 185)
    cx = w*s//2
    
    # Chain arc
    d.arc([4*s, 1*s, (w-4)*s, (h-6)*s], 200, 340, fill=col+(100,), width=2*s)
    
    # Chain links (small dashes along the arc)
    for angle in range(210, 340, 12):
        rad = math.radians(angle)
        rx, ry = (w-8)*s/2, (h-7)*s/2
        x = cx + int(rx * math.cos(rad))
        y = (h-6)*s//2 + int(ry * math.sin(rad))
        d.ellipse([x-s, y-s, x+s, y+s], fill=col+(80,))
    
    # Chain sides down to pendant
    d.line([(5*s, (h//2-2)*s), (cx-4*s, (h-9)*s)], fill=col+(90,), width=2*s)
    d.line([((w-5)*s, (h//2-2)*s), (cx+4*s, (h-9)*s)], fill=col+(90,), width=2*s)
    
    # Pendant gem (diamond shape with glow)
    gem_pts = [
        (cx, (h-13)*s),
        (cx-5*s, (h-7)*s),
        (cx, (h-1)*s),
        (cx+5*s, (h-7)*s),
    ]
    d.polygon(gem_pts, fill=col+(80,), outline=col+(110,), width=s)
    # Gem facet
    d.line([(cx, (h-13)*s), (cx, (h-1)*s)], fill=col+(50,), width=s)
    d.line([(cx-5*s, (h-7)*s), (cx+5*s, (h-7)*s)], fill=col+(50,), width=s)
    
    return img.resize((w, h), Image.LANCZOS)


def make_ring_enhanced(w=28, h=28):
    """Enhanced ring silhouette."""
    s = 4
    img = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    
    col = (170, 175, 185)
    cx, cy = w*s//2, h*s//2 + 2*s
    
    # Outer ring band
    d.ellipse([4*s, 7*s, (w-4)*s, (h-3)*s], fill=None, outline=col+(100,), width=3*s)
    # Inner ring
    d.ellipse([7*s, 10*s, (w-7)*s, (h-6)*s], fill=None, outline=col+(60,), width=s)
    
    # Ring band thickness (fill between inner and outer)
    # Use a mask approach
    band = Image.new("RGBA", (w*s, h*s), (0, 0, 0, 0))
    bd = ImageDraw.Draw(band)
    bd.ellipse([4*s, 7*s, (w-4)*s, (h-3)*s], fill=col+(50,))
    bd.ellipse([8*s, 11*s, (w-8)*s, (h-7)*s], fill=(0, 0, 0, 0))
    img = Image.alpha_composite(img, band)
    d = ImageDraw.Draw(img)
    
    # Gem setting on top
    d.ellipse([cx-5*s, 1*s, cx+5*s, 11*s], fill=col+(80,), outline=col+(110,), width=s)
    # Gem facet highlight
    d.ellipse([cx-3*s, 3*s, cx+1*s, 7*s], fill=col+(40,))
    
    # Prongs
    d.line([(cx-4*s, 10*s), (cx-5*s, 6*s)], fill=col+(90,), width=s)
    d.line([(cx+4*s, 10*s), (cx+5*s, 6*s)], fill=col+(90,), width=s)
    
    return img.resize((w, h), Image.LANCZOS)


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    # Output filenames matching what the C++ code expects
    output_names = {
        "weapon_r":  "newui_item_weapon_r.png",
        "weapon_l":  "newui_item_weapon_l.png",
        "cap":       "newui_item_cap.png",
        "upper":     "newui_item_upper.png",
        "lower":     "newui_item_lower.png",
        "gloves":    "newui_item_gloves.png",
        "boots":     "newui_item_boots.png",
        "wing":      "newui_item_wing.png",
        "fairy":     "newui_item_fairy.png",
        "necklace":  "newui_item_necklace.png",
        "ring":      "newui_item_ring.png",
    }
    
    # Fallback generators for non-AI-generated icons
    fallback_generators = {
        "boots":    make_boots_enhanced,
        "wing":     make_wing_enhanced,
        "fairy":    make_fairy_enhanced,
        "necklace": make_necklace_enhanced,
        "ring":     make_ring_enhanced,
    }
    
    for slot, (tw, th) in SLOT_DIMS.items():
        outpath = os.path.join(OUTPUT_DIR, output_names[slot])
        
        if slot in AI_GENERATED:
            src = find_generated_image(AI_GENERATED[slot])
            if src:
                img = remove_black_bg_and_process(src, tw, th)
                img.save(outpath, "PNG")
                print(f"  ✓ {output_names[slot]} ({tw}x{th}) — AI-generated, bg removed")
                continue
            else:
                print(f"  ⚠ {slot}: AI image not found, using enhanced PIL fallback")
        
        # Use enhanced PIL fallback
        if slot in fallback_generators:
            img = fallback_generators[slot](tw, th)
            img.save(outpath, "PNG")
            print(f"  ✓ {output_names[slot]} ({tw}x{th}) — enhanced PIL")
        else:
            print(f"  ✗ {slot}: no source available!")
    
    print(f"\nDone! Icons saved to:\n  {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
