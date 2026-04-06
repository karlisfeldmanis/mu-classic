# Fire Particle System - Deep Technical Analysis

## Wave 2: Particle Physics & Rendering Discovery

### Critical Finding: BITMAP_FIRE Has NO Built-in Movement

**Key Discovery**: After examining the particle physics code, BITMAP_FIRE particles in Main 5.2:
- Have `Direction` vector set to `(0, 0, 0)` - NO initial velocity
- Do NOT use gravity physics (no `MoveJump` calls)
- Are **completely stationary** after spawn
- Movement comes ONLY from spawn scatter, not particle velocity

### Particle Spawn Analysis

**Death Gorgon spawn code pattern:**
```
for(int i=0; i<10; i++)
{
    Position = TransformBonePosition(randomBone);
    CreateParticle(BITMAP_FIRE, Position, Angle, Light);
}
```

**Critical**: No scatter in the SPAWN code itself. The scatter/spread comes from:
1. Random bone selection (bones are naturally distributed across body)
2. Natural bone animation (bones move as monster animates)
3. Particle texture rendering (billboards create volumetric appearance)

### Particle Lifetime & Recycling

**BITMAP_FIRE lifetime**: 1000 ticks
- At 25fps: 1000 ticks = 40 seconds
- At 60fps equivalent: Still ~40 seconds (tick-based, not frame-based)

**SubType 9 Recycling**: When BITMAP_FIRE expires, it spawns another with SubType 9:
```
case BITMAP_FIRE:
    CreateParticle(BITMAP_FIRE, o->Position, o->Angle, Light, 9, 1.f, o->Owner);
```

This creates an infinite loop - particles respawn themselves, maintaining density.

### Fire Texture Variations

Available fire textures in Data/Effect/:
- `Fire01.OZJ` - 14K - Standard orange fire (current)
- `Fire02.OZJ` - 9.3K - Compact fire
- `Fire03.OZJ` - 13K - Alternative fire
- `Fire04.OZJ` - 9.2K - Small fire
- `Fire05.OZJ` - 12K - Large fire
- `firehik01.OZJ` - 18K - HIK fire variant (higher quality)
- `firehik02.OZJ` - 20K - HIK fire variant 2
- `firehik03.OZJ` - 18K - HIK fire variant 3

**HIK variants** appear to be higher-quality fire textures used in some effects.

### Particle Rendering Details

**Billboard Rendering**: Particles are camera-facing quads with:
- Additive blending (for glow effect)
- Random rotation per particle
- Scale varies over lifetime (shrink/grow)
- Alpha fades over lifetime

**Light contribution**: Fire particles also call `AddTerrainLight`:
```
Light: (Luminosity * 1.0, Luminosity * 0.2, 0.0) = Red-orange
Radius: 2.0 (moderate range)
```

## Implementation Implications

### Problem with Current Implementation

We're adding **movement velocity** to fire particles:
```cpp
// Current (WRONG):
p.velocity = glm::vec3(cos(angle) * speed, upward, sin(angle) * speed);
```

But Main 5.2 fire particles are **STATIONARY**:
```cpp
// Main 5.2 pattern:
Direction = (0, 0, 0);  // No movement!
```

### Why Our Fire Looks Striped

1. **Moving particles create streaks** - upward velocity creates flame tongues
2. **Short lifetime** - particles fade before filling volume
3. **Missing recycling** - particles don't respawn themselves
4. **Wrong density model** - trying to achieve instant density vs. temporal accumulation

### Correct Implementation

**1. Make particles completely stationary:**
```cpp
case ParticleType::FIRE: {
  // NO velocity - particles are stationary
  p.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
  p.scale = 80.0f + (float)(rand() % 100); // 80-180
  p.maxLifetime = 40.0f; // Full 40 seconds
  p.alpha = 0.2f; // Low - many particles will accumulate
  p.color = glm::vec3(0.8f, 0.6f, 0.3f);
  break;
}
```

**2. Rely on spawn scatter for coverage:**
```cpp
// Scatter ONLY in spawn, not in velocity
glm::vec3 scatter(
  (float)(rand() % 60 - 30),  // ±30 X
  (float)(rand() % 40),        // 0-40 Y
  (float)(rand() % 60 - 30)    // ±30 Z
);
```

**3. Use particle recycling (optional):**
- When particle expires, spawn new one at same position
- Creates self-sustaining effect
- Original Main 5.2 pattern

**4. Try HIK fire textures:**
```cpp
m_fireTexture = TextureLoader::LoadOZJ(effectDataPath + "/Effect/firehik01.OZJ");
```

Higher quality textures may look better.

### The Real Secret: Static Particles + Random Bones + Long Lifetime

The smooth fire effect comes from:
1. **Stationary particles** spawned at bone positions
2. **Bones animate naturally** with monster movement
3. **40-second lifetime** allows massive accumulation
4. **Random bone selection** distributes particles across body
5. **Billboard rendering** creates volumetric appearance

Movement comes from **bone animation**, not particle physics!
