#!/usr/bin/env python3
"""
Lorencia Tile Texture Viewer
Shows all terrain tiles side-by-side with labels and index numbers.
Compares Main 5.2 vs mu.lv sources.

Usage: python3 tools/tile_viewer.py
"""
from PIL import Image, ImageDraw, ImageFont
import io, os, sys

TILE_DISPLAY_SIZE = 128  # Display each tile at this size
PADDING = 8
LABEL_HEIGHT = 40

SOURCES = {
    'Main 5.2': 'references/other/Main5.2/Source Main 5.2/bin/Data/World1',
    'mu.lv': 'references/mu.lv/Data/World1',
    'Current': 'client/Data/World1',
}

TILE_NAMES = [
    (0, 'TileGrass01', 'Grass field'),
    (1, 'TileGrass02', 'Grass patches'),
    (2, 'TileGround01', 'Dirt/earth'),
    (3, 'TileGround02', 'Dark dirt'),
    (4, 'TileGround03', 'Sandy ground'),
    (5, 'TileWater01', 'Water'),
    (6, 'TileWood01', 'Wood (tavern)'),
    (7, 'TileRock01', 'Stone pave'),
    (8, 'TileRock02', 'Cobblestone'),
    (9, 'TileRock03', 'Stone floor'),
    (10, 'TileRock04', 'Stone tiles'),
    (11, 'TileRock05', 'Dark stone'),
    (12, 'TileRock06', 'Light stone'),
    (13, 'TileRock07', 'Stone wall'),
    (14, 'TileGround01x', 'Path/road'),
    (15, 'TileMgrass', 'Moss grass'),
    (16, 'TileMstone', 'Moss stone'),
]

def load_ozj(path):
    """Load OZJ texture, return PIL Image or None."""
    try:
        data = open(path, 'rb').read()
        for off in [0, 24]:
            try:
                img = Image.open(io.BytesIO(data[off:]))
                img.load()
                return img.convert('RGB')
            except:
                pass
    except:
        pass
    return None

def main():
    # Find project root
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    num_tiles = len(TILE_NAMES)
    num_sources = len(SOURCES)

    cols = num_sources
    rows = num_tiles

    img_w = PADDING + cols * (TILE_DISPLAY_SIZE + PADDING) + 180  # extra for labels
    img_h = PADDING + LABEL_HEIGHT + rows * (TILE_DISPLAY_SIZE + PADDING + 14)

    canvas = Image.new('RGB', (img_w, img_h), (30, 30, 30))
    draw = ImageDraw.Draw(canvas)

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 12)
        font_small = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 10)
    except:
        font = ImageFont.load_default()
        font_small = font

    # Header row
    x = PADDING + 180
    for src_name in SOURCES:
        draw.text((x + 10, PADDING + 10), src_name, fill=(255, 255, 100), font=font)
        x += TILE_DISPLAY_SIZE + PADDING

    # Tile rows
    y = PADDING + LABEL_HEIGHT
    for idx, tile_name, desc in TILE_NAMES:
        # Label
        draw.text((PADDING, y + 20), f"[{idx:2d}] {tile_name}", fill=(200, 200, 200), font=font)
        draw.text((PADDING, y + 36), f"     {desc}", fill=(140, 140, 140), font=font_small)

        x = PADDING + 180
        for src_name, src_dir in SOURCES.items():
            path = os.path.join(root, src_dir, f"{tile_name}.OZJ")
            tile_img = load_ozj(path)

            if tile_img:
                resized = tile_img.resize((TILE_DISPLAY_SIZE, TILE_DISPLAY_SIZE), Image.NEAREST)
                canvas.paste(resized, (x, y))
                # Show original size
                draw.text((x + 2, y + TILE_DISPLAY_SIZE - 12),
                         f"{tile_img.size[0]}x{tile_img.size[1]}",
                         fill=(255, 255, 0), font=font_small)
            else:
                draw.rectangle([x, y, x + TILE_DISPLAY_SIZE, y + TILE_DISPLAY_SIZE],
                              fill=(60, 20, 20))
                draw.text((x + 20, y + 50), "N/A", fill=(200, 50, 50), font=font)

            x += TILE_DISPLAY_SIZE + PADDING

        y += TILE_DISPLAY_SIZE + PADDING + 14

    output = os.path.join(root, 'tile_comparison.png')
    canvas.save(output)
    print(f"Saved: {output}")
    print(f"Size: {img_w}x{img_h}")

    # Also open it
    os.system(f'open "{output}"')

if __name__ == '__main__':
    main()
