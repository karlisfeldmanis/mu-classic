# Death Gorgon Fire Effect - Research Findings

## Main 5.2 Implementation Analysis

### Spawn System
- **Rate**: 10 particles per frame at random bones
- **Frequency**: Every frame (25fps in original = 250 particles/sec)
- **Position**: Direct bone world position via `TransformPosition`
- **No scatter** in spawn code - particles spawn exactly at bone positions

### Particle Configuration
```
Type: BITMAP_FIRE (base fire texture)
Lifetime: 1000 ticks = 40 seconds
SubType: 0 (default)
Scale: 1.0 (default)
Light: Character's current light value
Angle: Character's current angle
```

### Particle Physics (Estimated)
Based on the 40-second lifetime and stationary spawning:
- **Very low velocity** - particles must move slowly to avoid streaks over 40s
- **Possible updraft** - slight upward movement (1-5 units/sec estimated)
- **Minimal horizontal drift** - creates volumetric cloud, not flame tongues

### Terrain Lighting
- Color: RGB(Luminosity × 1.0, Luminosity × 0.2, 0.0) = Red-orange glow
- Radius: 2 (moderate range)
- Applied to PrimaryTerrainLight

### Particle Recycling
When BITMAP_FIRE particles expire, they spawn a new BITMAP_FIRE with SubType 9,
creating a continuous effect without manual respawning.

## Implementation Recommendations

### Current Issues
1. **Lifetime too short** - Using 1.0-1.4s vs original 40s
2. **Spawn rate possibly too high** - 800/sec vs original ~250/sec
3. **Velocity too variable** - Causing streaks instead of volumetric cloud

### Recommended Changes

**1. Increase Particle Lifetime**
```cpp
p.maxLifetime = 10.0f + (float)(rand() % 100) / 10.0f; // 10-20 seconds
```
Much longer lifetime creates persistent cloud like original.

**2. Reduce Spawn Rate**
```cpp
// Spawn every 0.04s (25Hz) with 10 particles = 250/sec (matches original 25fps × 10)
if (mon.ambientVfxTimer >= 0.04f) {
  for (int i = 0; i < 10; ++i) {
```

**3. Minimal Velocity**
```cpp
// Near-zero velocity for volumetric cloud
float speed = 1.0f + (float)(rand() % 2); // 1-2 units/sec
p.velocity = glm::vec3(
  std::cos(angle) * speed,
  2.0f + (float)(rand() % 3),  // 2-4 upward
  std::sin(angle) * speed
);
```

**4. Larger Particles with Lower Alpha**
```cpp
p.scale = 80.0f + (float)(rand() % 100); // 80-180 units
p.alpha = 0.15f + (float)(rand() % 15) / 100.0f; // 0.15-0.30 (very low for long lifetime)
```

**5. No Y-Scatter Below Ground**
```cpp
glm::vec3 scatter(
  (float)(rand() % 40 - 20),  // ±20 X
  (float)(rand() % 20),        // 0-20 Y (upward only)
  (float)(rand() % 40 - 20)    // ±20 Z
);
```

## Key Insight

The original fire is a **persistent volumetric cloud**, not fast-moving flames:
- **Long lifetime** (10-20s) creates density through accumulation
- **Low velocity** (~2-4 units/sec) prevents streaking
- **Moderate spawn rate** (250/sec) builds up over time
- **Low alpha** (0.15-0.30) allows many particles to blend smoothly

The effect relies on **temporal accumulation** rather than spatial density per frame.
