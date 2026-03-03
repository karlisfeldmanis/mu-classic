# Build & Project Structure

## Build

```bash
# Client (always use Release)
cd client/build && cmake -DCMAKE_BUILD_TYPE=Release .. && ninja

# Server
cd server/build && cmake .. && ninja
```

**Always use Release builds** (`-DCMAKE_BUILD_TYPE=Release`) for the client — Debug builds have significant performance issues due to unoptimized rendering code.

### Launcher Script

Use `run.sh` from the project root to launch with correct working directories and data symlinks:

```bash
./run.sh          # Launch server + client (default)
./run.sh client   # Launch client only
./run.sh server   # Launch server only
./run.sh both     # Launch server + client
```

The script ensures `Data/` symlinks exist in both build directories before launching. When using `both`, the server is stopped automatically when the client exits.

Client targets: `MuRemaster` (game client), `ModelViewer` (BMD object browser), `CharViewer` (character animation browser).
Server target: `MuServer` (game server).
Dependencies: glfw3, GLEW, OpenGL, OpenAL, libjpeg-turbo (TurboJPEG), GLM (header-only), ImGui, giflib, minimp3 (header-only), SQLite3 (server only).

### Data Directory

`client/Data/` is the canonical data directory (local copy, gitignored). CMake auto-creates `build/Data` symlinks in both client and server build directories pointing to `client/Data/`. All assets from Main 5.2, `Player.bmd` has 247 actions.

### macOS Specifics
- **Window Activation**: Uses `activateMacOSApp()` (Objective-C runtime) to force the GLFW window to the foreground on launch.
- **GLEW Header Order**: `GL/glew.h` **must** be included before `GLFW/glfw3.h` to prevent symbol conflicts.
- **Metal Translation Layer VBO Updates**: On macOS (OpenGL->Metal), `glBufferSubData` works for dynamic VBO updates. `glBufferData` + VAO re-setup does NOT work reliably. Always use `glBufferSubData` for animated mesh re-skinning.

## Client Architecture

The client is organized into focused modules, each owning a specific subsystem. Modules communicate via non-owning pointer context structs (e.g., `InventoryUIContext`, `InputContext`, `ClientGameState`).

```
main.cpp (orchestrator: init, render loop, shutdown)
  ├── InputHandler        -- GLFW callbacks, mouse/keyboard, click-to-move
  ├── InventoryUI         -- Inventory/equipment panels, drag-drop, tooltips, stat allocation
  ├── ItemDatabase        -- Static item definitions (293 items), lookup functions
  ├── ItemModelManager    -- 3D item model loading/rendering (UI slots + world drops)
  ├── GroundItemRenderer  -- Ground drop physics, 3D models, floating labels
  ├── FloatingDamageRenderer -- Floating damage/XP/heal numbers
  ├── RayPicker           -- Mouse ray-cast: terrain, NPC, monster, ground item picking
  ├── ClientPacketHandler -- Incoming packet dispatch (initial sync + game loop)
  ├── ServerConnection    -- Typed send API wrapping NetworkClient
  ├── HeroCharacter       -- Player model, movement, combat, equipment visuals
  ├── MonsterManager      -- Monster rendering, state machine, nameplates
  ├── NpcManager          -- NPC rendering, labels
  ├── ObjectRenderer      -- World objects (2870+ instances), BlendMesh, roof hiding
  ├── Terrain             -- Heightmap mesh, tile blending, lightmap
  ├── FireEffect / VFXManager -- Particle effects
  ├── GrassRenderer       -- Billboard grass with wind
  ├── SoundManager        -- OpenAL sound engine + MP3 music
  └── Sky                 -- Sky dome
```

## Source Files

### Headers (include/)

| File | Purpose |
|------|---------|
| **Core Engine** | |
| `BMDStructs.hpp` | Data structures for BMD binary model format (Vertex_t, Normal_t, Triangle_t, Mesh_t, Bone_t, Action_t) |
| `BMDParser.hpp` | `BMDParser::Parse(path)` returns `unique_ptr<BMDData>`. Handles version 0xC encryption. |
| `BMDUtils.hpp` | Bone math: `ComputeBoneMatrices()`, `ComputeBoneMatricesInterpolated()` (slerp), `MuMath::TransformPoint/RotateVector`, AABB. Uses 3x4 row-major matrices. |
| `TextureLoader.hpp` | OZJ/OZT loading, texture resolution, `TextureScriptFlags` (_R/_H/_N suffixes), `TextureLoadResult` (ID + hasAlpha). |
| `MeshBuffers.hpp` | Per-mesh GPU state: VAO/VBO/EBO + rendering flags (hasAlpha, noneBlend, hidden, bright) + BlendMesh + animation. |
| `Shader.hpp` | RAII OpenGL shader wrapper. `use()`, `setMat4()`, `setVec3()`, `setBool()`. |
| `Camera.hpp` | Isometric camera with zoom, state persistence (`camera_save.txt`). |
| `ViewerCommon.hpp` | Shared viewer utilities: OrbitCamera, DebugAxes, UploadMeshWithBones/RetransformMeshWithBones helpers. |
| **World Rendering** | |
| `Terrain.hpp` | 256x256 heightmap mesh, 4-tap tile blending, lightmap integration. |
| `TerrainParser.hpp` | Parses MAP heightmaps, tile layers, alpha maps, attributes, objects, lightmaps. `TERRAIN_SIZE = 256`. |
| `ObjectRenderer.hpp` | World object rendering: BMD model cache, per-instance transforms, BlendMesh glow, per-type alpha (roof hiding). |
| `GrassRenderer.hpp` | Billboard grass system: wind animation, ball-push displacement, 3 texture layers. |
| `Sky.hpp` | Sky dome: gradient hemisphere rendered behind scene. |
| `FireEffect.hpp` | Particle-based fire system for Lorencia torches/bonfires/lights. GPU instancing + billboarding. |
| `VFXManager.hpp` | Visual effects: particle bursts (blood/fire/energy/spark/smoke), ribbon trails (lightning), 3D model effects (meteor falling fireball, lightning bolt, poison cloud), GPU instanced billboards. |
| **Sound** | |
| `SoundManager.hpp` | `namespace SoundManager`: OpenAL sound engine. `SoundId` enum (Main 5.2 IDs), `Play/Play3D/PlayPitched/PlayLoop/Stop`, MP3 music via minimp3 (`PlayMusic/StopMusic`). |
| **Characters & Entities** | |
| `HeroCharacter.hpp` | Player character: 5-part DK model, click-to-move, combat, weapon attachment (bone 33/42/47), equipment visuals, blob shadow. |
| `MonsterManager.hpp` | Monster system: multi-type rendering, server-driven state machine (7 states), skeleton weapon attachment, arrow projectiles, debris, nameplate rendering. |
| `NpcManager.hpp` | NPC rendering, name labels, two-phase init (models then server-spawned instances). |
| `ClickEffect.hpp` | Click-to-move visual feedback: animated ring effect at click position. |
| **Items & Inventory** | |
| `ItemDatabase.hpp` | `namespace ItemDatabase`: 293 item definitions, lookup by defIndex/category, body part mapping. |
| `ItemModelManager.hpp` | `class ItemModelManager`: static cache of 3D item BMDs, `RenderItemUI()` + `RenderItemWorld()`. |
| `GroundItemRenderer.hpp` | `FloatingDamage` struct, `FloatingDamageRenderer::Spawn/UpdateAndRender`, `GroundItemRenderer` physics/models/labels. |
| `InventoryUI.hpp` | `namespace InventoryUI`: inventory/equipment panels, drag-drop, tooltips, stat allocation, quick slot. Context via `InventoryUIContext`. |
| `ClientTypes.hpp` | Shared client types: `ClientItemDefinition`, `ClientInventoryItem`, `ClientEquipSlot`, `GroundItem`, `ServerData`. |
| **Networking** | |
| `NetworkClient.hpp` | Low-level TCP socket: connect, send, recv with packet framing. |
| `ServerConnection.hpp` | `class ServerConnection`: typed send API (`SendAttack`, `SendPickup`, `SendEquip`, etc.) wrapping NetworkClient. |
| `ClientPacketHandler.hpp` | `namespace ClientPacketHandler`: incoming packet dispatch. Context via `ClientGameState`. |
| **Input & UI** | |
| `InputHandler.hpp` | `namespace InputHandler`: GLFW callbacks (mouse, keyboard, scroll), `ProcessInput()`. Context via `InputContext`. |
| `RayPicker.hpp` | `namespace RayPicker`: mouse-to-world raycasting for terrain, NPCs, monsters, ground items. |
| `UICoords.hpp` | HUD coordinate system with centered scaling. |
| `Screenshot.hpp` | JPEG capture + GIF recording (frame-diffed animation). |

### Sources (src/)

| File | Purpose |
|------|---------|
| **Core Engine** | |
| `BMDParser.cpp` | BMD decryption (XOR key + cumulative wKey) and binary parsing. |
| `BMDUtils.cpp` | Euler->quaternion->matrix, parent-chain bone concatenation, quaternion slerp interpolation. |
| `TextureLoader.cpp` | OZJ (JPEG+header) and OZT (TGA+header) loading with RLE, V-flip, extension resolution. |
| `Camera.cpp` | Camera math, smooth zoom lerp, state save/load. |
| `ViewerCommon.cpp` | Shared viewer boilerplate. |
| **World Rendering** | |
| `Terrain.cpp` | Terrain vertex grid generation, texture array loading, shader-based 4-tap blending. |
| `TerrainParser.cpp` | Decrypts and parses terrain files: heightmap, mapping, attributes, objects, lightmap. |
| `ObjectRenderer.cpp` | Loads BMD models by type ID, caches GPU meshes, renders 2870+ instances with BlendMesh, terrain lightmap. |
| `GrassRenderer.cpp` | 42k grass billboards with GPU vertex shader wind and ball-push displacement. |
| `Sky.cpp` | Sky dome rendering. |
| `FireEffect.cpp` | Fire particle system: emitter management, GPU instanced billboards. |
| `VFXManager.cpp` | VFX: particle bursts (10+ types), ribbon trails (lightning skill), 3D model effects (MeteorBolt with Fire01.bmd, LightningBolt with Blast01.bmd, PoisonCloud with Poison01.bmd), per-monster combat effects (Budge fire, Lich lightning, Spider web), level-up effects. Main 5.2 1:1 migration. |
| `SoundManager.cpp` | OpenAL init/shutdown, WAV loader (PCM 16-bit), multi-channel source pooling, 3D positional audio, random pitch variation, MP3 music (minimp3 decode to OpenAL buffer). |
| **Characters & Entities** | |
| `HeroCharacter.cpp` | DK character: 5-part body, skeletal animation, click-to-move, weapon bone attachment (safe zone/combat), blob shadow with stencil buffer, attack state machine with GCD (global cooldown). |
| `MonsterManager.cpp` | Monster rendering, state machine, skeleton weapons (Sword/Shield/Bow/Axe via RetransformMeshWithBones), arrow projectiles (Arrow01.bmd), death debris, nameplates. |
| `NpcManager.cpp` | NPC models, animation, name label overlays. |
| `ClickEffect.cpp` | Click-to-move ring effect. |
| **Items & Inventory** | |
| `ItemDatabase.cpp` | 293 item definitions (addDef calls), all lookup functions, body part mapping, category names. |
| `ItemModelManager.cpp` | BMD item model cache with GPU upload, viewport-based UI rendering, world-space rendering. |
| `GroundItemRenderer.cpp` | Ground item physics (gravity/bounce), zen pile rendering, floating damage update/render, ground item labels/tooltips. |
| `InventoryUI.cpp` | Full inventory/equipment UI: panel rendering with WoW-style design system (gradient backgrounds, double borders, gold-tinted accents), drag-drop state machine, tooltip system, equipment stats, stat allocation, quickbar with styled resource bars. |
| **Networking** | |
| `NetworkClient.cpp` | TCP socket management, packet framing. |
| `ServerConnection.cpp` | Typed packet builders for all client->server messages. |
| `ClientPacketHandler.cpp` | Packet dispatch: initial sync (NPCs, monsters, equipment, stats) + game loop (combat, drops, movement, level-up). |
| **Input & UI** | |
| `InputHandler.cpp` | GLFW callbacks: mouse movement, scroll zoom, left/right click dispatch, keyboard hotkeys (C/I/Q/Esc), `processInput()` for held keys. |
| `RayPicker.cpp` | Ray-terrain intersection (binary search), ray-cylinder NPC/monster picking, ray-sphere ground item picking. |
| `GameUI.cpp` | UI texture and widget helpers. |
| `UITexture.cpp` | UI texture loading. |
| `UIWidget.cpp` | UI widget primitives. |
| `HUD.cpp` | HUD layout helpers. |
| `MockData.cpp` | Mock character data for testing. |
| **Main Executables** | |
| `main.cpp` | Game client: init sequence, render loop (terrain/objects/entities/HUD/overlays), server connection, shutdown. ~1700 lines. |
| `model_viewer_main.cpp` | Object browser: scans Object1/ for BMDs, orbit camera, ImGui. |
| `char_viewer_main.cpp` | Character browser: Player.bmd skeleton + body part armor system. |
| `diag_nearby_objects.cpp` | Diagnostic: lists objects near a terrain coordinate. |

### Server Sources (server/)

| File | Purpose |
|------|---------|
| `server/src/main.cpp` | Server entry point. |
| `server/src/Server.cpp` | TCP accept loop, session management, periodic autosave (60s). |
| `server/src/PacketHandler.cpp` | Server packet routing to handlers. |
| `server/src/Database.cpp` | SQLite database: characters, items, NPCs, monsters, skill/potion bar persistence. |
| `server/src/GameWorld.cpp` | Game world: terrain attributes, safe zones, monster AI state machine, A* pathfinding. |
| `server/src/PathFinder.cpp` | A* pathfinding on 256x256 terrain grid. |
| `server/src/StatCalculator.cpp` | DK stat formulas: HP, damage, defense, XP. |
| `server/src/handlers/CharacterHandler.cpp` | Character creation, stat allocation, save/load, quickslot sync. |
| `server/src/handlers/CombatHandler.cpp` | Attack resolution, skill damage, death/XP, monster aggro/pack assist. |
| `server/src/handlers/InventoryHandler.cpp` | Item pickup, equip/unequip, inventory moves, consumption. |
| `server/src/handlers/WorldHandler.cpp` | Position sync, monster AI, NPC viewport. |
| `server/src/handlers/ShopHandler.cpp` | NPC shop: buy/sell with zen validation, inventory slot management. |

### Shaders (shaders/)

| File | Purpose |
|------|---------|
| `model.vert` | Standard MVP transform, normal correction via inverse-transpose, `texCoordOffset` for UV scroll. |
| `model.frag` | Two-sided diffuse lighting, alpha discard 0.1, fog 1500-3500, `blendMeshLight`, `objectAlpha`, `terrainLight`, `luminosity`, point lights (64 max). **Must set `objectAlpha` to 1.0 or objects are invisible.** |
| `shadow.vert` | Minimal MVP vertex shader for blob shadow (position-only). |
| `shadow.frag` | Flat black at 30% opacity: `vec4(0, 0, 0, 0.3)`. |

## Data Paths

- Game assets: `Data/`
  - `Object1/` -- BMD models + OZT/OZJ textures
  - `Monster/` -- Monster BMD models + textures
  - `World1/` -- Lorencia terrain data
  - `Player/` -- Player body part BMDs
  - `Effect/` -- Effect textures (fire, thunder, etc.)
  - `NPC/` -- NPC BMD models + textures
  - `Item/` -- Item BMD models
  - `Sound/` -- WAV sound effects (1155 files, PCM 16-bit)
  - `Music/` -- MP3 music tracks (72 files)
  - `Skill/` -- Skill effect BMD models + textures
- Server database: `server/build/mu_server.db` (SQLite, auto-created on first run)

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| MAX_BONES | 200 | BMDStructs.hpp |
| TERRAIN_SIZE | 256 | TerrainParser.hpp |
| TERRAIN_SCALE | 100.0f | Terrain.cpp |
| INVENTORY_SLOTS | 64 | ClientTypes.hpp |
| MAX_GROUND_ITEMS | 64 | ClientTypes.hpp |
| MAX_FLOATING_DAMAGE | 32 | GroundItemRenderer.hpp |
| MAX_POINT_LIGHTS | 64 | main.cpp, MonsterManager.hpp |
| Object record size | 30 bytes | TerrainParser.cpp |
| MAX_WORLD_OBJECTS | 160 | reference _enum.h |
| Alpha discard | 0.1 | shaders/model.frag |
| Fog range | 1500-3500 | shaders/model.frag |
| Game tick rate | 40ms (25 FPS) | Original engine ZzzScene.cpp |
| Server port | 44405 | Server.cpp, main.cpp |

## Packet Protocol

Client-server communication uses a simple binary TCP protocol. Each packet starts with a 1-byte type ID.

### Client -> Server

| Type | Name | Payload |
|------|------|---------|
| 0x10 | PrecisePosition | float worldX, float worldZ |
| 0x20 | Attack | uint16_t monsterIndex |
| 0x21 | Pickup | uint16_t dropIndex |
| 0x22 | CharSave | charId, level, stats, XP, quickSlot |
| 0x23 | Equip | charId, slot, category, index, level |
| 0x24 | Unequip | charId, slot |
| 0x25 | StatAlloc | uint8_t statType |
| 0x26 | InventoryMove | uint8_t from, uint8_t to |
| 0x27 | ItemUse | uint8_t slot |
| 0x31 | GridMove | uint8_t gridX, uint8_t gridY |

### Server -> Client

| Type | Name | Payload |
|------|------|---------|
| 0x01 | Welcome | Server identification |
| 0x13 | NPC Viewport | NPC type, position, direction |
| 0x29 | DamageResult | Monster HP update, damage amount |
| 0x2A | MonsterDeath | Monster death + loot drops |
| 0x2F | MonsterAttack | Monster attacks player |
| 0x30 | MonsterRespawn | Monster respawn at new position |
| 0x34 | MonsterSpawn | Initial monster data |
| 0x35 | MonsterMove | Monster position update + chasing flag |
| 0x36 | InventorySync | Full inventory state |
| 0x37 | EquipmentSync | Equipment slots |
| 0x38 | StatSync | Level, stats, XP |
| 0x39 | GroundDrop | Item/zen drop on ground |
