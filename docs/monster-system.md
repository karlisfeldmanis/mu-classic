# Monster System

Reference: ZzzCharacter.cpp, ZzzOpenData.cpp, Monster.txt, MonsterSetBase.txt

## Monster Type Mapping (CreateMonster, ZzzCharacter.cpp:12577)

Server MonsterIndex -> BMD model file. `OpenMonsterModel(N)` loads `Data/Monster/Monster{N+1}.bmd`.

### Lorencia Monsters

| Server Type | Name | BMD Type (N) | BMD File | Scale | Notes |
|-------------|------|-------------|----------|-------|-------|
| 0 | Bull Fighter | 0 | Monster01.bmd | 0.8 | HiddenMesh=0 |
| 1 | Hound | 1 | Monster02.bmd | 0.85 | HiddenMesh=0 |
| 2 | Budge Dragon | 2 | Monster03.bmd | 0.5 | **Flying/hover** |
| 3 | Spider | 9 | Monster10.bmd | 0.4 | |
| 4 | Elite Bull Fighter | 0 | Monster01.bmd | 1.15 | Same model, bigger |
| 5 | Hell Hound | 1 | Monster02.bmd | 1.1 | Same model, bigger |
| 6 | Lich | 4 | Monster05.bmd | 0.85 | Ranged (AttackType=2) |
| 7 | Giant | 5 | Monster06.bmd | 1.6 | |
| 10 | Dark Knight | 3 | Monster04.bmd | 0.8 | |
| 11 | Ghost | 7 | Monster08.bmd | default | Alpha=0.4, MoveSpeed=15 |
| 12 | Larva | 6 | Monster07.bmd | 0.6 | |
| 13 | Hell Spider | 8 | Monster09.bmd | 1.1 | |

### Extended Mapping (non-Lorencia)

| Server Type | Name | BMD Type | Scale |
|-------------|------|----------|-------|
| 14-16 | Skeleton variants | MODEL_PLAYER | 0.95-1.2 |
| 17 | Cyclops | 10 | default |
| 18 | Gorgon | 11 | 1.5, BlendMesh=1 |
| 19 | Yeti | 12 | 1.1 |
| 20 | Stone Golem | 13 | 1.4 |
| 26 | Goblin | 19 | 0.8 |
| 27 | Chain Scorpion | 20 | 1.1 |
| 42 | Red Dragon | special | boss |

## Monster Stats (Monster.txt, Main 5.2)

Column order: Index, Rate, Name, Level, MaxLife, MaxMana, DamageMin, DamageMax, Defense, MagicDefense, AttackRate, DefenseRate, MoveRange, AttackType, AttackRange, ViewRange, MoveSpeed, AttackSpeed, RegenTime, Attribute, ItemRate, MoneyRate, MaxItemLevel, MonsterSkill

### Lorencia Monster Stats

| Type | Name | Level | HP | DMG | Defense | DefRate | AtkRate | MvRange | MvSpeed | AtkSpeed |
|------|------|-------|-----|-----|---------|---------|---------|---------|---------|----------|
| 0 | Bull Fighter | 6 | 100 | 16-20 | 6 | 6 | 28 | 3 | 400 | 1600 |
| 1 | Hound | 9 | 140 | 22-27 | 9 | 9 | 39 | 3 | 400 | 1600 |
| 2 | Budge Dragon | 4 | 60 | 10-13 | 3 | 3 | 18 | 3 | 400 | 2000 |
| 3 | Spider | 2 | 30 | 4-7 | 1 | 1 | 8 | 2 | 400 | 1800 |
| 4 | Elite Bull Fighter | 12 | 190 | 31-36 | 12 | 12 | 52 | 3 | 400 | 1400 |
| 5 | Hell Hound | 38 | 1400 | 125-130 | 55 | 55 | 185 | 3 | 400 | 1200 |

**Note:** `MoveSpeed=400` in Monster.txt is a server-side timer in milliseconds (400ms move tick). The client-side `c->MoveSpeed` is 10 (units per frame at 25fps = 250 units/sec).

## Movement System (ZzzCharacter.cpp:6201)

### MoveCharacterPosition()
```cpp
// Movement vector: (0, -MoveSpeed, 0) rotated by facing angle
Vector(0.f, -CharacterMoveSpeed(c), 0.f, v);
VectorRotate(v, Matrix, Velocity);
VectorAdd(o->Position, Velocity, o->Position);
o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
```

Default `c->MoveSpeed = 10` (all monsters except Ghost=15).
At 25fps: 10 units/tick * 25 ticks/sec = **250 world units/sec** = 2.5 grid cells/sec.

### Client-Side Path Following (MoveMonsterClient, line 6229)
The client receives `TargetX/TargetY` from server network packets and uses:
1. **A* pathfinding** (`PathFinding2`) to compute a path from current to target grid cell
2. **Catmull-Rom spline** (`MovePath`) to interpolate along path waypoints smoothly
3. Arrival threshold: 20 world units

**Monster AI is server-side only.** The client does NOT decide when to chase or wander.

## Budge Dragon Hover (ZzzCharacter.cpp:6222)

```cpp
// Applied in MoveCharacterPosition() — both moving and idle
if (o->Type == MODEL_MONSTER01+2) {
    o->Position[2] += -absf(sinf(o->Timer)) * 70.f + 70.f;
}
o->Timer += 0.15f;  // 0.15 radians per tick
```

- Range: 0 to +70 units above terrain
- Period: 2*pi / (0.15 * 25fps) = ~1.68 seconds per full bob cycle
- Frequency: 3.75 rad/sec
- When dead (line 6285): snaps to terrain height, no hover

## Bounding Boxes (ZzzCharacter.cpp:11397)

Default: Min=(-60,-60,0), Max=(50,50,150)

| Monster Types | BoundingBoxMax |
|---------------|----------------|
| MODEL_PLAYER | (40, 40, 120) |
| +2, +6, +9, +20, +19, +17 (small/medium) | (50, 50, 80) |
| +11, +31, +39, +42, +44 (large/tall) | (70, 70, 250) |
| default (Bull Fighter, Hound, etc.) | **(50, 50, 150)** |

## BodyHeight

`b->BodyHeight = 0.f` for ALL monsters (ZzzCharacter.cpp:6441). Used only for UI name tag positioning, not collision or rendering.

## Attack Alternation

Original uses `SwordCount % 3 == 0` -> ATTACK1, else ATTACK2. From ZzzAI.cpp SetAction.

## Kind System

```
TYPE 0-99:    KIND_MONSTER
TYPE 100-110: KIND_TRAP
TYPE 111-149: KIND_MONSTER
TYPE 150-199: KIND_MONSTER
TYPE 200:     KIND_MONSTER
TYPE 201-259: KIND_NPC
TYPE 260+:    KIND_MONSTER
```

## Server-Side Monster AI (GameWorld.cpp)

Monster AI runs entirely on the server. The client only receives target positions and pathfinds visually.

### AI State Machine

```
IDLE → WANDERING → IDLE (cycle every 2-6 seconds)
IDLE → CHASING → ATTACKING → CHASING (aggro on player)
CHASING → RETURNING → IDLE (leash/de-aggro/path fail)
DYING → DEAD → IDLE (death + respawn after 10s)
```

### State Details

| State | Behavior |
|-------|----------|
| **IDLE** | Wait 2-6s, pick random walkable cell within moveRange, pathfind there |
| **WANDERING** | Walk along path, interrupt if player enters viewRange. Emit target on state entry. |
| **CHASING** | A* pathfind toward aggro target every 1s. Give up after 5 consecutive path failures. |
| **APPROACHING** | Close-range approach when within grid attackRange but outside melee world distance. |
| **ATTACKING** | Deal damage every attackCooldown. Return to CHASING if target leaves attackRange. |
| **RETURNING** | Pathfind back to spawn. Teleport if no path. Restore full HP on arrival. |
| **DYING** | 3s death animation timer. |
| **DEAD** | Wait RESPAWN_DELAY (10s), then respawn at original spawn with full HP. |

### PathFinder (A*)

Server uses `PathFinder::FindPath()` — A* on the 256x256 terrain grid.
- Max path length: 16 steps (monsters re-pathfind periodically for longer chases)
- Max iterations: 500 (prevent stalls)
- Checks terrain walkability (`TW_NOMOVE`) and monster occupancy grid
- Monsters temporarily clear their own cell before pathfinding

### Melee Attack Range
Melee monsters (attackRange <= 1) use an additional Euclidean world-space distance check (`MELEE_ATTACK_DIST_SQ = 150²`) on top of Chebyshev grid distance. This prevents melee monsters from attacking at diagonal adjacency (~141 world units) when they should be closer.

### Leash & Aggro Timer
- Leash distance: `max(20, viewRange * 5)` grid cells from spawn
- Aggro timer (15s) only decays when monster is IDLE, not during active engagement (CHASING/APPROACHING/ATTACKING)

### Position Tracking
- `HandleMove` (D4 packet) contains click-to-move DESTINATION, NOT current position
- `HandlePrecisePosition` (D7 packet) provides actual current player position
- Only D7 updates `session.worldX/worldZ` — using D4 caused monsters to de-aggro immediately

### Pack Assist

When an aggressive monster is attacked, same-type allies within `viewRange` join the chase against the attacker.
- Skips monsters already in an active state (CHASING/ATTACKING/RETURNING).
- **Chase Failure Limit**: Allies will NOT participate if their `chaseFailCount` is >= 5. This prevents monsters from entering an infinite loop of attempting to aggro and immediately giving up due to invalid paths (e.g. across rifts).
- `chaseFailCount` is reset when the monster successfully returns to its spawn point or respawns.

### Chase Failure Count

Monsters track consecutive A* pathfinding failures during a chase.
- If `chaseFailCount` reaches 5, the monster transitions to `RETURNING` state.
- This represents the monster "giving up" when the target is unreachable (e.g. across a chasm or rift in Devias).
- Prevents CPU spikes from repeated pathfinding failures.
- State resets on arrival at spawn or death.

### Wander Emission

When transitioning IDLE→WANDERING, the server emits `MON_MOVE` with the wander target immediately.
Without this emit, the client never sees the monster start walking.

## Dungeon Traps (Types 100-102)

Traps are `KIND_TRAP` entities with special server-side AI and client-side VFX.

### Trap Types

| Type | Name | Model | Attack Range | Behavior |
|------|------|-------|-------------|----------|
| 100 | Lance Trap | Trap01.bmd | 4 (grid cells) | Pressure plate — attacks players in range on cooldown |
| 101 | Iron Stick | Trap02.bmd | 1 | Pressure plate — attacks adjacent players |
| 102 | Fire Trap | FireTrap01.bmd | 4 | Step-on activation — player must stand ON trap tile to trigger AoE |

### Trap Stats (OpenMU Version075)
- All traps: 1.0s attack cooldown, ShieldBypassChance=1.0 (100% shield bypass)
- Lance: HP=1000, Damage=100-110, Defense=400
- Iron Stick: HP=400, Damage=55-65, Defense=100
- Fire: HP=600, Damage=90-100, Defense=200

### Server-Side AI (`processTrapAI` in GameWorld.cpp)
- **Lance/Iron Stick (100-101)**: Standard cooldown loop — scan players within grid `attackRange`, deal damage to all in range.
- **Fire Trap (102)**: Two-step activation — first checks if any player is standing ON the trap's exact grid cell (step-on trigger). Only then does it deal AoE damage to all players within `attackRange`.
- Traps never move, never aggro, never die. They are stationary hazards.

### Client-Side VFX
- **Lance Trap idle**: Continuous rotating `SPELL_LIGHTNING` particles at +150 above position (~8/sec), matching Main 5.2 `BITMAP_LIGHTNING+1` sprites.
- **Lance/Iron Stick attack**: `SOUND_TRAP01` (aGrate.wav) + `SPELL_LIGHTNING` burst (lance only).
- **Fire Trap attack**: `SOUND_FLAME` + `SPELL_FIRE` burst.
- **VFX trigger**: Fires in `TriggerAttackAnimation` (on server attack packet), NOT continuously.
- **Iron Stick animation**: Toggles between action 0 and action 1 on attack.
- **World object HiddenMesh=-2**: Trap world objects (types 39/40/51) are invisible in Dungeon — `ObjectRenderer` skips them.

## Reference Code

- `ZzzCharacter.cpp:CreateMonster()` (line 12577) -- Type-to-model mapping + scale
- `ZzzCharacter.cpp:MoveCharacterPosition()` (line 6201) -- Movement + hover
- `ZzzCharacter.cpp:MoveMonsterClient()` (line 6229) -- A* + Catmull-Rom path following
- `ZzzCharacter.cpp:Setting_Monster()` (line 12477) -- Kind/role assignment
- `ZzzOpenData.cpp:OpenMonsterModel()` (line 2376) -- Model loading + PlaySpeed
- `Monster.txt` -- Stats table
- `MonsterSetBase.txt` -- Spawn positions
- `_define.h:483` -- Action constants
- `server/src/GameWorld.cpp` -- Server-side AI state machine, pathfinding, terrain attributes, trap AI
- `src/PathFinder.cpp` -- A* pathfinding implementation
