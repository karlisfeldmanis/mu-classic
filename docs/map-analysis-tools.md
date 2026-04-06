# Map Analysis & Reverse Engineering Tools

Comprehensive toolkit for deep-diving into MU Online map file formats, extracting complete map intelligence including terrain, objects, monsters, VFX, sounds, lighting, and atmosphere.

## Available Tools

### 1. `map_inspect` - Low-Level Format Analysis

Deep reverse engineering of binary terrain file formats. Focuses on raw data structures, encryption, and format validation.

**Build:**
```bash
cd tools
g++ -std=c++17 -O2 -I../client/external/glm map_inspect.cpp -o map_inspect
```

**Supported Files:**
- `*.map` - Terrain mapping (layer1, layer2, alpha blend) — 196,610 bytes encrypted
- `*.att` - Terrain attributes (walkability, safe zones, voids) — 65,540 or 131,076 bytes
- `*.obj` - World objects (type, position, rotation, scale) — variable size
- `*.OZB` / `*.ozh` - Height data (256×256 uint8 array, scaled by 1.5)
- `*.OZJ` - Lightmap (pre-baked RGB, JPEG compressed)

**Usage Examples:**
```bash
# Basic stats + histogram for terrain mapping
./map_inspect ../client/Data/World1/EncTerrain1.map --stats --histogram

# Attribute zone visualization (safe/walk/void boundaries)
./map_inspect ../client/Data/World1/EncTerrain1.att --zones

# World object analysis with type grouping + density heatmap
./map_inspect ../client/Data/World1/EncTerrain1.obj --types --density

# Height data with ASCII visualization
./map_inspect ../client/Data/World1/TerrainHeight.OZB --stats --heatmap

# Full world directory analysis
./map_inspect ../client/Data/World1/ --all

# Compare two maps (diff analysis)
./map_inspect ../client/Data/World1/EncTerrain1.map --compare ../client/Data/World2/EncTerrain2.map

# Export data as PPM images for external tools
./map_inspect ../client/Data/World1/EncTerrain1.map --export-ppm

# Hex dump of decrypted data (first 512 bytes)
./map_inspect ../client/Data/World1/EncTerrain1.att --raw
```

**Analysis Modes:**
- `--raw` - Hex dump of decrypted data structure
- `--stats` - Statistical analysis (min/max/avg/distribution)
- `--histogram` - Tile/attribute usage histogram with top 20
- `--heatmap` - ASCII art visualization (64×64 downsample)
- `--zones` - Attribute zone map (safe/walk/void/blocked)
- `--objects` - Full object coordinate dump
- `--types` - Group objects by type with instance counts
- `--density` - Object placement density heatmap (32×32 regions)
- `--validate` - Data validation (tile 255, isolated safe zones, void mismatches)
- `--export-ppm` - Export as PPM image for external processing
- `--compare <file>` - Diff two map files
- `--all` - Enable all analysis modes

### 2. `map_analyze` - High-Level Intelligence Tool

Comprehensive map intelligence combining terrain files, database, and code cross-references. Provides complete map profiles with monsters, objects, VFX, sounds, and atmosphere.

**Build:**
```bash
cd tools
g++ -std=c++17 -O2 -I../client/external/glm -I../server/external/sqlite map_analyze.cpp -lsqlite3 -o map_analyze
```

**Usage Examples:**
```bash
# Full analysis of Lorencia (map 0)
./map_analyze --map 0 --all

# Dungeon monster spawns + objects
./map_analyze --map 1 --monsters --objects

# Lost Tower VFX reference (objects with effects)
./map_analyze --map 4 --vfx

# Devias terrain stats only
./map_analyze --map 2 --terrain
```

**Interpreting Output:**

When you see objects marked with **(REMOVED)** in the output:
- These objects exist in terrain `.obj` files as binary legacy data
- All associated code (VFX, AI, collision, sounds) has been deleted
- Database spawn entries have been removed
- They load as static inert geometry with no functionality
- Harmless decorative elements from Main 5.2
- Do NOT re-implement - removal was intentional

Example:
```
Type  39:  335 × LanceTrap (REMOVED)
Type  25:  148 × MeteoriteTrap (REMOVED)
Type  40:    1 × IronStickTrap (REMOVED)
```
This indicates 484 trap objects in Lost Tower that are non-functional.

**Information Extracted:**

1. **Atmosphere Profile:**
   - Map description and architecture style
   - Ambient sound (SOUND_WIND01, SOUND_DUNGEON01)
   - Safe zone music (MuTheme.mp3, Dungeon.mp3, etc.)
   - Wild/combat music (if different from safe)
   - Environment features (Sky, Grass, Wind3D, Doors, Leaves)
   - Luminosity value (0.9 for dark dungeons, 1.0 for outdoor)

2. **Terrain Data:**
   - Terrain file names (EncTerrainN.map/att/obj)
   - World directory (WorldN/)
   - Safe zone coverage (% of total cells)
   - Walls/blocked areas (TW_NOMOVE flag)
   - Void/NoGround cells (TW_NOGROUND for cliffs/rifts)
   - Walkable area percentage

3. **World Objects:**
   - Total object count
   - Type distribution (sorted by frequency)
   - Object names and model files
   - VFX descriptions:
     - Fire emitters (particle systems, ambient flames)
     - Chrome/metallic scrolling UV effects
     - Window light flicker formulas
     - HiddenMesh markers (invisible geometry with VFX only)
     - BlendMesh values (additive glow rendering)
     - StreamMesh (UV scrolling waterfalls, banners)
   - Sound effects (SOUND_FIRE_LOOP, SOUND_WATERFALL_LOOP, SOUND_BONE2)
   - Point lights (RGB color + range in world units)
   - Special behaviors (CheckSkull tracking, cliff fade, bridge protection)

4. **Monster Spawns** (from database):
   - Total spawn count
   - Monster distribution by type
   - Stats per monster (Level, HP, ATK, DEF, AttackRate)
   - Spawn density heatmap (16×16 regions, ASCII visualization)
   - Aggressive vs passive flags
   - Respawn timers

**Options:**
- `--map <id>` - Map ID (0=Lorencia, 1=Dungeon, 2=Devias, 3=Noria, 4=LostTower)
- `--monsters` - Show monster spawn data from database
- `--objects` - Show world objects with instance counts
- `--vfx` - Show VFX/sound/lighting details for each object type
- `--terrain` - Show terrain attribute statistics
- `--all` - Enable all analysis modes (default if no flags given)

## File Format Specifications

### EncTerrainN.map (Terrain Mapping)

**Size:** 196,610 bytes (encrypted)

**Structure:**
```
Offset  Size    Field
------  ------  ---------
0       1       Version (0)
1       1       Map Number (1-based world ID)
2       65536   Layer1 (256×256 tile indices, uint8)
65538   65536   Layer2 (256×256 tile indices, uint8)
131074  65536   Alpha (256×256 blend values, 0=pure L1, 255=pure L2)
```

**Encryption:** XOR cipher + rolling key
- XOR table: `{0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2}`
- Initial key: `0x5E`
- Formula: `decrypted[i] = (encrypted[i] ^ XOR[i%16]) - key`
- Key update: `key = encrypted[i] + 0x3D`

**Tile Indexing:**
- Tile textures stored in `Data/WorldN/TileXXX.OZJ`
- Index 255 = invalid/empty marker (fill with neutral brown 80,70,55)
- Water tile (Lorencia) = index 5 (animated UV scrolling)

**Alpha Blending:**
- Fragment shader blends Layer1 and Layer2 based on alpha value
- Bilinear sampling of neighboring alphas for smooth transitions
- Used for grass→dirt transitions, shore blending, decorative patches

### EncTerrainN.att (Terrain Attributes)

**Size:** 65,540 bytes (BYTE format) or 131,076 bytes (WORD format) — encrypted

**BYTE Format:**
```
Offset  Size    Field
------  ------  ---------
0       1       Version
1       1       Map Number
2       1       Width (255)
3       1       Height (255)
4       65536   Attributes (256×256 uint8 flags)
```

**WORD Format:**
```
4       131072  Attribute pairs (256×256 × 2 bytes)
                Low byte:  TW_* flags
                High byte: Symmetry/extra data (mostly 0x00)
```

**Encryption:** XOR cipher (same as .map) + BuxConvert
- BuxConvert XOR: `{0xFC, 0xCF, 0xAB}` applied after map decryption

**Attribute Flags** (from `_define.h`):
```cpp
#define TW_SAFEZONE  0x01  // Safe zone (no PvP, no mounts)
#define TW_NOMOVE    0x04  // Movement blocked (walls, obstacles)
#define TW_NOGROUND  0x08  // No ground (void, cliffs, bridges)
```

**Walkability Check:**
```cpp
bool IsWalkable(uint8_t attr) {
  return (attr & (TW_NOMOVE | TW_NOGROUND)) == 0;
}
```

**Note:** `TW_SAFEZONE` does NOT block movement — only `TW_NOMOVE` and `TW_NOGROUND` do. Checking only `TW_NOMOVE` allows walking into void cells.

### EncTerrainN.obj (World Objects)

**Structure:**
```
Offset  Size    Field
------  ------  ---------
0       1       Version (varies, usually 0-2)
1       1       Map Number
2       2       Object Count (int16, little-endian)
4       N×30    Object Records (30 bytes each)
```

**Object Record (30 bytes):**
```
Offset  Size    Field
------  ------  ---------
0       2       Type (int16) — maps to ObjectN/ObjectXX.bmd
2       12      Position (float[3]: X, Y, Z in MU coordinates)
14      12      Angle (float[3]: X, Y, Z in degrees)
26      4       Scale (float, typically 1.0)
```

**Coordinate Mapping (MU → OpenGL):**
```cpp
// MU: X=right, Y=forward, Z=up (height)
// GL: X=right, Y=up,      Z=backward
WorldX = mu_pos[1]  // MU Y → WorldX
WorldY = mu_pos[2]  // MU Z → WorldY (height)
WorldZ = mu_pos[0]  // MU X → WorldZ
```

**Encryption:** Same XOR cipher as .map files (no BuxConvert)

### TerrainHeight.OZB / TerrainN.ozh (Height Data)

**Format:** Raw uint8 array (256×256 = 65,536 bytes)
- Usually wrapped in OZB container with header/padding
- Parser reads last 65,536 bytes of file
- Scaling: `worldHeight = rawByte * 1.5`

**Height Range:**
- Typical: 50-150 raw (75-225 world units)
- Extreme: 0-255 raw (0-382.5 world units)

### TerrainLight.OZJ (Lightmap)

**Format:** JPEG compressed RGB image (256×256)
- Decoded via libjpeg-turbo with `TJFLAG_BOTTOMUP`
- Each pixel = pre-baked RGB lighting (0-255 per channel)
- Applied per-pixel in terrain shader via `s_lightMap` sampler

**Directional Sun Lighting:**
- CPU-side pass after loading: `luminosity = dot(terrainNormal, sunDir) + 0.5`
- Sun direction: `(0.5, -0.5, 0.5)` for outdoor maps (MU coordinates)
- Adds relief shading to hills and slopes

**Dynamic Point Lights:**
- Fire emitters (types 50-52, 55, 130, 150) add CPU-side lighting to grid cells
- Linear falloff, cell range 3, colors scaled ×0.35
- Re-uploaded to GPU each frame via `glTexSubImage2D`

## Object Type Reference

### Common Object Types (All Maps)

| Type | Name | Model | VFX/Notes |
|------|------|-------|-----------|
| 3-4 | Tree03/04 (Chrome) | Object1/ | Chrome/metallic scrolling UV (BlendMeshTexCoordU) |
| 11 | CliffWall | Object2/Object11.bmd | Cliff face fade (bottom darkening) |
| 12-13 | Bridge Planks/Supports | Object2/ | Walkable bridge geometry |
| 50-51 | FireLight01/02 | Object1/ | Fire emitter, SOUND_FIRE_LOOP, point light 255,150,50,400 |
| 52 | Bonfire01 | Object1/ | Fire particles, wider flicker (0.4-0.9), light 255,200,100,500 |
| 55 | DungeonGate01 | Object1/ | Fire braziers, gate entrance, light 255,150,50,400 |
| 80 | Bridge01 | Object1/ | Walkable bridge over Lorencia water |
| 85 | BridgeStone01 | Object1/ | Stone bridge over Dungeon/Devias rifts |
| 90 | StreetLight01 | Object1/ | Constant glow (1.0), BlendMesh=1, light 255,255,200,300 |
| 98 | Carriage01 | Object1/ | Window light BlendMesh=2 |
| 105 | Waterspout01 | Object1/ | Water smoke emitters at Y+180 and Y+120, BlendMesh=3 |
| 117 | House03 | Object1/ | Window flicker (0.4-0.7), BlendMesh=4, light 255,200,150,250 |
| 118-119 | House04/05 | Object1/ | Window flicker + UV scroll (StreamMesh) |
| 122 | HouseWall02 | Object1/ | Window flicker (0.4-0.7), BlendMesh=4 |
| 125-126 | Fountain + Bottom | Object1/ | RENDER_WAVE water animation, SOUND_WATERFALL_LOOP |
| 127-129 | HouseEtc01-03 | Object1/ | Flag poles, static (2 meshes: pole + cloth) |
| 130 | Light01 | Object1/ | Fire emitter, point light 255,150,50,350 |
| 150 | Candle01 | Object1/ | Constant glow (1.0), BlendMesh=1, light 255,200,100,200 |

### Lost Tower Specific Objects

| Type | Name | Model | VFX/Notes |
|------|------|-------|-----------|
| 19 | MagicOrb | Object5/Object20.bmd | BITMAP_MAGIC+1 rotating sprites at bones 15,19,21 |
| 20 | LightningOrb | Object5/Object21.bmd | BITMAP_LIGHTNING+1 rotating sprites at bones 15,19,21 |
| 24 | FlamePillar | Object5/Object25.bmd | HiddenMesh=-2, BITMAP_FLAME particles (1/64 prob) |
| 38-39 | Skull01/02 | Object5/Object39-40.bmd | CheckSkull() — tracks player, rotates toward player, plays SOUND_BONE2 |
| 40 | LightningColumn | Object5/Object41.bmd | Rotating BITMAP_LIGHTNING+1 sprites at Z+260 |

### Dungeon Specific Objects

| Type | Name | Model | VFX/Notes |
|------|------|-------|-----------|
| 41-42 | Torch01/02 | Object2/Object42-43.bmd | Fire glow mesh (BlendMesh=1), phase-offset flicker, light 255,180,100,350 |

### Removed Objects (Trap Migration Failed)

| Type | Name | Status |
|------|------|--------|
| 39 | LanceTrap | REMOVED — HiddenMesh=-2, VFX not migrated from Main 5.2 |
| 40 | IronStickTrap | REMOVED — HiddenMesh=-2, VFX not migrated from Main 5.2 |
| 51 | FireTrap | REMOVED — Direction-based AoE, VFX not migrated |
| 25 | MeteoriteTrap | REMOVED — HiddenMesh=-2, falling rocks VFX not migrated |

## Map Profiles

### Map 0: Lorencia

**Atmosphere:**
- Warm grassland with gentle hills and fountain center
- Ambient: SOUND_WIND01 (3D wind loop in wild areas)
- Music: MuTheme.mp3 (safe zone)
- Features: Sky, Grass, Wind3D
- Luminosity: 1.0 (full brightness)

**Terrain:**
- Size: 256×256 cells (25,600×25,600 world units)
- Safe zone: ~2,560 cells (3.91%) — Elveland town center
- Walkable: ~47,574 cells (72.59%)
- Blocked: ~17,962 cells (27.41%) — mountains, buildings, water edges

**Objects:** 2,870 total
- Top types: FlamePillar (317), Trees (various), Statues (213), FireLights (44)
- 16 Bridge01 instances (spanning water channels)
- 1 Fountain (center), 8 Fountain_Bottom pieces
- 9 Bonfire01, 26 FireLight01, 18 FireLight02 (ambient fire)
- 10 StreetLight01, 6 Candle01 (town lighting)
- 15 Chrome trees (types 3-4, scrolling UV effect)

**Monsters:** (from database, typical spawn)
- Bull Fighter, Hound, Budge Dragon (low level 2-9)
- Spider, Lich, Giant (mid level 14-17)
- Skeleton Warrior/Archer/Elite (high level 19-42)

### Map 1: Dungeon (3 Floors)

**Atmosphere:**
- Dark underground torch-lit corridors
- Ambient: SOUND_DUNGEON01 (echoing dungeon ambience)
- Music: Dungeon.mp3
- Features: None (no sky, grass, wind)
- Luminosity: 0.9 (darker than outdoor)

**Floors:**
- Floor 1: Lorencia entry gate → Dungeon 2 forward gate
- Floor 2: Two-way passage + Dungeon 3 forward gate
- Floor 3: Two passage zones (deeper areas)

**Terrain:**
- Safe zone: minimal (entry area only)
- Void cells: ~10-15% (bridge rifts, cliff edges)
- Bridge types: 80 (Bridge), 85 (BridgeStone), 12-13 (Planks/Supports)

**Objects:**
- Torch01/02 (types 41-42) — fire glow, phase-offset flicker
- CliffWall (type 11) — bottom fade into void
- Bridge geometry (types 12, 13, 80, 85)

**Monsters:**
- Hell Hound, Poison Bull, Thunder Lich (Lv 38-46)
- Dark Knight, Ghost, Larva (Lv 25-48)
- Hell Spider, Cyclops, Gorgon (Lv 28-55, boss)

### Map 4: Lost Tower (7 Floors)

**Atmosphere:**
- Dark tower dungeon with metallic/stone architecture
- Ambient: SOUND_DUNGEON01
- Music: lost_tower_a.mp3
- Features: None (indoor dungeon)
- Luminosity: 0.9

**Floors:** (gate-based teleport system, same map)
- Floor 1: Lorencia entry (gate 42) spawn (208,75)
- Floor 2-7: Internal gates (30→31, 32→33, 34→35, 36→37, 38→39, 40→41)

**Objects:** 5,380 total

*Active Objects:*
- 872 × Tree01 (gothic pillars/columns)
- 777 × Skull01 (type 38, interactive CheckSkull tracking)
- 399 × Tree04 (Chrome UV scroll), 211 × Tree03 (Chrome UV scroll)
- 95 × FlamePillar (type 24, HiddenMesh=-2, fire particles)
- 14 × MagicOrb (type 19), 6 × LightningOrb (type 20)

*Inactive/Removed Objects (still in .obj file):*
- 335 × LanceTrap (type 39) — Code/AI removed, non-functional
- 148 × MeteoriteTrap (type 25) — Code/AI removed, non-functional
- 1 × IronStickTrap (type 40) — Code/AI removed, non-functional

**VFX Effects:**
- Magic/lightning orbs (types 19-20): BITMAP_MAGIC+1 / BITMAP_LIGHTNING+1 at bones 15,19,21
- Skulls (type 38): Track player position, rotate toward player, SOUND_BONE2
- Flame pillars (type 24): 1/64 chance per frame to spawn BITMAP_FLAME
- Chrome trees (types 3-4): Metallic scrolling UV animation

**Monsters:** 446 spawns
- Poison Shadow (87), Shadow (71), Death Knight (71)
- Death Gorgon (61), Devil (54), Death Cow (51)
- Cursed Wizard (49), Balrog (2, boss)
- Level range: 47-66

---

## Removed Features & Legacy Objects

### Trap System (Types 100-103) - REMOVED

**Status**: All trap code, AI, VFX, database entries, and collision detection have been completely removed from the codebase.

**Why Removed**: Could not be migrated correctly from Main 5.2 due to complexity of client-side VFX synchronization with server-side damage triggers. The original implementation required tight coupling between client particle effects and server AI state machines that proved incompatible with our client-server separation architecture.

**Trap Types That Were Removed:**
- **Type 100**: Lance Trap (Object2/Object40.bmd) - Retractable lance emerging from ground
- **Type 101**: Iron Stick Trap (Object2/Object41.bmd) - Swinging iron bar
- **Type 102**: Fire Trap (Object2/Object52.bmd) - Directional flame jets
- **Type 103**: Meteorite Trap (Object5/Object26.bmd) - Falling rock debris

**Legacy Objects in Terrain Files:**

The trap objects **still exist in `.obj` files** as static decorative geometry but are completely non-functional:

| Map | Trap Objects Remaining in .obj |
|-----|-------------------------------|
| Dungeon | Type 39 (LanceTrap), Type 40 (IronStickTrap), Type 51 (FireTrap) |
| Lost Tower | Type 39 (LanceTrap, 335×), Type 25 (MeteoriteTrap, 148×), Type 40 (IronStickTrap, 1×) |

**Why They Remain:**
- `.obj` files are binary terrain data from Main 5.2 that we don't modify
- Removing them would require hex editing encrypted binary files
- They load as static geometry with no VFX, collision, or AI
- Harmless as decorative elements

**Code References:**
- All trap checks removed from: `RayPicker.cpp`, `CombatHandler.cpp`, `main.cpp`, `MonsterManager.cpp`, `MonsterManagerRender.cpp`, `GameWorld.cpp`
- Database: 205 trap spawns deleted via `DELETE FROM monster_spawns WHERE type >= 100 AND type <= 103`
- Memory note: `MEMORY.md` documents trap removal to prevent re-implementation

## Map Analysis Workflow

**Step 1: Extract terrain data**
```bash
./map_inspect ../client/Data/World1/EncTerrain1.att --zones --validate
./map_inspect ../client/Data/World1/EncTerrain1.map --stats --histogram
```

**Step 2: Analyze world objects**
```bash
./map_inspect ../client/Data/World1/EncTerrain1.obj --types --density
```

**Step 3: Get complete map intelligence**
```bash
./map_analyze --map 0 --all > lorencia_profile.txt
```

**Step 4: Cross-reference with Main 5.2**
- Check `references/other/Main5.2/Source Main 5.2/source/ZzzObject.cpp` for object rendering
- Check `ZzzCharacter.cpp` for monster VFX (MoveEye, blood, wave, hover)
- Check `ZzzBmd.cpp` for BlendMesh/HiddenMesh/StreamMesh handling

## Object VFX Implementation Checklist

When implementing a new object type, check:

1. **BlendMesh** - Which mesh index uses additive blend?
   - Search Main 5.2 for `o->BlendMesh = N` where N is the object type
   - Mesh with `Texture == BlendMesh` renders additive (GL_ONE, GL_ONE)

2. **HiddenMesh** - Is the mesh invisible?
   - `HiddenMesh = -2` means ALL meshes invisible (VFX-only object)
   - `HiddenMesh = N` means mesh N is invisible, others render

3. **StreamMesh** - Does it have UV scrolling?
   - `StreamMesh = N` → mesh N gets `texCoordOffset.y = -WorldTime * 0.001`

4. **Point Lights** - Does it emit dynamic light?
   - Fire objects (50-52, 55, 130, 150) have hardcoded RGB + range
   - Check `ObjectRenderer::CollectPointLights()` for the table

5. **Sound Effects** - Ambient sound loops?
   - Fire objects: SOUND_FIRE_LOOP (3D positioned)
   - Fountain: SOUND_WATERFALL_LOOP
   - Skulls: SOUND_BONE2 (proximity trigger)

6. **Particle Emitters** - Fire/smoke/magic?
   - Fire types: `FireEffect::AddEmitter(worldPos + yOffset)`
   - Water smoke: `FireEffect::AddWaterSmokeEmitter(worldPos + 180/120)`
   - Flame pillars: 1/64 random spawn BITMAP_FLAME

7. **Special Rendering** - Custom shaders or effects?
   - Chrome (types 3-4): Scrolling UV `BlendMeshTexCoordU`
   - Cliff walls (type 11): `cliffBottomFade` RGB darkening
   - Fountain: RENDER_WAVE texture animation
   - Skull: CheckSkull() rotation tracking

8. **Animation** - Does the BMD have actions?
   - Most world objects: 1 bone, 1 action, 1 keyframe (static)
   - Gates: 2 actions (open/close) with interpolation
   - Doors: multiple actions for opening sequences

## Validation & QA

**Common Issues:**

1. **Tile 255 in Layer1** — Invalid marker, renders as missing texture
   - Fix: Replace with nearest valid tile or fill with neutral brown

2. **Void without TW_NOGROUND** — Pathfinding allows walking into abyss
   - Fix: Set `TW_NOGROUND (0x08)` for all tile 255 cells

3. **Isolated Safe Zones** — Safe cells with no walkable neighbors
   - Fix: Either remove safe flag or add walkable connections

4. **Object Density Hotspots** — Too many objects in one region (performance)
   - Threshold: >50 objects per 8×8 grid region may cause FPS drops

5. **Bridge Coordinates** — Bridge objects must match void cell positions
   - `ReconstructBridgeAttributes()` marks cells within bridge radius
   - Height tolerance: ±200 units from bridge object height

## Tool Output Examples

### Example: Lorencia Full Analysis

```bash
$ ./map_analyze --map 0 --all

═══════════════════════════════════════════════════════════════
  MAP 0: Lorencia
═══════════════════════════════════════════════════════════════

┌─ ATMOSPHERE ─────────────────────────────────────┐
│ Description:   Warm grassland, gentle hills, fountain center
│ Ambient Sound: SOUND_WIND01
│ Safe Music:    MuTheme.mp3
│ Features:      Sky Grass Wind3D
│ Luminosity:    1
└──────────────────────────────────────────────────┘

┌─ TERRAIN DATA ───────────────────────────────────┐
│ Terrain File:  EncTerrain1
│ World Dir:     World1/
│ Safe Zone:      2560 cells ( 3.91%)
│ Walls/Blocked: 17962 cells (27.41%)
│ Void/NoGround:     0 cells ( 0.00%)
│ Walkable:      47574 cells (72.59%)
└──────────────────────────────────────────────────┘

┌─ WORLD OBJECTS ──────────────────────────────────┐
│ Total Objects: 2870
│
│ Type Distribution:
│   Type  24:  317 × FlamePillar
│   Type  50:   26 × FireLight01
│   Type  52:    9 × Bonfire01
│   ...
└──────────────────────────────────────────────────┘

┌─ MONSTER SPAWNS ─────────────────────────────────┐
│ Total Spawns:  156
│
│ Monster Distribution:
│   Type   0:   45 × Bull Fighter         (Lv  6, HP=100)
│   Type   1:   38 × Hound                (Lv  9, HP=140)
│   ...
│
│ Spawn Density Heatmap (16×16 regions):
│   [ASCII visualization]
└──────────────────────────────────────────────────┘
```

### Example: Attribute Zone Visualization

```bash
$ ./map_inspect ../client/Data/World1/EncTerrain1.att --zones

Legend: . = walkable, # = nomove, ~ = noground, S = safe, X = safe+nomove

................................................................
................................................................
....................SSSSSSSSSSSS................................
....................SSSSSSSSSSSS................................
................................................................
......######........................................############
......######........................................############
................................................................
```

## Integration with Claude.md

These tools complement the existing `bmd_inspect` tool to provide complete asset reverse engineering:

- **bmd_inspect** - Models (characters, monsters, weapons, items, world objects)
- **map_inspect** - Low-level terrain file format analysis
- **map_analyze** - High-level map intelligence (complete profiles)

Together they enable:
1. Finding missing VFX/sounds by comparing tool output with Main 5.2
2. Validating terrain data integrity before shipping
3. Understanding object placement patterns for new map design
4. Documenting map characteristics for wiki/guides
5. Debugging rendering issues (why is this object invisible? wrong BlendMesh)

## Future Enhancements

Potential additions:
- Export to JSON for automated tooling
- Generate minimap PNGs from terrain data
- Path validation (spawn → safe zone walkability check)
- Gate zone verification (ensure no teleport loops)
- Lightmap quality analysis (dark spot detection)
- Texture reference checker (verify all TileXX.OZJ exist)
- Monster level progression curves (XP efficiency)
- Object model existence verification (does ObjectXX.bmd exist?)
