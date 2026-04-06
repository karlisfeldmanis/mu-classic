#!/usr/bin/env python3
"""Dump ALL objects near the Lorencia fountain area to identify the fountain structure."""

import struct

MAP_XOR_KEY = bytes([0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                     0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2])

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

def parse_objects(path):
    with open(path, 'rb') as f:
        raw = f.read()
    data = decrypt_map_file(raw)
    count = struct.unpack_from('<h', data, 2)[0]
    objects = []
    ptr = 4
    for i in range(count):
        obj_type = struct.unpack_from('<h', data, ptr)[0]
        ptr += 2
        mu_pos = struct.unpack_from('<3f', data, ptr)
        ptr += 12
        mu_angle = struct.unpack_from('<3f', data, ptr)
        ptr += 12
        scale = struct.unpack_from('<f', data, ptr)[0]
        ptr += 4
        gl_x, gl_y, gl_z = mu_pos[1], mu_pos[2], mu_pos[0]
        objects.append({
            'type': obj_type, 'gl_pos': (gl_x, gl_y, gl_z),
            'mu_pos': mu_pos, 'angle': mu_angle, 'scale': scale
        })
    return objects

def parse_heightmap(path):
    with open(path, 'rb') as f:
        raw = f.read()
    raw_heights = raw[-(256*256):]
    return [float(b) * 1.5 for b in raw_heights]

def get_terrain_height(heights, gl_x, gl_z):
    gz = max(0.0, min(gl_x / 100.0, 254.0))
    gx = max(0.0, min(gl_z / 100.0, 254.0))
    xi, zi = int(gx), int(gz)
    xd, zd = gx - xi, gz - zi
    h00 = heights[zi * 256 + xi]
    h10 = heights[zi * 256 + (xi + 1)]
    h01 = heights[(zi + 1) * 256 + xi]
    h11 = heights[(zi + 1) * 256 + (xi + 1)]
    return h00*(1-xd)*(1-zd) + h10*xd*(1-zd) + h01*(1-xd)*zd + h11*xd*zd

# Object type names (from _enum.h)
TYPE_NAMES = {
    0: "Tree01", 1: "Tree02", 2: "Tree03", 3: "Tree04", 4: "Tree05",
    5: "Tree06", 6: "Tree07", 7: "Tree08", 8: "Tree09", 9: "Tree10",
    10: "Tree11", 11: "Tree12", 12: "Tree13", 13: "Tree14", 14: "Tree15",
    15: "Tree16", 16: "Tree17", 17: "Tree18", 18: "Tree19", 19: "Tree20",
    20: "Grass01", 21: "Grass02", 22: "Grass03", 23: "Grass04", 24: "Grass05",
    25: "Grass06", 26: "Grass07", 27: "Grass08", 28: "Grass09", 29: "Grass10",
    30: "Stone01", 31: "Stone02", 32: "Stone03", 33: "Stone04", 34: "Stone05",
    35: "Stone06", 36: "Stone07", 37: "Stone08", 38: "Stone09", 39: "Stone10",
    40: "StoneStatue01", 41: "StoneStatue02", 42: "StoneStatue03",
    43: "SteelStatue", 44: "Tomb01", 45: "Tomb02", 46: "Tomb03",
    50: "FireLight01", 51: "FireLight02", 52: "Bonfire", 53: "DungeonGate01",
    54: "DungeonGate02", 55: "MerchantAnimal", 56: "Waterspout",
    57: "Well", 58: "Straw01",
    80: "Bridge01", 81: "Bridge02", 82: "Bridge03", 83: "Bridge04",
    84: "Fence01", 85: "Fence02", 86: "Fence03", 87: "Fence04", 88: "Fence05",
    90: "StreetLight", 91: "StreetSign",
    100: "House01", 101: "House02", 102: "House03", 103: "House04",
    104: "House05", 105: "House06", 106: "House07", 107: "House08",
    130: "Furniture01", 131: "Furniture02", 132: "Furniture03", 133: "Furniture04",
    134: "Furniture05", 135: "Furniture06", 136: "Furniture07",
    140: "Candle01", 141: "Candle02", 142: "Candle03",
    150: "CandleStick01"
}

if __name__ == '__main__':
    base = '/Users/karlisfeldmanis/Desktop/mu_remaster/client/Data/World1'
    objects = parse_objects(f'{base}/EncTerrain1.obj')
    heights = parse_heightmap(f'{base}/TerrainHeight.bmp')

    # The Lorencia fountain is roughly at the town center
    # Let's search several candidate areas for type=40 (StoneStatue01) objects
    # and dump everything near each one

    # First, find the main fountain - it's the pair of type=40 statues at the center
    print("=== ALL TYPE 40 (StoneStatue01) OBJECTS ===")
    for obj in objects:
        if obj['type'] == 40:
            gx, gy, gz = obj['gl_pos']
            terr = get_terrain_height(heights, gx, gz)
            print(f"  grid=({gx/100:.1f}, {gz/100:.1f}) Y={gy:.1f} terrH={terr:.1f} delta={gy-terr:.1f}")

    # The fountain at (125.5, 172.5) seems like the town center
    # Let's dump EVERYTHING within 1000 units of it
    center_x, center_z = 12550, 17250  # grid 125.5, 172.5

    print(f"\n=== ALL OBJECTS within 1000 units of ({center_x/100:.1f}, {center_z/100:.1f}) ===")
    nearby = []
    for obj in objects:
        gx, gy, gz = obj['gl_pos']
        dist = ((gx-center_x)**2 + (gz-center_z)**2)**0.5
        if dist < 1000:
            terr = get_terrain_height(heights, gx, gz)
            nearby.append((dist, obj, terr))

    nearby.sort(key=lambda x: x[0])
    for dist, obj, terr in nearby:
        gx, gy, gz = obj['gl_pos']
        name = TYPE_NAMES.get(obj['type'], f"Unknown_{obj['type']}")
        print(f"  [{obj['type']:3d}] {name:16s} dist={dist:4.0f} grid=({gx/100:.1f}, {gz/100:.1f}) "
              f"Y={gy:.1f} terrH={terr:.1f} delta={gy-terr:+.1f} scale={obj['scale']:.2f}")

    # Also check the town square area more broadly
    print(f"\n=== ALL OBJECTS in grid region 120-135, 168-178 ===")
    region = []
    for obj in objects:
        gx, gy, gz = obj['gl_pos']
        grid_x = gx / 100.0
        grid_z = gz / 100.0
        if 120 <= grid_x <= 135 and 168 <= grid_z <= 178:
            terr = get_terrain_height(heights, gx, gz)
            region.append((grid_x, grid_z, obj, terr))

    region.sort(key=lambda x: (x[0], x[1]))
    for gx, gz, obj, terr in region:
        gl_x, gl_y, gl_z = obj['gl_pos']
        name = TYPE_NAMES.get(obj['type'], f"Unknown_{obj['type']}")
        print(f"  [{obj['type']:3d}] {name:16s} grid=({gx:.1f}, {gz:.1f}) "
              f"Y={gl_y:.1f} terrH={terr:.1f} delta={gl_y-terr:+.1f} "
              f"angle=({obj['angle'][0]:.0f},{obj['angle'][1]:.0f},{obj['angle'][2]:.0f}) "
              f"scale={obj['scale']:.2f}")
