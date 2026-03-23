# Terrain & Environment Systems

## Water Rendering (Terrain Shader)

The original MU engine for Lorencia renders water as a **regular tile** (layer1 index 5) with animated UV scrolling -- no overlay, no proximity kernel, no blue tint.

- `sampleLayerSmooth()` handles water tile animation: `tileUV.x += uTime * 0.1` + sinusoidal Y offset
- Shore transitions: handled naturally by alpha map blending between Layer1 and Layer2
- Bridge protection: `sampleLayerSmooth()` checks `TW_NOGROUND` (0x08) on bilinear neighbors.
- **Bridge Terrain Protection** (Devias, Dungeon): Rift chasms use `TW_NOGROUND` (0x08) which renders as voids by skipping cell index generation. Bridges span these voids:
    - `ReconstructBridgeAttributes()` scans bridge objects (type 80/85) and builds a `bridgeZone` mask marking all cells within bridge radius whose height is within 200 units of the bridge object.
    - `rawAttributes` (saved before reconstruction) are used for mesh generation. `bridgeZone` cells are exempt from void-skipping in index generation and from void vertex sinking.
    - **Interior void sinking**: Void vertices are sunk 600 units and darkened to black, but ONLY if all 4 neighboring quads are also void (interior cells). Border void vertices shared with rendered terrain keep their original height to prevent triangle stretching at void edges.
    - Tile 255 (invalid) in bridge zone cells is patched to nearest valid tile within a 10-cell radius.
    - Dungeon bridge types: {80 (Bridge), 85 (BridgeStone)}.

**Lesson learned**: Do not invent rendering systems that don't exist in the original source. For Lorencia, water is just a tile with animated UVs. Special water overlays only exist for Atlantis (WD_7ATLANSE). When sinking void vertices to hide abyss clipping, only sink interior void vertices — border vertices are shared with rendered terrain quads and sinking them stretches adjacent triangles.

## Terrain Walkability Flags

Terrain attribute flags (from `_define.h`):
- `TW_SAFEZONE` (0x01) — Safe zone, no PvP
- `TW_NOMOVE` (0x04) — Movement blocked (walls, obstacles)
- `TW_NOGROUND` (0x08) — No ground (bridges, voids, cliff edges)

**Movement checks must test BOTH `TW_NOMOVE | TW_NOGROUND`** to block walking into voids. Only `TW_SAFEZONE` is excluded from movement blocking (used only for safe zone detection).

**Lesson learned**: Client-side `RayPicker::IsWalkable()` and `PathFinder` walkability lambda must check `(attr & (TW_NOMOVE | TW_NOGROUND)) == 0`. Checking only `TW_NOMOVE` allows players to walk into void/cliff cells. Server (`GameWorld::IsWalkable`) correctly checks both.

## Terrain Tile Index 255

Layer1/layer2 may contain tile index 255 as "empty/invalid" marker. Fill unloaded texture slots with **neutral dark brown (80, 70, 55)** to blend with surrounding terrain. Magenta debug fill causes pink artifacts through bilinear blending.

## Terrain Void Mesh Generation

Terrain mesh generation skips quads where the **top-left corner** has the void flag (`TW_NOGROUND` 0x08). Only the primary corner is checked — this intentionally lets terrain quads extend one cell into void areas, filling gaps behind world objects placed at void edges.

**Lesson learned**: Checking all 4 corners of each quad for void status creates larger terrain mesh gaps, exposing geometry holes in world objects (tentacles, cliff walls) placed at void edges. The single-corner check is correct.

## Lightmap Alpha Channel

The terrain lightmap (RGBA32F) stores height data in the alpha channel:
- **Terrain cells**: `alpha = -1.0f` (sentinel value)
- **Void cells**: `alpha = nearTerrainH[i]` (height of nearest terrain cell, propagated via multi-pass sweep)

This is used by `fs_model.sc` for cliff-face lighting in void areas — height-based brightness when lightmap RGB is dark.

## Terrain Lightmap on Objects

Objects use **per-pixel lightmap sampling** in the fragment shader for smooth lighting across large meshes.
- `u_terrainLight.w = 1.0` → per-pixel sampling from `s_lightMap` texture (sampler slot 2)
- `u_terrainLight.w = 0.0` → per-object uniform fallback (characters/NPCs)
- UV derived from world position: `lmUV = vec2(fragpos.z / 25600, fragpos.x / 25600)`

## Point Light System

64 point lights from fire emitter objects. Used differently for terrain vs objects:

**Terrain**: CPU-side `AddTerrainLight` matching original (ZzzLodTerrain.cpp). Each frame: reset lightmap, add lights to 256x256 grid cells with **linear falloff** and **cell range 3**, re-upload via `glTexSubImage2D`. Colors scaled x0.35.

**Objects/Characters**: Per-pixel in `model.frag` shader. Uniforms: `pointLightPos[64]`, `pointLightColor[64]`, `pointLightRange[64]`, `numPointLights`.

**Lesson learned**: Original MU applies dynamic lights to terrain via CPU-side lightmap grid modification, NOT per-pixel shader computation. Per-pixel with large ranges creates reddish spots.

## BlendMesh System (Window Light / Glow)

Marks specific mesh indices for additive blending (window glow, lamp light).

**Matching**: `Mesh_t::Texture` compared against per-type BlendMesh value. Match -> `mb.isWindowLight = true`.

**Per-type BlendMesh table**:
- 117 (House03) -> texID 4, 118 (House04) -> 8, 119 (House05) -> 2
- 122 (HouseWall02) -> 4, 52 (Bonfire01) -> 1, 90 (StreetLight01) -> 1
- 150 (Candle01) -> 1, 98 (Carriage01) -> 2, 105 (Waterspout01) -> 3

**BlendMeshLight intensity rules**:
- 117 (House03), 122 (HouseWall02): sin-based flicker (0.4-0.7)
- 118 (House04), 119 (House05): flicker + UV scroll
- 52 (Bonfire01): wider flicker (0.4-0.9)
- 90, 150, 98: constant 1.0 (no flicker)
- 105 (Waterspout01): constant 1.0 + UV scroll

## Dungeon Object Rendering (Map 1)

### Face Culling Disabled Objects

Thin/double-sided geometry in the dungeon disables backface culling (`BGFX_STATE_CULL_CW` removed):

| Types | Object | Notes |
|-------|--------|-------|
| 11 | Cliff/rock wall (Object11.bmd) | Cliff fade to black at base |
| 22-24 | Tentacle/steam objects | OZJ texture (no alpha), mesh extends into void |
| 35 | Tentacle/steam variant | Same behavior as 22-24 |
| 44-46 | Coffins/sarcophagi | Thin lids |
| 53 | Squid tentacle (2 meshes, 9 bones) | squid01.jpg + bons.jpg |

**Tentacle mesh gaps (types 23, 24, 35)**: These meshes have geometry that doesn't fully cover the void area behind them. The terrain mesh at void edges fills this gap — this is why the terrain void check must use single-corner (top-left only), not all-4-corners. The tentacle textures are OZJ (JPEG, no alpha channel) so `texColor.a = 1.0` always.

### Dungeon Water (Types 22-24)

StreamMesh=1: mesh index 1 renders with additive blend + V-axis UV scroll.
Formula: `texCoordOffset.y = -(WorldTime % 1000) * 0.001f`

### Dungeon Torches (Types 41-42)

BlendMesh=1 (fire glow mesh). Per-torch phase offset prevents synchronized flicker:
`intensity = 0.78 + 0.10*sin(t*3.8+phase) + 0.06*sin(t*9.5+phase*2.1)`

## Fire System

- Data: `Data/Effect/Fire01.OZJ` (animated billboard strip)
- Fire types: 50-51 (FireLight), 52 (Bonfire), 55 (DungeonGate), 80 (Bridge), 130 (Light01)
- GPU Instancing: single `glDrawElementsInstanced` call
- Additive Blending: `GL_ONE, GL_ONE` with `glDepthMask(GL_FALSE)`

## Grass Pushing System

Grass billboard vertices near player pushed away (GMHellas.cpp:402 CheckGrass).
- Top vertices within `pushRadius` (150 units) of `ballPos` XZ displaced away
- Quadratic falloff + slight downward bend
- `ballPos` and `pushRadius` uniforms set per frame

## Luminosity System (Day/Night)

Uniform `luminosity` in `model.frag` and terrain shader. Currently disabled (defaults to 1.0).

**Original formula**: `g_Luminosity = sinf(WorldTime * 0.004f) * 0.15f + 0.6f` -- range 0.45-0.75, ~26 minute period. From ZzzAI.cpp.

## HouseEtc01-03 (Types 127-129)

HouseEtc01 has 2 meshes: pole + flag cloth. 1 bone, 1 keyframe. **Completely static in original MU**. 4 instances surround the fountain.
