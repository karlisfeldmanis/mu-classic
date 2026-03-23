# MU Online Remaster - Reference Library

Native C++20 restoration of the MU Online engine for macOS with OpenGL 3.3+.
Reference source: Main 5.2. We are **migrating, not innovating** -- stick to the original source as the source of truth.
Server-side balance reference: **OpenMU Version075** (`references/OpenMU/`) -- use for monster stats, spawn data, item definitions, and combat formulas.

## Project Structure

```
mu_remaster/
├── client/           # Game client (standalone, distributable to players)
│   ├── src/          # Client source code
│   ├── include/      # Client headers
│   ├── shaders/      # GLSL shaders
│   ├── external/     # Third-party libs (imgui, stb)
│   ├── Data/         # Game assets (Main 5.2, local copy, gitignored)
│   ├── CMakeLists.txt
│   └── build/        # Build output (CMake symlinks Data/ here)
├── server/           # Game server (standalone, zero client code dependencies)
│   ├── src/          # Server source code
│   ├── include/      # Server headers
│   ├── CMakeLists.txt
│   └── build/        # Build output + mu_server.db
├── docs/             # Reference documentation
├── references/       # Original source (git-ignored)
└── CLAUDE.md
```

## Build

```bash
# Client
cd client/build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(sysctl -n hw.ncpu)

# Server
cd server/build && cmake .. && make -j$(sysctl -n hw.ncpu)
```

**Always use Release builds** for the client (`-DCMAKE_BUILD_TYPE=Release`) — Debug builds have significant performance issues.

## Architecture Overview

Client uses modular namespace/class design with non-owning pointer context structs for cross-module communication:

- **main.cpp** (~1700 lines) -- Orchestrator: init, render loop, shutdown
- **ItemDatabase** -- 293 static item definitions, all lookup functions
- **ItemModelManager** -- 3D item model cache, UI + world rendering
- **InventoryUI** -- Inventory/equipment panels, drag-drop, tooltips, stat allocation
- **InputHandler** -- GLFW callbacks, mouse/keyboard dispatch
- **RayPicker** -- Mouse ray-cast: terrain, NPC, monster, ground item picking
- **GroundItemRenderer** -- Ground drop physics/models/labels, floating damage numbers
- **ClientPacketHandler** -- Incoming packet dispatch (initial sync + game loop)
- **ServerConnection** -- Typed send API (SendAttack, SendPickup, SendEquip, etc.)
- **VFXManager** -- Particle bursts, ribbon/lightning trails, fire/meteor effects (Main 5.2 1:1)
- **MonsterManager** -- Monster rendering, weapons, arrow projectiles, debris, nameplates
- **HeroCharacter** -- Player model, weapon attachment (safe zone back / combat hand), stat formulas

Server uses handler-based architecture with SQLite persistence:
- **PacketHandler** routes to **CharacterHandler**, **CombatHandler**, **InventoryHandler**, **WorldHandler**, **ShopHandler**
- **Database** manages SQLite (characters, items, NPCs, monsters)
- **GameWorld** manages terrain attributes, safe zones, monster AI
- **StatCalculator** implements DK stat formulas

## Documentation Index

Detailed reference docs are in `docs/`:

| Document | Content |
|----------|---------|
| [docs/build-and-project.md](docs/build-and-project.md) | Build system, source file index, architecture, packet protocol, data paths |
| [docs/bmd-format.md](docs/bmd-format.md) | BMD binary model format spec, encryption, byte layout |
| [docs/texture-formats.md](docs/texture-formats.md) | OZJ/OZT formats, texture resolution priority, script flags |
| [docs/animation-system.md](docs/animation-system.md) | Tick-based timing, PlaySpeed tables (monsters + players), world object animation |
| [docs/monster-system.md](docs/monster-system.md) | Monster type mapping, stats, movement, hover, bounding boxes, AI architecture |
| [docs/character-system.md](docs/character-system.md) | DK stats, Player.bmd action indices, weapon config, combat formulas |
| [docs/rendering.md](docs/rendering.md) | Coordinate system, blend states, shadows, world objects, EncTerrain.obj format |
| [docs/terrain-and-environment.md](docs/terrain-and-environment.md) | Terrain, water, lightmap, point lights, BlendMesh, fire, grass, luminosity |
| [docs/ui-system.md](docs/ui-system.md) | Diablo HUD (resource orbs), expanded quickbar, map name display, notifications |
| [docs/reference-navigation.md](docs/reference-navigation.md) | Key functions and files in original Main 5.2 source |

## Database

**Single source of truth: `server/build/mu_server.db`** (auto-created on first server run via `SeedItemDefinitions()` + `SeedNpcSpawns()`). Delete to reset/re-seed. All item definitions, NPC spawns, and character data live here. There is NO other database — any `.db` files found elsewhere (e.g. `server/mu_server.db`, `server/build/mu.db`) are stale leftovers and should be deleted. Schema: `characters`, `character_inventory`, `character_equipment`, `character_skills`, `item_definitions`, `npc_spawns`.

**Classes**: Only DW (classCode=0), DK (16), ELF (32), MG (48) are supported. No other classes.

## AG (Ability Gauge) System

DK does NOT use Mana — uses AG (Ability Gauge / Stamina) for skill costs.
- **Formula**: `MaxAG = 1.0*ENE + 0.3*VIT + 0.2*DEX + 0.15*STR` (OpenMU ClassDarkKnight.cs)
- Server repurposes mana packet fields for AG when class is DK
- Client shows "AG" label instead of "Mana" in HUD and character panel
- AG recovers at 5% of max per second (server-side tick in GameWorld)

## Skill System

Skills are server-authoritative. Stored in `character_skills` table. DK starts with 0 skills, learns all via skill orbs (category 12 items). Packet: `SKILL_LIST` (0x41, C2 variable-length).

### DK 0.97d Skills

| Skill | ID | AG Cost | Level Req | Orb Index | Description |
|-------|----|---------|-----------|-----------|-------------|
| Falling Slash | 19 | 9 | 1 | 20 | Downward slash |
| Lunge | 20 | 9 | 1 | 21 | Forward thrust |
| Uppercut | 21 | 8 | 1 | 22 | Upward strike |
| Cyclone | 22 | 9 | 1 | 23 | Spinning attack |
| Slash | 23 | 10 | 1 | 24 | Horizontal slash |
| Twisting Slash | 41 | 10 | 30 | 7 | AoE spinning slash |
| Rageful Blow | 42 | 20 | 170 | 12 | Powerful ground strike |
| Death Stab | 43 | 12 | 160 | 19 | Piercing stab |

### Skill Icons

`Data/Interface/Skill.OZJ` — 512x512 sprite sheet, 25 icons/row, each 20x28px. Icon index = skill ID.

## Critical Rules

1. **objectAlpha uniform**: Any renderer using `model.frag` MUST set `objectAlpha` to 1.0 or objects will be invisible.
2. **GLEW before GLFW**: `GL/glew.h` must be included before `GLFW/glfw3.h`.
3. **glBufferSubData for macOS**: Use `glBufferSubData` for dynamic VBO updates, not `glBufferData` + VAO re-setup.
4. **No invented systems**: Do not add rendering/gameplay systems that don't exist in the original source. For example, Lorencia water is just an animated tile -- no water overlay.
5. **Tick-based engine**: Original runs at 25fps (40ms per tick). Animation and movement are per-tick, not delta-time scaled. PlaySpeed and MoveSpeed are per-tick values.
6. **Monster AI is server-side**: Client receives TargetX/TargetY and pathfinds there. Client does NOT decide when to chase or wander.
7. **BodyHeight = 0 for ALL monsters**: No body offset. Hover (Budge Dragon only) is separate.
8. **Module pattern**: New subsystems use `namespace Foo { Init(ctx); ... }` or `class Foo { static Init(); ... }` with context structs for shared state. No global "GameContext" object.

## Lessons Learned

- Do not invent rendering systems that don't exist in the original (water overlay, custom effects).
- The `_enum.h` enum values ARE the BMD action indices. Do NOT add offsets.
- Terrain dynamic lights use CPU-side lightmap grid modification, NOT per-pixel shader.
- RENDER_WAVE is ONLY for MODEL_MONSTER01+51, never for world objects.
- Terrain tile 255 = empty/invalid, fill with neutral dark brown (80,70,55) not magenta.
- Per-pixel point lights with large ranges create reddish spots -- use CPU-side lightmap instead.
- Item definitions use `defIndex = category * 32 + itemIndex` as the unique key across all systems.
- Client inventory is authoritative from server -- client sends requests, server validates and sends back results.
- **Weapon attachment**: `parentMat = charBone[attachBone] * offset`, then `wFinalBones[i] = parentMat * weaponLocalBone[i]`, then re-skin vertices. Weapon mesh upload MUST use `isDynamic=true` or `RetransformMeshWithBones` is a no-op.
- **BlendMesh in Main 5.2**: `BlendMesh=N` means mesh with `Texture==N` renders additive, other meshes render normally. It is NOT "render only mesh N". Arrow uses BlendMesh=1 (mesh 1 = fire glow additive, mesh 0 = shaft normal).
- **Skill model textures**: Models in `Data/Skill/` reference textures in that same directory. When loading via `loadMonsterModel`, pass `texDirOverride` pointing to `Data/Skill/` or textures won't resolve.
- **Bone positions are model-local**: `cachedBones` from `ComputeBoneMatrices` are in BMD-local space. To get world positions, apply model rotation matrix: `rotate(-90°Z) * rotate(-90°Y) * rotate(facing)` then scale + translate.
- **Giant (type 7) has no blood**: Main 5.2 excludes MODEL_MONSTER01+7 from blood particle spawning.
- **NPC direction values**: DB stores OpenMU-style 1-8 (West=1, SouthWest=2, South=3, SouthEast=4, East=5, NE=6, North=7, NW=8). Main 5.2 MonsterSetBase uses raw protocol 0-7 (add +1 for our DB). Client formula: `facing = (dir-1) * PI/4`.
- **NPC coordinates are version-stable**: Lorencia NPC positions (grid x/y) are identical across Version075, 095d, Season 6, and Main 5.2. Season 6 inherits from 095d which inherits from 075.
- **Player.bmd version matters**: Must use Main 5.2 Player.bmd (247 actions, 3.0 MB), NOT Kayito 0.97k (141 actions, 1.47 MB). Our code uses Main 5.2 `_enum.h` action indices which map to wrong animations in Kayito's version. Guards also use Player.bmd skeleton.
- **Data directory**: `client/Data/` is the canonical data directory (local copy, gitignored). CMake creates `build/Data` symlinks in both client and server build dirs pointing to `client/Data/`. All assets from Main 5.2, Player.bmd has 247 actions.
- **Server terrain path**: CMake symlinks `server/build/Data/` → `client/Data/`, so server loads `Data/World1/EncTerrain1.att` directly.
- **Safe zone attribute**: Only `TW_SAFEZONE` (0x01). Do NOT include `TW_NOGROUND` (0x08) in safe zone checks — that flag is for bridge/void cells.
- **Monster pathfinding chase fail**: Monsters that fail pathfinding 5+ times transition to RETURNING. Pack assist must skip these monsters (chaseFailCount >= 5) or they enter an infinite aggro-give up-re-aggro loop. Reset chaseFailCount on return to spawn and respawn.
- **Autosave optimization**: Skip empty equipment slots (category 0xFF) during autosave. Don't call SaveSession on every kill — rely on periodic 60s autosave.
- **Guard NPC type 249**: Guards don't have shops. ShopHandler silently ignores non-shop NPC types.
- **Body part BMDs have 1 action**: Helm/Armor/Pant/Glove/Boot BMDs (e.g. `HelmMale01.bmd`) have 56 bones and exactly 1 action with 1 keyframe (a static bind pose). Player.bmd has 60 bones and 284 actions. For UI rendering (inventory/shop), use Player.bmd action 1 (idle) bones to get a natural standing pose. Body part vertex bone indices 0-55 are a subset of Player.bmd's 0-59.
- **Helm BMD textures are `head_XX.jpg`**: Helm meshes use texture names like `head_02.jpg`, `head_03.jpg`. When filtering body part meshes for UI display (to hide character skin), do NOT filter `head_` for category 7 (helms) -- that IS the actual helm. Only filter `skin_` and `hide` for helms. Filter all three (`head_`, `skin_`, `hide`) for categories 8-11.
- **Shadow render order**: Shadows with `glDisable(GL_DEPTH_TEST)` MUST render BEFORE the character model, not after, or they appear on top of the character instead of on the ground.
- **Stencil shadow merging**: Use `GL_INCR` (not `GL_REPLACE`) in `glStencilOp` for unified body+weapon+shield shadows. `GL_REPLACE` with ref=0 replaces stencil with 0, never blocking subsequent fragments.
- **Character select point lights**: Collect from world object instances (types 50/51=fire, 52=bonfire, 55=gate, 90=streetlight, etc.). Pass to terrain via CPU lightmap and to character shader via uniform arrays.
- **Click-to-move state guard**: GLFW mouse callbacks fire regardless of game state. Guard with `!s_gameReady` to prevent character select button clicks from bleeding through as click-to-move commands during state transition.
- **D4 MOVE packet = destination, not position**: `HandleMove` receives the click-to-move TARGET coordinates. Only `HandlePrecisePosition` (D7) provides actual current player position. Using D4 to update `session.worldX/worldZ` causes monster AI to think player teleported, breaking aggro.
- **Melee attack range**: Chebyshev grid distance is too coarse for melee (diagonal adjacency = ~141 world units). Layer Euclidean world-space check (`MELEE_ATTACK_DIST_SQ = 150²`) on top.
- **VFX model separation**: Each spell VFX type (meteor, lightning, poison, ice) should have its own struct, vector, update, and render functions. Do NOT mix them into shared structs with `isMeteor`/`isLightning` flags — this creates hard-to-maintain branching.
- **Main 5.2 Meteorite trail**: NOT a ribbon trail. Trail is per-tick BITMAP_FIRE billboard sprite particles spawned at the fireball's position. They fade, shrink, and rotate individually. The fireball itself is Fire01.bmd 3D model falling diagonally (Direction(0,0,-50) rotated by Angle(0,20,0)).
- **Mount ride models**: Uniria=Rider01.bmd (Skill/), Dinorant=Rider02.bmd (Skill/). NOT the item models (Helper03/04.bmd in Player/). Render scale: 1.0 in-game (Main 5.2 RenderMount overrides creation scale 0.9 to 1.0).
- **Rider02.bmd bone 0 is STATIC**: Bone 0 = `Box01` (helper marker), bone 1 = `Bip01` (animated root). Root motion removal must use bone 1 for Dinorant. Detect by finding first bone named "Bip01".
- **Mount animation sync**: Player.bmd ride walk and mount walk have matching 7-key patterns. Sync mount animFrame to player animFrame via `fmod()` to prevent Z oscillation phase drift (~25-36 unit bounce).
- **Dinorant player height**: +30 on normal maps, +90 on fly maps (ZzzCharacter.cpp:6263). Mount position = player position - offset (mount at terrain level).
- **Mount safe zone**: Mount stays loaded (active=true), alpha fades to 0. `isMountRiding()` = `active && !inSafeZone`. Player uses ground animations in safe zone.
- **Pet/mount mutual exclusion**: Slot 8 holds either pet (idx 0-1) or mount (idx 2-3). Equipping one unequips the other.
- **Rageful Blow (skill 42) vs Strike of Destruction (skill 232)**: DIFFERENT skills! RB uses MODEL_SKILL_FURY_STRIKE (EarthQuake01-08.bmd) — radial ground cracks with RED light. Strike of Destruction uses MODEL_BLOW_OF_DESTRUCTION (knight_plancrack models) — directional line cracks with BLUE light. Don't confuse them.
- **Ground item rotation order**: Main 5.2 `AngleMatrix()` uses ZYX rotation order (Z first, then Y, then X). `RenderItemWorld` must apply rotations in this order, not XYZ. Default item scale is 0.8 (from `ItemObjectAttribute`), not 1.8.
- **Walkability must check TW_NOGROUND**: Client `RayPicker::IsWalkable()` and `PathFinder` must check `(attr & (TW_NOMOVE | TW_NOGROUND)) == 0`. Missing `TW_NOGROUND` (0x08) allows walking into void/cliff cells. Server already checks both.
- **AoE spell sounds — no duplicate on hit**: AoE spells (Evil Spirit, Twister, Hellfire) play cast sound client-side only. Server damage response must NOT replay cast sound — with N monsters hit, this plays N duplicate sounds causing audio looping.
