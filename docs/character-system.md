# Character System

Reference: ZzzCharacter.cpp, ZzzOpenData.cpp, _enum.h

## DK Naked Character (HeroCharacter)

5-part body (Helm, Armor, Pants, Gloves, Boots) skinned against `Player.bmd` skeleton (60 bones, 0 meshes, 284 actions).

### Player.bmd Action Indices (from _enum.h)

Enum values map **1:1 to BMD action indices**. No offset needed. `MAX_PLAYER_ACTION = 284`.

| Range | Actions |
|-------|---------|
| 1-14 | Idle (Stop Male/Female/weapon variants, Fly, Ride) |
| 15-24 | Walk (Male/Female/weapon variants, Swim) |
| 25-37 | Run (weapon variants, Fly, Ride) |
| 38-59 | Combat (Fist=38, Sword R1=39, R2=40, Bow, Ride attacks) |
| 60-66 | DK Sword Skills (SKILL_SWORD1-5, Wheel, Fury Strike) |
| 67 | Heal (SKILL_VITALITY) |
| 70-71 | Spear/DeathStab |
| 146-154 | Magic Skills (Energy Ball, Teleport, Flash, Inferno, Hell Fire) |
| 186-221 | Emotes (Defense, Greeting, Gesture, Cry, Respect, etc.) |
| 230 | Shock (hit stun) |
| 231-232 | Die |
| 233-240 | Sit/Healing/Pose |

**Lesson learned**: `YDG_ADD_SKILL_RIDING_ANIMATIONS` ifdef is never defined -- those 6 extra entries do NOT exist in the Player.bmd.

### Player Animation Speeds (ZzzOpenData.cpp:329)

| Action | PlaySpeed | Keys/sec |
|--------|-----------|----------|
| Idle (STOP_MALE) | 0.28 | 7.0 |
| Walk (WALK_MALE) | 0.30 | 7.5 |
| Combat (attacks) | 0.32 | 8.0 |
| Shock | 0.40 | 10.0 |
| Die | 0.45 | 11.25 |

### Weapon-Specific Idle/Walk Actions

| Category | Idle Action | Walk Action | Attach Bone |
|----------|------------|------------|-------------|
| Sword | 4 (STOP_SWORD) | 17 (WALK_SWORD) | 33 (R Hand) |
| Axe | 4 | 17 | 33 |
| Mace | 4 | 17 | 33 |
| Spear | 6 (STOP_SPEAR) | 19 (WALK_SPEAR) | 33 |
| Bow | 8 (STOP_BOW) | 20 (WALK_BOW) | 42 (L Hand) |
| Staff | 10 (STOP_WAND) | 22 (WALK_WAND) | 42 |
| Shield | 4 | 17 | 42 |

### Body Part BMD Structure

Body part models (Helm, Armor, Pants, Gloves, Boots) live in `Data/Player/`. Each has 56 bones (subset of Player.bmd's 60), 1 action, 1 keyframe.

| Part | Default (DK) | Example Equipped | Texture Naming |
|------|-------------|-----------------|----------------|
| Helm | HelmClass02.bmd | HelmMale01.bmd | `head_02.jpg` (helm mesh), `hide.jpg` |
| Armor | ArmorClass02.bmd | ArmorMale01.bmd | `upper_02_m.jpg` (armor), `skin_barbarian_01.jpg`, `hide.jpg` |
| Pants | PantClass02.bmd | PantMale01.bmd | `lower_02_m.jpg` (pants), `hide_m.jpg` |
| Gloves | GloveClass02.bmd | — | `skin_wizard_01.jpg` (single mesh) |
| Boots | BootClass02.bmd | — | `skin_wizard_01.jpg` (single mesh) |

**Texture naming convention for UI filtering**:
- `skin_*` = character body skin mesh (hide in inventory/shop)
- `hide*` = invisible placeholder mesh (always hide)
- `head_*` = head/helm mesh. For helms (cat 7) this IS the item; for other body parts it's the character head (hide in UI)
- `upper_*`, `lower_*` = actual armor/pants mesh (always show)

**Pose**: Body part BMDs' single action is a static bind pose. For natural display, use Player.bmd action 1 (PLAYER_STOP_MALE) bone matrices instead.

## DK Stats (MuEmu-0.97k DefaultClassInfo.txt)

| Stat | Starting Value |
|------|---------------|
| STR | 28 |
| DEX | 20 |
| VIT | 25 |
| ENE | 10 |
| Base HP | 110 |
| HP per Level | 2.0 |
| HP per VIT | 3.0 |
| Points per Level | 5 |

### Derived Combat Formulas (OpenMU Version075)

```
DamageMin = STR / 6 + weaponDamageMin
DamageMax = STR / 4 + weaponDamageMax
Defense = DEX / 3 + equipmentDefense
AttackSuccessRate = Level * 5 + DEX * 3/2 + STR / 4
DefenseSuccessRate = DEX / 3
MaxHP = BaseHP + Level * LevelLife + (VIT - BaseVIT) * VITtoLife
MaxAG = 1.0*ENE + 0.3*VIT + 0.2*DEX + 0.15*STR
```

### AG (Ability Gauge)

DK uses AG instead of Mana. AG is the resource for all DK skills.
- Recovery: 5% of MaxAG per second (server-side)
- Server repurposes mana/maxMana packet fields for AG values

### DK Skills (0.97d scope)

| Skill | ID | AG Cost | Level Req | Damage Bonus |
|-------|----|---------|-----------|-------------|
| Falling Slash | 19 | 9 | 1 | +15 |
| Lunge | 20 | 9 | 1 | +15 |
| Uppercut | 21 | 8 | 1 | +15 |
| Cyclone | 22 | 9 | 1 | +18 |
| Slash | 23 | 10 | 1 | +20 |
| Twisting Slash | 41 | 10 | 30 | +25 |
| Rageful Blow | 42 | 20 | 170 | +60 |
| Death Stab | 43 | 12 | 160 | +70 |

All DK skills are learned from orbs (category 12 items). DK starts with 0 skills.

### XP Formula (OpenMU Version075)

`XP = (targetLevel + 25) * targetLevel / 3.0 * 1.25` with level scaling.

### Item Enhancement Table (OpenMU)

Weapon/Armor damage/defense per level: `{0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 31, 36}`
Shield defense per level: `+1/level`

## Combat State Machine & Global Cooldown

Attack state: `NONE → APPROACHING → SWINGING → COOLDOWN → NONE`

### Global Cooldown (GCD)
- Set when `SWINGING` starts: `GCD = animationDuration + cooldownTime`
- Persists even if player cancels the attack via click-to-move
- Prevents the attack cancel exploit (click-to-move → immediate recast)
- `GetGlobalCooldown()` / `GetGlobalCooldownMax()` — used by UI for cooldown overlay

### Attack Cancel Rules
- **Before hit frame**: Movement clicks are blocked (player must commit to the swing)
- **After hit frame**: Player can cancel via click-to-move, but GCD prevents immediate recast
- `HasRegisteredHit()` — returns true after the hit has been applied

### Cooldown Indicator
- HUD quickslot skill icons show a dark sweep overlay proportional to remaining GCD
- Rendered via ImDrawList in InventoryUI

## Pet Companions (Guardian Angel / Imp)

Category 13 items equipped in slot 8 (Pet/Helper slot). Floating orbit companions that follow the player with Main 5.2 GOBoid.cpp direction-vector movement.

| Item | Index | Model | Bonus |
|------|-------|-------|-------|
| Guardian Angel | 0 | Helper01.bmd | +50 Max HP, 20% Damage Reduction |
| Imp | 1 | Helper02.bmd | 30% Attack Damage Increase |

- **Tick-based AI**: Idle wandering (smooth random orbit, radius 100, 1.5s cooldown after owner stops), follow mode (lateral weave, ramp-up delay, max distance 180)
- **Sparkle VFX**: `PET_SPARKLE` (white dots) for Angel, `IMP_SPARKLE` (dark red embers) for Imp
- **Two-pass rendering**: Body meshes normal alpha, wing meshes additive blend
- **Body exclusion**: Soft spring pushes pet away from character body
- **Fade-in**: Exponential smoothing alpha from 0→1 on equip

## Mount System (Uniria / Dinorant)

Category 13 items equipped in slot 8. Player rides the mount model; character uses riding animations.

| Item | Index | Ride Model | Bonus |
|------|-------|------------|-------|
| Horn of Uniria | 2 | Rider01.bmd (Skill/) | None |
| Horn of Dinorant | 3 | Rider02.bmd (Skill/) | 15% Attack, 10% Damage Reduction |

### Ride Model Details

| Property | Rider01 (Uniria) | Rider02 (Dinorant) |
|----------|-------------------|---------------------|
| Bones | 24 (bone 0 = Bip01, animated) | 73 (bone 0 = Box01 static, bone 1 = Bip01 animated) |
| Actions | 4 (idle/walk/attack) | 8 (ground + fly pairs) |
| Textures | Item/ (unicon.jpg) | Skill/ (RDgon.jpg, RDgonW.tga) |
| Player height offset | 0 | +30 (normal maps) |
| Render scale | 1.0 (Main 5.2 RenderMount override) | 1.0 |

### Mount Action Mapping (Ground Maps)

| State | Uniria | Dinorant |
|-------|--------|----------|
| Idle | 0 | 0 |
| Walk | 2 | 2 |
| Attack | 3 | 4 |
| Skill | — | 6 |

### Player Ride Actions (_enum.h)

| Action | Index | Use |
|--------|-------|-----|
| STOP_RIDE | 13 | Idle on mount, no weapon |
| STOP_RIDE_WEAPON | 14 | Idle on mount, with weapon |
| RUN_RIDE | 36 | Walk on mount, no weapon |
| RUN_RIDE_WEAPON | 37 | Walk on mount, with weapon |
| ATTACK_RIDE_SWORD | 54 | Sword attack while riding |
| ATTACK_RIDE_TWO_HAND_SWORD | 55 | 2H sword attack |
| ATTACK_RIDE_SPEAR | 56 | Spear attack |
| ATTACK_RIDE_SCYTHE | 57 | Scythe attack |
| ATTACK_RIDE_BOW | 58 | Bow attack |
| ATTACK_RIDE_CROSSBOW | 59 | Crossbow attack |

### Mount Technical Notes

- **`isMountRiding()`**: Returns `m_mount.active && !m_inSafeZone`. Controls ride animation selection.
- **Root bone detection**: `EquipMount` finds first bone named "Bip01" for root motion removal. Rider01 = bone 0, Rider02 = bone 1.
- **Root motion**: Horizontal (XY) always stripped when mounted; vertical (Z) bounce preserved.
- **Animation sync**: Mount animation frame synced to player ride frame via `fmod(m_animFrame, mountWrapKeys)` to prevent Z oscillation phase drift.
- **Safe zone**: Mount stays loaded (`m_mount.active=true`), alpha fades to 0. Player uses ground animations. Auto-restores alpha when leaving.
- **Ride PlaySpeeds** (Main 5.2): idle 0.28 (7.0fps), walk 0.30 (7.5fps). Mount velocity: 0.34 (8.5fps).
- **No shadow**: Main 5.2 suppresses player shadow when riding.
- **No shock**: Main 5.2 suppresses hit stagger animation when riding.

### Mutual Exclusion

Pet and mount are mutually exclusive in slot 8:
- Equipping a mount calls `UnequipPet()` first
- Equipping a pet calls `UnequipMount()` first

## Item Enhancement Glow (Chrome Passes)

Enhanced items (+7 and above) render additive chrome/metal glow passes over the base model. Each pass uses an environment-map texture with normals-derived UVs.

### Pass Structure by Enhancement Level

| Level | Passes | Textures |
|-------|--------|----------|
| +7 | 1: CHROME | Chrome01.OZJ |
| +9 | 2: CHROME + METAL | Chrome01.OZJ + Shiny01.OZJ |
| +11 | 3: CHROME2 + METAL + CHROME | Chrome02.OZJ + Shiny01.OZJ + Chrome01.OZJ |
| +13 | 3: CHROME4 + METAL + CHROME | Chrome02.OZJ (dynamic) + Shiny01.OZJ + Chrome01.OZJ |

### Color Functions

Two color lookup functions determine glow tint per item type:

- **`GetPartObjectColor(category, itemIndex)`** — Used for CHROME and METAL passes. 43-color palette, default warm gold `(1.0, 0.5, 0.0)`. Per-item overrides for all armor (cat 7-11), weapon (cat 0-5), and shield (cat 6) types.

- **`GetPartObjectColor2(category, itemIndex)`** — Used for CHROME2 and CHROME4 passes only. 4-color modulator:

| Color | RGB | Effect |
|-------|-----|--------|
| 0 (default) | (1, 1, 1) | Neutral (keep original) |
| 1 | (1, 0.5, 0) | Warm orange |
| 2 | (0, 0.5, 1) | Cyan/blue |
| 3 | (1, 1, 1) | Pure white |

Armor overrides: items 4, 14, 15, 17 → orange; 18, 43 → cyan; 21, 44 → white.
Weapon overrides: Sword 14, Bow 5/13, Staff 5 → cyan.

### Render State

All glow passes: `glBlendFunc(GL_ONE, GL_ONE)` additive, `glDepthMask(GL_FALSE)`, face culling disabled.

## Weapon Blur Trail

- **WeaponTrail**: 30-point tip/base position history captured during attack animations
- Two blur modes: `blur01.OZJ` for regular attacks (level-based color), `motion_blur_r.OZJ` for skills (white)
- Auto-stops on swing end or attack cancel, fades out over 0.3s

### Trail Colors

| Weapon Level | Color | RGB |
|---|---|---|
| +7+ | Orange-gold | (1.0, 0.6, 0.2) |
| +5 to +6 | Blue | (0.2, 0.4, 1.0) |
| +3 to +4 | Red | (1.0, 0.2, 0.2) |
| Below +3 | Gray/white | (0.8, 0.8, 0.8) |

**Per-weapon overrides** (always red regardless of level): Sword idx 13, Mace idx 6, Spear idx 9.

## Sit / Pose System

Interactive world objects (type 6=Tree07, 133=PoseBox, 145/146=Furniture) support sit/pose animations:
- `ACTION_SIT1` (233), `ACTION_SIT2` (234), `ACTION_POSE1` (239)
- Walk-to-then-interact if too far; cancel on any world click

## Movement

- Click-to-move: `MoveTo(target)` sets destination
- `ProcessMovement(dt)` interpolates position with terrain height tracking
- Player MoveSpeed: base 10, run 12-15, mounts 15 (units per tick)
- At 25fps: 250-375 world units/sec

## Reference Code

- `ZzzCharacter.cpp:CreateCharacterPointer()` -- Weapon rendering config
- `ZzzCharacter.cpp:6263` -- Dinorant rider height offset (+30/+90)
- `GOBoid.cpp:495-596` -- Mount MoveMount action mapping
- `GOBoid.cpp:66-132` -- CreateMountSub (scale, blendMesh)
- `ZzzOpenData.cpp:329-451` -- Player PlaySpeed table
- `_enum.h` -- Action index enum (PLAYER_SET=0)
- `DefaultClassInfo.txt` -- Class starting stats
