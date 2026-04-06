#!/usr/bin/env python3
"""Analyze grass BMD files to understand their structure and positioning logic."""

import struct
import sys
from io import BytesIO

def decrypt_bmd(data):
    """XOR decrypt BMD data if needed."""
    # Check if already decrypted (starts with 'BMD\n')
    if data[:4] == b'BMD\n':
        return data
    # Try decryption
    key = bytearray([0xFC, 0xCF, 0xAB])
    result = bytearray(len(data))
    for i in range(len(data)):
        result[i] = data[i] ^ key[i % 3]
    return bytes(result)

def read_vec3(f):
    """Read a 3D vector (3 floats)."""
    return struct.unpack('<fff', f.read(12))

def read_string(f):
    """Read length-prefixed string."""
    length = struct.unpack('<I', f.read(4))[0]
    if length > 0 and length < 256:
        return f.read(length).decode('ascii', errors='ignore').rstrip('\0')
    return ""

def analyze_bmd(filepath):
    """Analyze BMD file structure."""
    with open(filepath, 'rb') as file:
        encrypted = file.read()
        decrypted = decrypt_bmd(encrypted)
        f = BytesIO(decrypted)

        # Read magic and internal path
        magic = f.read(4).decode('ascii', errors='ignore')
        internal_path = read_string(f)

        print(f"Magic: {repr(magic)}")
        print(f"Internal path: {internal_path}")

        # Skip padding
        padding = struct.unpack('<I', f.read(4))[0]

        mesh_count = struct.unpack('<H', f.read(2))[0]
        bone_count = struct.unpack('<H', f.read(2))[0]
        action_count = struct.unpack('<H', f.read(2))[0]

        print(f"Meshes: {mesh_count}, Bones: {bone_count}, Actions: {action_count}")

        # Read AABB
        min_x, min_y, min_z = read_vec3(f)
        max_x, max_y, max_z = read_vec3(f)

        print(f"\nAABB:")
        print(f"  Min: ({min_x:.4f}, {min_y:.4f}, {min_z:.4f})")
        print(f"  Max: ({max_x:.4f}, {max_y:.4f}, {max_z:.4f})")
        print(f"  Size: ({max_x-min_x:.4f}, {max_y-min_y:.4f}, {max_z-min_z:.4f})")
        print(f"  Center: ({(min_x+max_x)/2:.4f}, {(min_y+max_y)/2:.4f}, {(min_z+max_z)/2:.4f})")
        print(f"  Height: {max_y - min_y:.4f} units")

        # Bone data
        print(f"\nBones ({bone_count}):")
        bones = []
        for i in range(bone_count):
            name_len = struct.unpack('<I', f.read(4))[0]
            name = f.read(name_len).decode('ascii', errors='ignore').rstrip('\0')
            parent = struct.unpack('<h', f.read(2))[0]
            bones.append((name, parent))
            print(f"  [{i}] {name:20s} parent={parent}")

        # Mesh data
        print(f"\nMeshes ({mesh_count}):")
        for m in range(mesh_count):
            vert_count = struct.unpack('<H', f.read(2))[0]
            norm_count = struct.unpack('<H', f.read(2))[0]
            tex_count = struct.unpack('<H', f.read(2))[0]
            tri_count = struct.unpack('<H', f.read(2))[0]

            tex_len = struct.unpack('<I', f.read(4))[0]
            texture = f.read(tex_len).decode('ascii', errors='ignore').rstrip('\0')

            print(f"\n  Mesh {m}: {vert_count}v {norm_count}n {tex_count}t {tri_count}tri")
            print(f"    Texture: {texture}")

            # Read vertices and find actual min/max Y and bone assignments
            actual_min_y = float('inf')
            actual_max_y = float('-inf')
            bone_usage = {}

            for v in range(vert_count):
                x, y, z = read_vec3(f)
                actual_min_y = min(actual_min_y, y)
                actual_max_y = max(actual_max_y, y)
                bone_idx = struct.unpack('<B', f.read(1))[0]
                bone_usage[bone_idx] = bone_usage.get(bone_idx, 0) + 1

            print(f"    Actual vertex Y range: [{actual_min_y:.4f}, {actual_max_y:.4f}]")
            print(f"    Bone usage: {dict(sorted(bone_usage.items()))}")

            # Skip normals
            f.read(norm_count * 12)
            f.read(tex_count * 8)
            f.read(tri_count * 6)

        print(f"\nConclusion:")
        print(f"  If origin is at Y=0, grass extends from Y={min_y:.2f} (bottom) to Y={max_y:.2f} (top)")
        print(f"  To place grass on ground at Y=0, offset by {-min_y:.2f} units")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_grass.py <path_to_grass.bmd>")
        sys.exit(1)

    print(f"=== Analyzing: {sys.argv[1]} ===\n")
    analyze_bmd(sys.argv[1])
    print()
