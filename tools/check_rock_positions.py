#!/usr/bin/env python3
"""Check if rock objects are properly positioned on terrain."""

import struct
from pathlib import Path

def decrypt_map(data):
    """Decrypt terrain map file using Main 5.2 MapFileDecrypt algorithm."""
    MAP_XOR_KEY = [0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                   0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2]

    result = bytearray(len(data))
    map_key = 0x5E

    for i in range(len(data)):
        src_byte = data[i]
        xor_byte = MAP_XOR_KEY[i % 16]
        val = ((src_byte ^ xor_byte) - map_key) & 0xFF
        result[i] = val
        map_key = (src_byte + 0x3D) & 0xFF

    return bytes(result)

def load_heightmap(path):
    """Load and scale heightmap from .ozb file."""
    with open(path, 'rb') as f:
        f.seek(4)  # Skip 4-byte header
        f.seek(1080, 1)  # Skip BMP header (relative seek)
        raw = f.read(256 * 256)

    heightmap = []
    for byte_val in raw:
        # Main 5.2: *dst = (float)(*src)*1.5f
        heightmap.append(byte_val * 1.5)

    return heightmap

def parse_objects(obj_path):
    """Parse EncTerrainN.obj file."""
    with open(obj_path, 'rb') as f:
        enc_data = f.read()

    data = decrypt_map(enc_data)
    ptr = 0
    version = data[ptr]; ptr += 1
    map_number = data[ptr]; ptr += 1
    count = struct.unpack('<H', data[ptr:ptr+2])[0]; ptr += 2

    objects = []
    for i in range(count):
        obj_type = struct.unpack('<h', data[ptr:ptr+2])[0]; ptr += 2
        pos = struct.unpack('<fff', data[ptr:ptr+12]); ptr += 12
        angle = struct.unpack('<fff', data[ptr:ptr+12]); ptr += 12
        scale = struct.unpack('<f', data[ptr:ptr+4])[0]; ptr += 4
        objects.append({
            'type': obj_type,
            'mu_pos': pos,  # MU (X, Y, Z)
            'scale': scale
        })

    return objects

def main():
    base_path = Path('/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/World1')

    # Load heightmap
    height_path = base_path / 'TerrainHeight.OZB'
    heightmap = load_heightmap(height_path)

    # Load objects
    obj_path = base_path / 'EncTerrain1.obj'
    objects = parse_objects(obj_path)

    # Filter rock objects (types 30-42)
    rocks = [o for o in objects if 30 <= o['type'] <= 42]
    print(f"Found {len(rocks)} rock objects (types 30-42)\n")

    # Analyze rock positioning
    print("Rock positioning analysis:")
    print("="*80)

    gaps = []
    for i, rock in enumerate(rocks[:20]):  # First 20 rocks
        mu_x, mu_y, mu_z = rock['mu_pos']
        grid_z = int(mu_y / 100.0)
        grid_x = int(mu_x / 100.0)

        if 0 <= grid_x < 256 and 0 <= grid_z < 256:
            terrain_h = heightmap[grid_z * 256 + grid_x]
            gap = mu_z - terrain_h
            gaps.append(gap)

            if i < 10:
                print(f"Rock {i} (type {rock['type']:2d}) at MU({mu_x:4.0f}, {mu_y:4.0f}, {mu_z:6.1f}): "
                      f"terrain={terrain_h:5.1f}, gap={gap:6.1f}")

    if gaps:
        print(f"\nSummary (first 20 rocks):")
        print(f"  Avg gap: {sum(gaps)/len(gaps):6.1f}")
        print(f"  Min gap: {min(gaps):6.1f}")
        print(f"  Max gap: {max(gaps):6.1f}")

if __name__ == '__main__':
    main()
