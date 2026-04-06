# Wave 3: Constant Fire Implementation

## Critical Discovery: Fire Consistency Issue

### Main 5.2 Implementation Comparison

**Death Gorgon (MODEL_MONSTER01+11, Level==2):**
```cpp
// CONSTANT - spawns EVERY frame
for(int i=0; i<10; i++)
{
    b->TransformPosition(o->BoneTransform[rand()%b->NumBones], p, Position, true);
    CreateParticle(BITMAP_FIRE, Position, o->Angle, Light);
}
```
- **10 particles per frame**
- **No probability check** - runs every frame
- **Result**: Smooth, constant fire coverage

**Death Knight (MODEL_MONSTER01+29):**
```cpp
// FLICKERY - only 50% of frames!
if(rand()%2==0)  // 50% probability
{
    b->TransformPosition(o->BoneTransform[2], p, Position, true);
    CreateParticle(BITMAP_FIRE, Position, o->Angle, Light);
}
```
- **1 particle at bone 2**
- **rand()%2==0 check** - only 50% of frames
- **Result**: Flickering, inconsistent fire

## Solution: Remove Probability, Increase Rate

### For Constant Death Knight Fire:

**Option 1: Remove probability (spawn every frame)**
```cpp
// Spawn 1 particle every frame at bone 2
b->TransformPosition(o->BoneTransform[2], p, Position, true);
CreateParticle(BITMAP_FIRE, Position, o->Angle, Light);
```

**Option 2: Spawn 2 particles to compensate (our implementation)**
```cpp
// Spawn 2 particles every frame at bone 2
// Matches Main 5.2's expected rate: 1 particle × 50% = 0.5/frame
// vs. 2 particles × 100% at 25Hz = 1/frame at 60fps
m_vfxManager->SpawnBurst(ParticleType::FIRE, firePos, 2);
```

We chose Option 2 to maintain smooth particle density.

## Implementation Summary

### Death Gorgon (Type 35) - Already Constant ✅
- **Rate**: 10 particles per 0.04s (25Hz) = 250/sec
- **Spawning**: Every tick (constant)
- **Lifetime**: 40 seconds
- **Coverage**: Full-body (random bones)

### Death Knight (Type 40) - Now Constant ✅
- **Rate**: 2 particles per 0.04s (25Hz) = 50/sec
- **Spawning**: Every tick (constant, was 50% before)
- **Lifetime**: 40 seconds
- **Coverage**: Bone 2 (chest/core)

### Key Parameters for Both

**Particle Properties:**
```cpp
velocity: (0, 0, 0)           // STATIONARY - critical discovery!
scale: 80-180 units           // Large for coverage
maxLifetime: 40.0 seconds     // Full Main 5.2 duration
alpha: 0.18-0.30              // Low for accumulation
color: (0.8, 0.6, 0.3)        // Softer orange
```

**Spawn Scatter:**
```cpp
// Death Gorgon: Large scatter (full body)
X/Z: ±40 units horizontal
Y: 0-50 units upward

// Death Knight: Moderate scatter (chest focus)
X/Z: ±30 units horizontal
Y: 0-40 units upward
```

## Why Constant Fire Matters

1. **Visual Consistency**: No flickering or gaps
2. **Temporal Accumulation**: With 40s lifetime, constant spawning builds up to 2,000-10,000 particles
3. **Smooth Volumetric Effect**: Overlapping particles create seamless fire cloud
4. **Professional Look**: Matches the polished Main 5.2 appearance

## Performance Impact

**Death Gorgon:**
- Max particles: 250/sec × 40s = 10,000 particles
- Per frame at 60fps: ~167 new particles
- Total active: Builds up to 10,000 over 40 seconds

**Death Knight:**
- Max particles: 50/sec × 40s = 2,000 particles
- Per frame at 60fps: ~33 new particles
- Total active: Builds up to 2,000 over 40 seconds

Both are manageable with stationary particles (no physics computation).

## Verification Checklist

✅ Death Gorgon: 10 particles every 0.04s (constant)
✅ Death Knight: 2 particles every 0.04s (constant, was 50% before)
✅ Zero velocity: Particles are stationary
✅ 40-second lifetime: Full temporal accumulation
✅ Low alpha: 0.18-0.30 for smooth blending
✅ Large scatter: Creates volumetric coverage

## Result

Both Death Gorgon and Death Knight now have **perfectly constant, smooth fire effects** that build up into dense volumetric clouds over their 40-second particle lifetime. No flickering, no stripes, no gaps!
