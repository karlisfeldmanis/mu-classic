#!/usr/bin/env python3
"""Dump all grass object positions from terrain file and compare with heightmap."""

import struct
import sys
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
        # Apply 1.5x scaling like Main 5.2
        return [float(b) * 1.5 for b in raw]

def parse_objects(obj_path):
    """Parse EncTerrainN.obj file."""
    with open(obj_path, 'rb') as f:
        enc_data = f.read()

    data = decrypt_map(enc_data)
    ptr = 0

    version = data[ptr]
    ptr += 1
    map_number = data[ptr]
    ptr += 1
    count = struct.unpack('<H', data[ptr:ptr+2])[0]
    ptr += 2

    objects = []
    for i in range(count):
        obj_type = struct.unpack('<h', data[ptr:ptr+2])[0]
        ptr += 2
        pos = struct.unpack('<fff', data[ptr:ptr+12])
        ptr += 12
        angle = struct.unpack('<fff', data[ptr:ptr+12])
        ptr += 12
        scale = struct.unpack('<f', data[ptr:ptr+4])[0]
        ptr += 4

        objects.append({
            'type': obj_type,
            'mu_pos': pos,  # (X, Y, Z) in MU coords
            'angle': angle,
            'scale': scale
        })

    return objects

def main():
    base_path = Path('/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/World1')

    # Load heightmap (Lorencia uses TerrainHeight.OZB, no number suffix)
    height_path = base_path / 'TerrainHeight.OZB'
    heightmap = load_heightmap(height_path)
    print(f"Loaded heightmap: {len(heightmap)} values")
    print(f"  Min: {min(heightmap):.1f}, Max: {max(heightmap):.1f}, Avg: {sum(heightmap)/len(heightmap):.1f}\n")

    # Load objects
    obj_path = base_path / 'EncTerrain1.obj'
    objects = parse_objects(obj_path)

    # Filter grass objects (types 20-27)
    grass_objects = [o for o in objects if 20 <= o['type'] <= 27]
    print(f"Found {len(grass_objects)} grass objects (types 20-27)\n")

    # Analyze statistics by grass type
    print("\nStatistics by grass type:")
    print("="*80)
    by_type = {}
    for obj in grass_objects:
        t = obj['type']
        if t not in by_type:
            by_type[t] = []

        mu_x, mu_y, mu_z = obj['mu_pos']
        grid_z = int(mu_y / 100.0)
        grid_x = int(mu_x / 100.0)

        if 0 <= grid_x < 256 and 0 <= grid_z < 256:
            terrain_h = heightmap[grid_z * 256 + grid_x]
            gap = mu_z - terrain_h
            by_type[t].append(gap)

    for grass_type in sorted(by_type.keys()):
        gaps = by_type[grass_type]
        avg_gap = sum(gaps) / len(gaps)
        min_gap = min(gaps)
        max_gap = max(gaps)

        print(f"Type {grass_type} (Grass{grass_type-19:02d}.bmd): {len(gaps)} instances")
        print(f"  Gap: avg={avg_gap:6.1f}, min={min_gap:6.1f}, max={max_gap:6.1f}")

    # Show first 5 of each type
    print("\n\nFirst 5 instances of each type:")
    print("="*80)
    type_counts = {t: 0 for t in range(20, 28)}
    for i, obj in enumerate(grass_objects):
        t = obj['type']
        if type_counts[t] >= 5:
            continue
        type_counts[t] += 1

        mu_x, mu_y, mu_z = obj['mu_pos']
        grid_z = int(mu_y / 100.0)
        grid_x = int(mu_x / 100.0)

        if 0 <= grid_x < 256 and 0 <= grid_z < 256:
            terrain_h = heightmap[grid_z * 256 + grid_x]
            gap = mu_z - terrain_h

            print(f"Type {t} #{type_counts[t]}: MU_Z={mu_z:.1f} terrain={terrain_h:.1f} gap={gap:.1f}")

if __name__ == '__main__':
    main()
