# Lost Tower - Complete Map Analysis

**Map ID**: 4 (EncTerrain5)
**Region**: World5/
**Type**: Multi-floor dungeon (7 floors, single map instance)

---

## Summary

Lost Tower is the most complex dungeon in the game with 7 interconnected floors accessed via internal gate teleport system. Features high-level monsters (Lv 47-66), dense gothic architecture with metallic chrome elements, animated interactive skulls, and dark atmospheric lighting.

### Key Statistics
- **Terrain Size**: 255×255 grid (65,025 cells)
- **Walkable Area**: 46.13% (30,233 cells) - tight corridors and rooms
- **Total Objects**: 5,380 instances (37 unique types)
- **Monster Spawns**: 446 (8 unique types, Lv 47-66)
- **Safe Zones**: 0.60% (392 cells) - minimal safe areas per floor
- **Luminosity**: 0.9 (darker than outdoor maps)

---

## Atmosphere & Environment

**Description**: Dark tower dungeon with metallic/stone gothic architecture, 7 vertical floors connected by teleport gates

**Audio**:
- Ambient Sound: `SOUND_DUNGEON01` (echoing dungeon ambience)
- Combat Music: `lost_tower_a.mp3`

**Features**:
- No sky rendering
- No grass/vegetation
- No wind effects
- Dark atmospheric lighting (0.9 luminosity)

---

## Floor System

Lost Tower uses a **gate-based teleport system** on a single map instance. Floor detection uses nearest-anchor-point algorithm based on gate spawn coordinates.

| Floor | Gate Pairs | Description | Key Coordinates |
|-------|-----------|-------------|----------------|
| 1 | Entry (42) | Lorencia entrance | Spawn: (208, 75) |
| 2 | 30↔31 | First ascent | (190,7) ↔ (242,237) |
| 3 | 32↔33 | Second level | (86,167) ↔ (166,164) |
| 4 | 34↔35 | Mid tower | (87,87) ↔ (133,245) |
| 5 | 36↔37 | Upper level | (129,53) ↔ (133,135) |
| 6 | 38↔39 | High tower | (53,53) ↔ (131,16) |
| 7 | 40↔41 | Tower peak | (8,86) ↔ (6,6) |

**HUD Display**: "Lost Tower 1", "Lost Tower 2", ..., "Lost Tower 7"
**Floor 1** shows just "Lost Tower" (base name only)

---

## Terrain Layout

### Attribute Distribution (255×255 grid)
```
Total Cells:    65,535 (255×255)
Walkable:       30,233 cells (46.13%)
Walls/Blocked:  27,404 cells (41.82%)
Void/NoGround:  16,014 cells (24.44%) - deep pits and chasms
Safe Zones:        392 cells ( 0.60%) - minimal safe areas
```

### Terrain Features
- **Tight corridors**: Narrow passageways between rooms
- **Multi-level vertical design**: Gates connect floors
- **Void pits**: 24% of map is deep chasms (TW_NOGROUND)
- **High wall density**: 42% blocked terrain creates maze-like layout

### Texture Layers (256×256)
- **Layer1**: 10 unique tile types (primary stone/metal textures)
- **Layer2**: 8 unique tile types (detail overlays)
- **Alpha Blending**: 67% pure Layer1, 10% pure Layer2, 23% blended

---

## World Objects (5,380 total)

### Active Decorative Objects

| Type | Count | Name | VFX/Features |
|------|-------|------|--------------|
| 1 | 872 | Tree01 | Gothic pillars/columns |
| 38 | 777 | Skull01 | Interactive: tracks player, rotates, SOUND_BONE2 |
| 4 | 399 | Tree04 (Chrome) | Metallic scrolling UV animation |
| 5 | 357 | Tree05 | Standard decorative |
| 3 | 211 | Tree03 (Chrome) | Metallic scrolling UV animation |
| 24 | 95 | FlamePillar | HiddenMesh=-2, BITMAP_FLAME particles (1/64 prob) |
| 19 | 14 | MagicOrb | BITMAP_MAGIC+1 sprites, purple light (255,150,255,300) |
| 20 | 6 | LightningOrb | BITMAP_LIGHTNING+1 sprites, blue light (150,150,255,300) |
| 13 | 25 | BridgeSupport01 | Vertical bridge posts |
| 12 | 11 | BridgePlank01 | Bridge planks |
| 11 | 8 | CliffWall | Cliff face fade effect |

### Removed/Inactive Objects

**Status**: These objects exist in `EncTerrain5.obj` but are **completely non-functional**. All trap code, AI, VFX, collision detection, and database entries have been removed.

| Type | Count | Name | Original Purpose |
|------|-------|------|------------------|
| 39 | 335 | LanceTrap | Retractable lance emerging from ground |
| 25 | 148 | MeteoriteTrap | Falling rock debris with fire burst |
| 40 | 1 | IronStickTrap | Swinging iron bar |

**Why Removed**: Migration from Main 5.2 failed due to client-server VFX synchronization complexity. The original trap system required tight coupling between client particle effects and server damage triggers that proved incompatible with our architecture.

**Current State**: Load as static geometry with no visual effects, sounds, collision, or AI. Harmless decorative elements.

### Unknown Objects

418 objects of unknown types (16 unique type IDs) - likely decorative elements not yet cataloged.

---

## Monster Population (446 spawns)

### Monster Distribution by Type

| Type | Count | Name | Level | HP | Behavior |
|------|-------|------|-------|-----|----------|
| 39 | 87 | Poison Shadow | 50 | 3,500 | Aggressive melee |
| 36 | 71 | Shadow | 47 | 2,800 | Aggressive melee |
| 40 | 71 | Death Knight | 62 | 5,500 | Aggressive melee |
| 35 | 61 | Death Gorgon | 64 | 6,000 | Aggressive melee |
| 37 | 54 | Devil | 60 | 5,000 | Aggressive ranged (7 tile range) |
| 41 | 51 | Death Cow | 57 | 4,500 | Aggressive melee |
| 34 | 49 | Cursed Wizard | 54 | 4,000 | Aggressive ranged (7 tile range) |
| 38 | 2 | Balrog | 66 | 9,000 | Boss-tier, aggressive ranged |

### Monster Characteristics
- **Level Range**: 47-66 (end-game content)
- **HP Range**: 2,800 - 9,000 (high durability)
- **Aggression**: All monsters are aggressive with 5-7 tile view range
- **Ranged Attackers**: Cursed Wizard, Devil, Balrog (4-tile attack range)
- **Spawn Density**: Max 9 monsters per 16×16 region
- **Boss Monsters**: 2 Balrog spawns (Lv 66, 9,000 HP)

### Spawn Pattern Analysis
```
Spawn Density Heatmap (16x16 regions, each 16×16 grid cells):
Max density: 9 spawns/region
Distribution: Spread across all 7 floors with higher concentration in corridors
```

---

## Visual Effects & Interactive Elements

### Animated Objects

**Skull01 (Type 38)** - 777 instances
- Model: `Object5/Object39.bmd`
- Behavior: `CheckSkull()` function tracks player position
- Animation: Rotates toward player continuously
- Sound: `SOUND_BONE2` (bone rattle)
- Purpose: Interactive environmental hazard/decoration

**Chrome Trees (Types 3, 4)** - 610 instances
- Models: `Object1/Tree03.bmd`, `Object1/Tree04.bmd`
- VFX: Metallic scrolling UV animation (`BlendMeshTexCoordU`)
- Effect: Gives otherworldly/mystical appearance to pillars

### Particle Effects

**MagicOrb (Type 19)** - 14 instances
- Model: `Object5/Object20.bmd`
- VFX: `BITMAP_MAGIC+1` billboard sprites at bones 15, 19, 21
- Point Light: Purple (255, 150, 255) with 300 unit radius
- Behavior: Rotating sprites creating magical ambient glow

**LightningOrb (Type 20)** - 6 instances
- Model: `Object5/Object21.bmd`
- VFX: `BITMAP_LIGHTNING+1` billboard sprites at bones 15, 19, 21
- Point Light: Blue (150, 150, 255) with 300 unit radius
- Behavior: Rotating sprites creating electric ambient glow

**FlamePillar (Type 24)** - 95 instances
- Model: `Object5/Object25.bmd`
- VFX: `HiddenMesh=-2` (invisible geometry), `BITMAP_FLAME` particles
- Spawn Rate: 1/64 probability per frame
- Effect: Fire columns rising from invisible bases

---

## Technical Details

### File Structure
```
World5/
├── EncTerrain5.map      (196,610 bytes, encrypted)
├── EncTerrain5.att      (65,540 bytes, encrypted with BuxConvert)
├── EncTerrain5.obj      (161,404 bytes, 5380 objects)
├── TerrainHeight.OZB    (65,536 bytes, 256×256 uint8)
└── TerrainLight.OZJ     (JPEG compressed RGB lightmap)
```

### Encryption
- **Map file**: XOR cipher with 16-byte key table + rolling key
- **Attribute file**: XOR cipher + BuxConvert transformation
- **Object file**: Unencrypted binary structures

### Map Name Texture
- **OZT File**: `Data/Local/Eng/ImgsMapName/losttower.OZT`
- **Loading**: Prefix matching (`strncmp(name, "Lost Tower", 10)`) allows floor-suffixed names like "Lost Tower 3" to find base texture
- **Display**: Top-right HUD and full-screen region name overlay

---

## Removed Features

### Trap System (Types 25, 39, 40, 51)

**Total Inactive Trap Objects**: 484 instances (335 + 148 + 1)

These trap objects remain in `EncTerrain5.obj` as legacy data from Main 5.2 but are **completely non-functional**:

**What Was Removed:**
- ✗ All client-side VFX (fire particles, lance animation, meteorite fall)
- ✗ All server-side AI (pressure-plate detection, damage triggers)
- ✗ All collision detection (trap targeting, invulnerability)
- ✗ All database spawn entries (205 rows deleted)
- ✗ Sound effects and damage formulas

**Why They Remain in .obj File:**
- Binary terrain data from Main 5.2 that we don't modify
- Removing would require hex-editing encrypted files
- Load as inert static geometry with no effects
- Harmless as background decoration

**Code Removal Locations:**
- `RayPicker.cpp`: Trap targeting skip (lines 211-213) - REMOVED
- `CombatHandler.cpp`: Trap invulnerability checks (5 locations) - REMOVED
- `main.cpp`: Trap fire emitter registration - REMOVED
- `MonsterManager.cpp`: Trap attack VFX - REMOVED
- `MonsterManagerRender.cpp`: Trap ambient VFX - REMOVED
- `GameWorld.cpp`: Trap AI processing - REMOVED
- Database: 205 spawn rows deleted

**Memory Note**: `MEMORY.md` documents trap removal to prevent accidental re-implementation.

---

## Implementation Checklist

When working with Lost Tower:

- [x] Floor detection system (`DetermineFloor()` in main.cpp)
- [x] Floor-specific HUD display ("Lost Tower 1" through "Lost Tower 7")
- [x] Map name texture loading (`losttower.OZT` with prefix matching)
- [x] Gate teleport system (12 gates, 6 bidirectional pairs)
- [x] Monster spawns (446 total, all aggressive, Lv 47-66)
- [x] Skull01 tracking behavior (`CheckSkull()` rotation)
- [ ] Chrome tree UV scrolling animation (types 3, 4)
- [ ] MagicOrb/LightningOrb particle sprites (types 19, 20)
- [ ] FlamePillar fire particle emission (type 24)
- [ ] Ambient dungeon sound loop (`SOUND_DUNGEON01`)
- [ ] Dark lighting atmosphere (0.9 luminosity factor)

### Not Implemented (By Design)
- ✗ Trap VFX/AI (types 25, 39, 40) - Removed, will not re-implement
- ✗ Skull02, LightningColumn - Do not exist in actual terrain file

---

## Validation & Testing

**Walkability**: Verify pathfinding works across all 7 floors, no walking into void (TW_NOGROUND) cells
**Gate Teleports**: Test all 12 gates, confirm no teleport loops (3s cooldown working)
**Monster AI**: Verify 446 spawns distribute correctly across floors, aggressive behavior at 5-tile range
**VFX Performance**: 777 skulls + 95 flame pillars + orbs should maintain 60 FPS
**Floor Detection**: Walk through gates, confirm HUD shows correct "Lost Tower N"
**Trap Objects**: Confirm types 25/39/40 render as inert static geometry with no effects

---

## Cross-Reference

**Main 5.2 Source**:
- `ZzzObject.cpp` - Object rendering, CheckSkull(), BlendMeshTexCoordU, HiddenMesh
- `ZzzCharacter.cpp` - Monster VFX (MoveEye for monsters, no RenderEye for Death Cow)
- `ZzzBmd.cpp` - BMD loading, mesh visibility flags
- `NewUIMapName.cpp` - Map name texture display system

**OpenMU Reference**:
- `GameLogic/Maps/LostTower.cs` - 7-floor structure definition
- `GameLogic/NPC/MonsterSpawnArea.cs` - Spawn distribution patterns

**Codebase**:
- [client/src/main.cpp:237-318](../client/src/main.cpp#L237-L318) - Floor detection, gate anchor points
- [client/src/InventoryUI.cpp:972-990](../client/src/InventoryUI.cpp#L972-L990) - Map name OZT loading
- [server/src/Server.cpp:1120-1156](../server/src/Server.cpp#L1120-L1156) - Gate teleport coordinates
- [server/src/GameWorld.cpp:135-215](../server/src/GameWorld.cpp#L135-L215) - Monster definitions

---

## Map Analysis Commands

```bash
# Full profile
./map_analyze --map 4

# Objects only (faster)
./map_analyze --map 4 --objects

# Monsters only (with spawn heatmap)
./map_analyze --map 4 --monsters

# Low-level terrain stats
./map_inspect ../client/Data/World5/EncTerrain5.att --stats

# Attribute zone visualization
./map_inspect ../client/Data/World5/EncTerrain5.att --zones

# Object type distribution
./map_inspect ../client/Data/World5/EncTerrain5.obj --types

# Object density heatmap
./map_inspect ../client/Data/World5/EncTerrain5.obj --density

# Complete analysis suite
./map_inspect ../client/Data/World5/ --all
```
