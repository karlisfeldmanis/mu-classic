#!/usr/bin/env python3
"""Check if grass objects are placed on rocks."""

import struct
from pathlib import Path

def decrypt_map(data):
    """Decrypt terrain map file."""
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
        objects.append({'type': obj_type, 'mu_pos': pos, 'scale': scale})
    return objects

def main():
    base_path = Path('/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/World1')
    obj_path = base_path / 'EncTerrain1.obj'
    objects = parse_objects(obj_path)

    # Rock types in Lorencia (types 30-39 typically)
    rocks = [o for o in objects if 30 <= o['type'] <= 50]
    grass2021 = [o for o in objects if o['type'] in [20, 21]]

    print(f"Total objects: {len(objects)}")
    print(f"Rock objects (types 30-50): {len(rocks)}")
    print(f"Grass types 20-21: {len(grass2021)}\n")

    # Check first 10 grass 20-21 for nearby rocks
    print("Checking if grass 20-21 is near rocks:")
    print("="*80)

    for i, grass in enumerate(grass2021[:10]):
        gx, gy, gz = grass['mu_pos']

        # Find rocks within 200 units
        nearby_rocks = []
        for rock in rocks:
            rx, ry, rz = rock['mu_pos']
            dist_sq = (gx - rx)**2 + (gy - ry)**2
            if dist_sq < 200**2:  # Within 200 units horizontally
                dist = dist_sq**0.5
                height_diff = gz - rz
                nearby_rocks.append((rock['type'], dist, rz, height_diff))

        print(f"\nGrass {i} (type {grass['type']}) at MU ({gx:.0f}, {gy:.0f}, {gz:.1f}):")
        if nearby_rocks:
            print(f"  {len(nearby_rocks)} nearby rocks:")
            for rock_type, dist, rock_z, h_diff in sorted(nearby_rocks, key=lambda x: x[1])[:3]:
                print(f"    Rock type {rock_type}: dist={dist:.0f}, rockZ={rock_z:.1f}, heightDiff={h_diff:.1f}")
        else:
            print(f"  No rocks within 200 units")

if __name__ == '__main__':
    main()
