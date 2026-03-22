# Rendering Pipeline

## Graphics Backend: BGFX

The renderer has been migrated from raw OpenGL 3.3 to **BGFX** (cross-platform rendering library). BGFX abstracts over Metal, OpenGL, Vulkan, and D3D backends — on macOS it uses **Metal** by default.

- **Shaders**: BGFX shader files in `client/shaders/bgfx/` (`.sc` format), compiled to platform-specific binaries (`.bin`) via `shaderc`. Old GLSL shaders (`client/shaders/*.vert`/`.frag`) have been removed.
- **Uniforms**: BGFX uses `vec4`-padded uniforms (e.g., `u_terrainParams` packs time, debugMode, luminosity into one `vec4`).
- **Vertex/Index buffers**: Use `bgfx::createVertexBuffer`/`bgfx::createIndexBuffer` with `bgfx::copy()`. Dynamic buffers use `bgfx::createDynamicVertexBuffer` with `BGFX_BUFFER_ALLOW_RESIZE`.
- **Texture arrays**: Terrain tile textures use `bgfx::createTexture2D` layered into a `SAMPLER2DARRAY`.
- **Shadow mapping**: Uses a separate BGFX view with framebuffer for depth-only pass, sampled in terrain/model shaders.

## Coordinate System
- MU Online uses Z-up right-handed: X-right, Y-forward, Z-up
- OpenGL uses Y-up right-handed: X-right, Y-up, Z-backward
- **Position mapping**: `GL = (MU_Y, MU_Z, MU_X)` -- cyclic permutation (det=1, no mirror)
- **Model geometry conversion**: `Rz(-90) * Ry(-90)` -- NOT Rx(-90)!
  - MU_X -> GL_Z (matches MU_X -> WorldZ position mapping)
  - MU_Y -> GL_X (matches MU_Y -> WorldX position mapping)
  - MU_Z -> GL_Y (height axis preserved)
- Terrain grid mapping: MU_Y (outer loop z) -> WorldX, MU_X (inner loop x) -> WorldZ
- World position = `(z * 100, height, x * 100)` where z/x are grid indices

## Per-Mesh Blend State (BGFX)

World objects use **two-pass rendering**: opaque meshes first (depth write ON), then alpha/additive meshes (depth write OFF). This prevents opaque geometry from cutting through transparent parts of the same model.

```
Pass 0 (opaque): meshes where !hasAlpha && !isWindowLight && !bright
  state = WRITE_RGB | WRITE_A | WRITE_Z | DEPTH_TEST_LESS | CULL_CW | MSAA

Pass 1 (transparent/additive): meshes where hasAlpha || isWindowLight || bright
  noneBlend  -> opaque, CULL_CW
  bright     -> BLEND_FUNC(ONE, ONE), no WRITE_Z (additive)
  hasAlpha   -> BLEND_FUNC(SRC_ALPHA, INV_SRC_ALPHA)
  windowLight -> BLEND_FUNC(ONE, ONE), no WRITE_Z + flicker intensity
```

## Per-Pixel Lightmap Sampling for World Objects

World objects now sample the terrain lightmap **per-pixel** in the fragment shader (`fs_model.sc`) instead of using a single per-object uniform. This provides smooth lighting across large meshes that span multiple lightmap cells.

- `u_terrainLight.w = 1.0` enables per-pixel sampling; `w = 0.0` uses per-object uniform (characters/NPCs)
- Lightmap texture bound to `s_lightMap` (sampler slot 2)
- UV derived from world position: `lmUV = vec2(v_fragpos.z / 25600.0, v_fragpos.x / 25600.0)`
- **Void area lighting**: When lightmap RGB is dark (void cells), shader blends in a height-based brightness (`voidBright = 0.35 + heightFade * 0.65`) to keep cliff faces visible

## Cliff Bottom Fade (Type 11)

Cliff wall objects (type 11) use RGB darkening to blend into the black void at their base. Implemented as a per-object uniform approach (not per-pixel lightmap) to avoid jagged artifacts at terrain/void boundaries.

- CPU: `ObjectRenderer` computes `cliffTopH` from 7x7 terrain heightmap area around object position
- Shader: `fadeFactor = smoothstep(cliffTopH - 500, cliffTopH - 200, v_fragpos.y)` darkens RGB
- Uses `u_params2.z` (flag) and `u_params2.w` (cliffTopH) uniforms
- **RGB darkening, not alpha transparency** — avoids Z-fighting with terrain at void edges

## Chrome Glow Rendering (Item Enhancement)

See [character-system.md](character-system.md) for the full pass structure and color tables. Summary:

- **Environment-map textures**: Chrome01.OZJ, Chrome02.OZJ, Shiny01.OZJ
- **UV generation**: Fragment shader derives UVs from vertex normals + time-based wave
  - CHROME: `UV = (N.z*0.5+wave, N.y*0.5+wave*2)`
  - CHROME2: `UV = ((N.z+N.x)*0.8+wave2*2, (N.y+N.x)*1.0+wave2*3)` with sinusoidal wave2
  - METAL: `UV = (N.z*0.5+0.2, N.y*0.5+0.5)` (static)
  - CHROME4: Dynamic light vector `L = (cos(t), sin(2t), 1)`, `UV = dot(N,L)` variants (most intense)
- **Blend state**: `GL_ONE, GL_ONE` additive, depth write off, face culling off
- **Color selection**: `GetPartObjectColor()` for CHROME/METAL, `GetPartObjectColor2()` for CHROME2/CHROME4

## ChromeGlow Item Enhancement

Items with +7 or higher level display a dynamic "chrome" enhancement effect.
- **Module**: `ChromeGlow.cpp` / `ChromeGlow.hpp`.
- **Implementation**: Second rendering pass with additive blend and environment-mapping.
- **Visuals**: Pulsing glow that flows across the item's surface.
- **Logic**: Linked to item's `lvl` attribute in `ItemModelManager`.

## Blob Shadow (ZzzBMD.cpp RenderBodyShadow)

Shadow projection formula in MU-local space:
```
pos.x += pos.z * (pos.x + 2000) / (pos.z - 4000)
pos.y += pos.z * (pos.y + 2000) / (pos.z - 4000)
pos.z = 5.0  // flatten to just above ground
```
- Facing rotation baked into vertices before projection
- Shadow model matrix: `translate(m_pos) * Rz(-90) * Ry(-90)` -- NO facing rotation
- Stencil buffer (`GL_EQUAL, 0` + `GL_INCR`) prevents overlap darkening — body, weapon, and shield draw as one unified shadow
- Must use `GL_INCR` not `GL_REPLACE` (GL_REPLACE with ref=0 writes 0, never blocks subsequent fragments)
- When depth test is disabled for shadows, render shadows BEFORE the character model or they appear on top
- Requires `glfwWindowHint(GLFW_STENCIL_BITS, 8)`
- Position-only VBOs (`GL_DYNAMIC_DRAW`), re-uploaded per frame via `glBufferSubData`

## World Object Rendering

Objects loaded from `EncTerrain1.obj`, parsed by `TerrainParser::ParseObjectsFile()`.

**Coordinate transform** (MU -> OpenGL):
- `WorldX = mu_pos[1]`, `WorldY = mu_pos[2]`, `WorldZ = mu_pos[0]`

**Per-instance model matrix**: translate -> Rz(-90) -> Ry(-90) -> MU_rotateZ/Y/X -> scale

**MU rotation convention** (AngleMatrix in ZzzMathLib.cpp):
- angles[0]=X (pitch), angles[1]=Y (yaw), angles[2]=Z (roll)
- Applied as Z*Y*X matrix
- Large angle values (e.g. 900) are valid -- sin/cos handle wrapping

**Model caching**: BMD models loaded once per type, shared across all instances. 109 unique models for ~2870 Lorencia instances.

## RENDER_WAVE -- NOT For World Objects

`RENDER_WAVE` (ZzzBMD.cpp:1331) is a procedural sine-wave vertex displacement. Formula: `pos += normal * sin((WorldTime_ms + vertexIndex * 931) * 0.007) * 28.0`. **ONLY** used for `MODEL_MONSTER01+51`, never for world objects.

## Roof Hiding System (objectAlpha)

When player stands on tile with `layer1 == 4` (building interior), types 125 (HouseWall05) and 126 (HouseWall06) fade to invisible. Reference: ZzzObject.cpp:3744.

- `objectAlpha` uniform in `model.frag`: multiplies fragment alpha
- Exponential ease: `alpha += (target - alpha) * (1 - exp(-20 * dt))`
- Objects with alpha < 0.01 skipped entirely

## EncTerrain.obj Format

Encrypted with same XOR key as other map files.
```
Header: Version(1) + MapNumber(1) + Count(2 bytes, short)
Per object (30 bytes each):
  Type:     short (2)    -- maps to BMD filename
  Position: vec3 float (12) -- MU world coords (TERRAIN_SCALE=100 scaled)
  Angle:    vec3 float (12) -- rotation in degrees
  Scale:    float (4)
```
Lorencia (World1): 2870 objects, 109 unique model types.

## Object Type-to-BMD Mapping

Follows `AccessModel()` convention (MapManager.cpp):
- `AccessModel(TYPE, "Data\\Object1\\", "BaseName", index)` -> `BaseName0X.bmd` or `BaseNameXX.bmd`

Key ranges: Tree(0-19), Grass(20-29), Stone(30-39), StoneStatue(40-42), Tomb(44-46), FireLight(50-51), Bonfire(52), DoungeonGate(55), SteelWall(65-67), StoneWall(69-74), StoneMuWall(75-78), Bridge(80), Fence(81-84), StreetLight(90), Cannon(91-93), Curtain(95), House(115-119), HouseWall(121-126), Furniture(140-146).

## GIF Capture Optimizations

- Resolution Downscaling: box-filter averaging during capture
- Frame Diffing: unchanged pixels marked transparent
- Dirty Rectangle Encoding: only changed bounding box per frame
- Frame Skipping: configurable capture frequency
