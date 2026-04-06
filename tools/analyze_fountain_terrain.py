#!/usr/bin/env python3
"""Check terrain mapping (layer1, alpha, attributes) under the Lorencia fountain."""

import struct

MAP_XOR_KEY = bytes([0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                     0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2])
SIZE = 256

def decrypt_map_file(data):
    result = bytearray(len(data))
    map_key = 0x5E
    for i in range(len(data)):
        src_byte = data[i]
        xor_byte = MAP_XOR_KEY[i % 16]
        val = ((src_byte ^ xor_byte) - map_key) & 0xFF
        result[i] = val
        map_key = (src_byte + 0x3D) & 0xFF
    return bytes(result)

def parse_mapping(path):
    with open(path, 'rb') as f:
        raw = f.read()
    data = decrypt_map_file(raw)
    cells = SIZE * SIZE
    # Skip version(1) + map(1) = 2 bytes
    ptr = 2
    layer1 = list(data[ptr:ptr+cells]); ptr += cells
    layer2 = list(data[ptr:ptr+cells]); ptr += cells
    alpha = [b / 255.0 for b in data[ptr:ptr+cells]]
    return layer1, layer2, alpha

def parse_attributes(path):
    with open(path, 'rb') as f:
        raw = f.read()
    data = decrypt_map_file(raw)
    # BuxConvert
    bux = bytearray(data)
    bux_code = [0xFC, 0xCF, 0xAB]
    for i in range(len(bux)):
        bux[i] ^= bux_code[i % 3]
    data = bytes(bux)
    cells = SIZE * SIZE
    attrs = [0] * cells
    if len(data) >= 4 + cells * 2:
        for i in range(cells):
            attrs[i] = data[4 + i * 2]
    elif len(data) >= 4 + cells:
        for i in range(cells):
            attrs[i] = data[4 + i]
    return attrs

def parse_heightmap(path):
    with open(path, 'rb') as f:
        raw = f.read()
    raw_heights = raw[-(SIZE*SIZE):]
    return [float(b) * 1.5 for b in raw_heights]

if __name__ == '__main__':
    base = '/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/World1'
    layer1, layer2, alpha = parse_mapping(f'{base}/EncTerrain1.map')
    attrs = parse_attributes(f'{base}/EncTerrain1.att')
    heights = parse_heightmap(f'{base}/TerrainHeight.bmp')

    # Fountain is at grid (125.5, 172.5) — check surrounding cells
    # GrassRenderer uses: grid z = worldX/100, x = worldZ/100
    # So fountain at GL worldX=12550, worldZ=17250 → gz=125, gx=172
    # Wait — need to be careful about coord mapping
    # In the script above, objects: gl_x = mu_pos[1], gl_z = mu_pos[0]
    # GrassRenderer: posSW((float)z * 100.0f, h_sw, (float)x * 100.0f)
    # So GL worldX = z*100, GL worldZ = x*100
    # Fountain at GL (12550, _, 17250) → z=125.5, x=172.5
    # Terrain array index = z * 256 + x

    print("=== FOUNTAIN AREA TERRAIN ===")
    print("Fountain center: grid z=125, x=172 (GL worldX=12550, worldZ=17250)")
    print(f"{'z':>4s} {'x':>4s} | {'L1':>3s} {'L2':>3s} {'alpha':>6s} {'attr':>4s} {'height':>7s} | {'grass?':>6s}")
    print("-" * 60)

    grass_count = 0
    for z in range(121, 134):
        for x in range(168, 179):
            idx = z * SIZE + x
            l1 = layer1[idx]
            l2 = layer2[idx]
            a = alpha[idx]
            at = attrs[idx]
            h = heights[idx]

            # GrassRenderer logic:
            is_grass_tile = (l1 == 0 or l1 == 1)
            # Check alpha at all 4 corners
            has_alpha = False
            if z < SIZE-1 and x < SIZE-1:
                idx00 = z * SIZE + x
                idx10 = z * SIZE + (x + 1)
                idx01 = (z + 1) * SIZE + x
                idx11 = (z + 1) * SIZE + (x + 1)
                if alpha[idx00] > 0 or alpha[idx10] > 0 or alpha[idx01] > 0 or alpha[idx11] > 0:
                    has_alpha = True
            is_noground = (at & 0x08) != 0
            would_render = is_grass_tile and not has_alpha and not is_noground

            if would_render:
                grass_count += 1

            marker = "YES" if would_render else "no"
            attr_str = f"0x{at:02X}"
            print(f"{z:4d} {x:4d} | {l1:3d} {l2:3d} {a:6.2f} {attr_str:>4s} {h:7.1f} | {marker:>6s}")

    print(f"\nTotal billboard grass cells in fountain area: {grass_count}")

    # Also check: what tiles are under the fountain stone blocks?
    # The fountain has StoneStatue01 at (125.5, 172.5) — z=125, x=172
    print(f"\n=== FOUNTAIN CENTER CELLS (z=124-127, x=171-174) ===")
    for z in range(124, 128):
        for x in range(171, 175):
            idx = z * SIZE + x
            l1 = layer1[idx]
            a = alpha[idx]
            at = attrs[idx]
            is_grass = (l1 == 0 or l1 == 1)
            print(f"  z={z} x={x}: layer1={l1} alpha={a:.2f} attr=0x{at:02X} → {'GRASS' if is_grass else 'no grass'}")
