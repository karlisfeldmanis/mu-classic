# Coordinate System Test Guide

## Overview

This test evaluates switching from GL coordinates (Y-up) to MU native coordinates (Z-up) to fix floating grass/rock positioning issues.

## Root Cause

- Original Main 5.2 uses MU coordinates directly: X=right, Y=forward, Z=up
- Our code converts MU→GL: `position = (MU_Y, MU_Z, MU_X)` in TerrainParser.cpp:329
- This conversion may cause positioning issues since terrain files store MU coords
- BGFX supports Z-up natively, so conversion might be unnecessary

## Test Implementation

A compile-time flag `USE_MU_COORDINATES` allows testing both coordinate systems:
- **OFF (default)**: GL Y-up system (current behavior)
- **ON**: MU Z-up system (Main 5.2 native)

### Modified Files

1. **CMakeLists.txt** - Added `USE_MU_COORDINATES` option
2. **TerrainParser.cpp** - Object position conversion wrapped
3. **Terrain.cpp** - Terrain vertex generation wrapped
4. **Camera.cpp** - WorldUp and front vector calculation wrapped
5. **TerrainUtils.cpp** - Grid coordinate mapping wrapped
6. **ObjectRenderer.cpp** - Terrain height/light sampling wrapped
7. **MonsterManager.cpp** - Monster spawn position conversion wrapped
8. **NpcManager.cpp** - NPC position conversion wrapped
9. **ClientPacketHandler.cpp** - Ground item position construction wrapped
10. **GrassRenderer.cpp** - Grass billboard position construction wrapped
11. **RayPicker.cpp** - Ray-terrain and ray-entity intersection wrapped
12. **TerrainUtils.hpp** - Added coordinate-agnostic helper macros:
    - `WORLD_POS(x, y, height)` - Construct world position vector
    - `GET_VERT(pos)` - Extract vertical component
    - `GET_HORIZ_X(pos)` - Extract horizontal X
    - `GET_HORIZ_Y(pos)` - Extract horizontal Y (forward/back)

## Build & Test

### Test GL coordinates (current, default):
```bash
cd client/build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
bash ../../launch.sh
```

### Test MU coordinates (Main 5.2 native):
```bash
cd client/build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_MU_COORDINATES=ON ..
ninja
bash ../../launch.sh
```

## What to Check

### 1. Grass Positioning (Primary Test)
- Load Lorencia (World1)
- Check if grass types 20-21 (Grass01/02) are properly grounded
- Currently these types are HIDDEN in GL mode due to floating
- In MU mode, they should render at correct height (no skipping needed)
- Types 22-27 should remain correctly positioned in both modes

### 2. Rock Positioning
- Rocks (types 30-42: Stone01-Stone13) should sit on terrain
- Check around coordinates: (18650, 18300), (19450, 21650), (12850, 12550)
- In GL mode, some rocks float; in MU mode should be correct

### 3. Camera & Movement
- WASD camera movement should work correctly
- Camera angles (yaw/pitch) should feel natural
- Isometric view should be preserved

### 4. Raycasting
- Click-to-move on terrain should work
- NPC clicking (shop access) should work
- Monster clicking (attack targeting) should work

### 5. Character & Entities
- Character select terrain snapping should work
- Player should stand on terrain (not float/sink)
- Monsters should spawn at correct heights
- NPCs should be positioned correctly

## Expected Results

### GL Mode (OFF)
- Current behavior preserved
- Grass 20-21 hidden (skipped during load)
- Some rocks may float

### MU Mode (ON)
- Grass 20-21 should render at CORRECT heights (matches terrain)
- Rocks should sit properly on terrain
- All positioning should match Main 5.2 exactly
- If successful, this becomes the new default (remove GL mode)

## Risks & Known Issues

### Potential Breakage in MU Mode:
- Model rotations might need adjustment (if models appear sideways)
- Particle effects might spawn at wrong heights
- UI elements tied to world coords might misalign
- Pathfinding grid conversion might break
- Network packets assume specific coord order

### If MU Mode Works:
1. Remove grass 20-21 skip logic from ObjectRenderer.cpp
2. Remove FixGrassPositions() function (no longer needed)
3. Remove GL coordinate code paths (keep only MU)
4. Update CLAUDE.md to document MU-native coordinate system

## Rollback

If MU mode breaks too many systems:
```bash
cd client/build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_MU_COORDINATES=OFF ..
ninja
```

This restores GL coordinate behavior immediately.
