#ifndef VFX_MANAGER_HPP
#define VFX_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

enum class ParticleType {
  BLOOD,       // Red spray (Main 5.2: CreateBlood)
  HIT_SPARK,   // White sparks with gravity (Main 5.2: BITMAP_SPARK)
  SMOKE,       // Gray ambient smoke (monsters)
  FIRE,        // Orange fire particle (Budge Dragon breath)
  ENERGY,      // Blue/white energy flash (Lich hand)
  FLARE,       // Bright additive impact flash (Main 5.2: BITMAP_FLASH)
  LEVEL_FLARE, // Main 5.2: BITMAP_FLARE level-up ring (15 joints rising)
  // DK Skill effects
  SKILL_SLASH,   // White-blue slash sparks (Sword1-5)
  SKILL_CYCLONE, // Cyan spinning spark ring (Cyclone/Twisting Slash)
  SKILL_FURY,    // Orange-red ground burst (Rageful Blow)
  SKILL_STAB,    // Dark red piercing sparks (Death Stab)
  // DW Spell effects
  SPELL_ENERGY,    // Blue-white energy burst (Energy Ball)
  SPELL_FIRE,      // Orange-yellow fire burst (Fire Ball, Hellfire)
  SPELL_FLAME,     // Main 5.2: BITMAP_FLAME (Flame ground fire — larger, slower fade)
  SPELL_ICE,       // Cyan-white ice shards (Ice)
  SPELL_LIGHTNING,  // Bright white-blue electric sparks (Lightning)
  SPELL_POISON,    // Green toxic cloud (Poison)
  SPELL_METEOR,    // Dark orange falling sparks (Meteorite)
  SPELL_DARK,      // Purple-black dark energy (Evil Spirit, Twister)
  SPELL_WATER,     // Blue water spray (Aqua Beam)
  SPELL_TELEPORT,  // Bright white flash ring (Teleport)
  // Inferno-specific particles (Main 5.2: BITMAP_SPARK SubType 2 + BITMAP_EXPLOTION)
  INFERNO_SPARK,     // 2x scale, 3x velocity sparks (Main 5.2: SubType 2)
  INFERNO_EXPLOSION, // Animated 4x4 explosion sprite sheet (Explotion01.OZJ)
  INFERNO_FIRE,      // Dedicated inferno fire (inferno.OZJ texture)
  // Main 5.2: BITMAP_ENERGY orb (Thunder01.jpg, full-texture, rotating)
  SPELL_ENERGY_ORB, // Energy Ball core glow — uses Thunder01 texture at full UV
};

class VFXManager {
public:
  void Init(const std::string &effectDataPath);
  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &projection);
  void Cleanup();

  // Spawns a burst of particles at a given world position
  void SpawnBurst(ParticleType type, const glm::vec3 &position, int count = 10);

  // Main 5.2 level-up effect: 15 BITMAP_FLARE joints rising in a ring
  void SpawnLevelUpEffect(const glm::vec3 &position);

  // Update level-up effect center to follow the character
  void UpdateLevelUpCenter(const glm::vec3 &position);

  // Spawn skill cast VFX at hero position (Main 5.2: BITMAP_SHINY+2 sparkle)
  void SpawnSkillCast(uint8_t skillId, const glm::vec3 &heroPos, float facing);

  // Spawn skill impact VFX at monster position (skill-specific particles)
  void SpawnSkillImpact(uint8_t skillId, const glm::vec3 &monsterPos);

  // Spawn a spell projectile traveling from caster to target
  // Main 5.2: BITMAP_ENERGY — billboard with trailing particles + sparks
  void SpawnSpellProjectile(uint8_t skillId, const glm::vec3 &start,
                            const glm::vec3 &target);

  // Spawns a textured ribbon from start heading toward target
  // Main 5.2: two passes per Lich bolt — scale=50 (thick) + scale=10 (thin)
  void SpawnRibbon(const glm::vec3 &start, const glm::vec3 &target, float scale,
                   const glm::vec3 &color, float duration = 0.5f);

  // Main 5.2: AddTerrainLight — spell projectiles emit dynamic point lights
  // Appends active spell lights to the provided vectors (caller merges with world lights)
  void GetActiveSpellLights(std::vector<glm::vec3> &positions,
                            std::vector<glm::vec3> &colors,
                            std::vector<float> &ranges,
                            std::vector<int> &objectTypes) const;

  // Main 5.2: MODEL_SKILL_BLAST — twin sky-strike bolts falling at target
  void SpawnLightningStrike(const glm::vec3 &targetPos);

  // Main 5.2: Meteorite — single fireball falling from sky at target
  void SpawnMeteorStrike(const glm::vec3 &targetPos);
  void SpawnIceStrike(const glm::vec3 &targetPos);

  // Main 5.2: MODEL_POISON — spawn green cloud at target position
  void SpawnPoisonCloud(const glm::vec3 &targetPos);

  // Main 5.2: BITMAP_FLAME SubType 0 — persistent ground fire at target
  void SpawnFlameGround(const glm::vec3 &targetPos);

  // Main 5.2: MODEL_STORM SubType 0 — Twister tornado from caster toward target
  void SpawnTwisterStorm(const glm::vec3 &casterPos, const glm::vec3 &targetDir);

  // Main 5.2: Evil Spirit — 4-directional spirit beams from caster
  void SpawnEvilSpirit(const glm::vec3 &casterPos, float facing);

  // Main 5.2: Hellfire — ground fire ring from caster
  void SpawnHellfire(const glm::vec3 &casterPos);

  // Main 5.2: MODEL_SKILL_INFERNO — ring of 8 fire explosions around caster
  void SpawnInferno(const glm::vec3 &casterPos);

  // Main 5.2: Hellfire blast — 36 radial spirit beams at spell release
  void SpawnHellfireBeams(const glm::vec3 &casterPos);

  // Main 5.2: AT_SKILL_FLASH (Aqua Beam) — 20-segment laser beam from caster forward
  void SpawnAquaBeam(const glm::vec3 &casterPos, float facing);

  // Kill all active aqua beams (called when casting animation ends)
  void KillAquaBeams();

  // Main 5.2: BITMAP_GATHERING SubType 2 — converging particles before Aqua Beam
  // Spawns lightning arcs + water sprites converging toward hand position
  void SpawnAquaGathering(const glm::vec3 &handPos);

  // Called when Hellfire transitions from charge to blast phase
  void EndHellfireCharge();

  // Per-frame hero bone world positions (for bone-attached particles)
  void SetHeroBonePositions(const std::vector<glm::vec3> &positions);

  // Check if a monster should receive StormTime spin from a nearby spirit beam.
  // Returns true (once per beam per monster) if any primary beam is within range.
  bool CheckSpiritBeamHit(uint16_t serverIndex, const glm::vec3 &monPos,
                          float radiusSq = 80.0f * 80.0f);

  bool HasActiveSpiritBeams() const { return !m_spiritBeams.empty(); }

  // Check if a monster should receive StormTime spin from a nearby tornado.
  // Returns true (once per tornado per monster) if any tornado is within range.
  bool CheckTwisterHit(uint16_t serverIndex, const glm::vec3 &monPos,
                       float radiusSq = 80.0f * 80.0f) {
    for (auto &ts : m_twisterStorms) {
      if (ts.affectedMonsters.count(serverIndex)) continue;
      float dx = monPos.x - ts.position.x;
      float dz = monPos.z - ts.position.z;
      if (dx * dx + dz * dz <= radiusSq) {
        ts.affectedMonsters.insert(serverIndex);
        return true;
      }
    }
    return false;
  }

  bool HasActiveTwisters() const { return !m_twisterStorms.empty(); }

  // Terrain height callback (set from main.cpp for ground collision)
  void SetTerrainHeightFunc(std::function<float(float, float)> fn) {
    m_getTerrainHeight = std::move(fn);
  }

private:
  struct Particle {
    ParticleType type;
    glm::vec3 position;
    glm::vec3 velocity;
    float scale;
    float rotation;
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
    float alpha;
    float frame = 0.0f; // 0+ = sprite sheet column, -1 = full texture UV
  };

  // Ribbon segment: one cross-section of the trail (Main 5.2 JOINT Tails)
  struct RibbonSegment {
    glm::vec3 center;
    glm::vec3 right; // Half-width offset in local X (horizontal face)
    glm::vec3 up;    // Half-width offset in local Z (vertical face)
  };

  // Textured ribbon effect (Main 5.2 JOINT with BITMAP_JOINT_THUNDER)
  struct Ribbon {
    glm::vec3 headPos;      // Current head position
    glm::vec3 targetPos;    // Where the ribbon is heading
    float headYaw = 0.0f;   // Current heading yaw (radians)
    float headPitch = 0.0f; // Current heading pitch (radians)
    float scale;            // Half-width (50.0 or 10.0)
    glm::vec3 color;        // RGB tint
    float lifetime;
    float maxLifetime;
    float velocity = 1500.0f;            // World units/second
    float uvScroll = 0.0f;               // UV scroll offset (animated)
    std::vector<RibbonSegment> segments; // Tail trail (newest at [0])
    static constexpr int MAX_SEGMENTS = 50;
  };

  struct InstanceData {
    glm::vec3 worldPos;
    float scale;
    float rotation;
    float frame;
    glm::vec3 color;
    float alpha;
  };

  // Ribbon vertex: position + UV for textured quad strip
  struct RibbonVertex {
    glm::vec3 pos;
    glm::vec2 uv;
  };

  // Main 5.2: BITMAP_MAGIC ground circle (level-up, teleport)
  struct GroundCircle {
    glm::vec3 position;
    float rotation; // Current rotation angle (radians)
    float lifetime;
    float maxLifetime;
    glm::vec3 color;
  };

  // Main 5.2: Level-up orbiting flare effect (ZzzEffectJoint.cpp)
  // 15 BITMAP_FLARE joints, each orbiting with a trail of 20 flat quads.
  static constexpr int LEVEL_UP_MAX_TAILS = 20;
  struct LevelUpSprite {
    float phase;     // Direction[1]: random initial orbit phase
    float riseSpeed; // Direction[2]: random upward speed (world units/tick)
    float height;    // Accumulated height (Position[2])
    int numTails;    // Current number of tail quads (grows to MAX_TAILS)
    glm::vec3 tails[LEVEL_UP_MAX_TAILS]; // Trail positions (newest at [0])
  };
  struct LevelUpEffect {
    glm::vec3 center;  // Character position at spawn
    int lifeTime;      // Remaining ticks (starts at 50, decrements)
    float tickAccum;   // Fractional tick accumulator
    float radius;      // Velocity: orbit radius (40 world units)
    float spriteScale; // Scale: sprite size (40 world units)
    std::vector<LevelUpSprite> sprites; // 15 orbiting BITMAP_FLARE joints
  };

  // Main 5.2: BITMAP_ENERGY spell projectile (Energy Ball, Fire Ball, etc.)
  // Rendered as glowing billboard traveling from caster to target with trail.
  // Direction=(0,-60,0), LifeTime=20, Luminosity=LifeTime*0.2, trail=BITMAP_ENERGY+BITMAP_SPARK+1
  struct SpellProjectile {
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 direction; // Normalized travel direction
    float speed;         // World units/sec (Main 5.2: ~1500)
    float scale;         // Billboard size
    float rotation;      // Current rotation angle (radians)
    float rotSpeed;      // Rotation speed (Main 5.2: 20 deg/tick = 500 deg/sec)
    float lifetime;
    float maxLifetime;
    float trailTimer;    // Accumulator for trail particle spawning
    glm::vec3 color;     // RGB tint
    float alpha;
    uint8_t skillId;     // For matching spell type
    ParticleType trailType; // Particle type for trail
    float yaw = 0.0f;   // Heading toward target (3D model orientation)
    float pitch = 0.0f;  // Vertical angle toward target
  };

  // Main 5.2: MODEL_SKILL_BLAST — falling sky-strike bolt
  struct LightningBolt {
    glm::vec3 position;
    glm::vec3 velocity;
    float scale;
    float rotation;      // Spinning
    float lifetime;
    float maxLifetime;
    bool impacted;
    float impactTimer;
    // Main 5.2: BITMAP_JOINT_ENERGY SubType 5 — 10-segment trailing energy aura
    static constexpr int MAX_TRAIL = 10;
    glm::vec3 trail[MAX_TRAIL];
    int numTrail = 0;
    float trailTimer = 0.0f;
  };

  // Main 5.2: MODEL_FIRE SubType 0 — falling fireball for Meteorite skill
  // Trail = per-tick BITMAP_FIRE billboard particles, no ribbon
  struct MeteorBolt {
    glm::vec3 position;
    glm::vec3 velocity;    // Diagonal fall
    float scale;           // 1.0-1.7
    float lifetime;
    float maxLifetime;     // 40 ticks = 1.6s
    bool impacted;
    float impactTimer;
    float trailTimer = 0.0f;
  };

  // Main 5.2: MODEL_POISON — green cloud at target (BlendMesh=1, LifeTime=40 ticks)
  struct PoisonCloud {
    glm::vec3 position;
    float rotation;      // Spinning (Main 5.2: 20 deg/tick)
    float lifetime;
    float maxLifetime;   // 1.6s (40 ticks @ 25fps)
    float alpha;
    float scale;
  };

  // Main 5.2: BITMAP_FLAME SubType 0 — persistent ground fire at target
  // LifeTime=40 ticks, spawns 6 flame particles/tick, orange terrain light
  struct FlameGround {
    glm::vec3 position;      // Target position (ground level)
    float lifetime;           // Remaining seconds (starts at 1.6f = 40 ticks)
    float maxLifetime;        // 1.6f
    float tickTimer = 0.0f;   // Accumulator for tick-rate spawning (0.04s/tick)
  };

  // Main 5.2: MODEL_STORM SubType 0 — Twister tornado at target
  // LifeTime=59 ticks (~2.36s), BlendMesh=0, Direction=(0,-10,0)
  // UV scroll = -LifeTime*0.1, smoke/lightning/debris per tick
  struct TwisterStorm {
    glm::vec3 position;      // Current position (snapped to terrain each tick)
    glm::vec3 direction;     // Horizontal travel direction (normalized XZ)
    float speed;             // Travel speed (world units/sec)
    float lifetime;           // Remaining seconds (starts at 2.36f = 59 ticks)
    float maxLifetime;        // 2.36f
    float tickTimer = 0.0f;   // Accumulator for tick-rate effects
    float rotation;           // Current Y-axis rotation
    std::set<uint16_t> affectedMonsters; // Monsters already spun by this tornado
  };

  // Main 5.2: Evil Spirit — homing spirit beams spiraling around caster
  // 4 directions × 2 scales (80+20) = 8 beams, BITMAP_JOINT_SPIRIT texture
  // RENDER_TYPE_ALPHA_BLEND_MINUS (subtractive blending)
  // MoveHumming: beams fly outward then curve back toward caster, creating spiral
  struct SpiritBeam {
    glm::vec3 position;     // Current head position
    glm::vec3 angle;        // Current heading (pitch, unused, yaw) in degrees
    glm::vec3 direction;    // Angular drift accumulator (wobble)
    glm::vec3 casterPos;    // Caster origin (beams home back toward this)
    float scale;            // Half-width: 80 (primary) or 20 (secondary)
    float lifetime;
    float maxLifetime;      // 1.96s (49 ticks)
    float trailTimer;
    // Trail: 6 previous positions for ribbon rendering
    static constexpr int MAX_TRAIL = 6;
    glm::vec3 trail[MAX_TRAIL];
    int numTrail = 0;
    std::set<uint16_t> affectedMonsters; // StormTime tracking per beam
  };

  // Main 5.2: Hellfire — BITMAP_JOINT_SPIRIT SubType 6/7 beams
  // SubType 6: MoveHumming toward center + forward movement (curving outward/back)
  // SubType 7: straight up (Position[2]+=Vel) + forward movement (tall columns)
  struct HellfireBeam {
    glm::vec3 position;
    glm::vec3 angle;       // (pitch, unused, yaw) degrees
    glm::vec3 casterPos;   // SubType 6: MoveHumming target (center + 100Y)
    float scale;           // 60.0 (Main 5.2: CreateJoint scale=60)
    float velocity;        // Starts 0, +5/tick, cap 30 (turn rate + speed)
    float lifetime;
    float maxLifetime;     // 0.8s (20 ticks)
    float trailTimer;
    static constexpr int MAX_TRAIL = 5;
    glm::vec3 trail[MAX_TRAIL];
    int numTrail = 0;
    int subType;           // 6=homing outward, 7=straight up + forward
  };

  // Main 5.2: Hellfire ground circle — MODEL_CIRCLE + MODEL_CIRCLE_LIGHT
  // MODEL_CIRCLE: BlendMeshLight = LifeTime*0.1 fade, 45 ticks
  // MODEL_CIRCLE_LIGHT: scrolling UV, stone debris, terrain light, 40 ticks
  struct HellfireEffect {
    glm::vec3 position;     // Ground-level center
    float lifetime;          // 1.8s (45 ticks)
    float maxLifetime;
    float tickTimer = 0.0f;
    float uvScroll = 0.0f;   // MODEL_CIRCLE_LIGHT: BlendMeshTexCoordU scrolling
    int tickCount = 0;       // Tick counter for debris/ring fire timing
    bool chargePhase = true; // True during HELL_BEGIN+HELL_START, false at BLAST
  };

  // Main 5.2: MODEL_SKILL_INFERNO — ring of 8 fire explosions around caster
  // Inferno01.bmd at center, BlendMesh=-2, Scale=0.9
  struct InfernoEffect {
    glm::vec3 position;     // Caster position (center of ring)
    float lifetime;
    float maxLifetime;
    float tickTimer = 0.0f;
    glm::vec3 ringPoints[8]; // 8 explosion positions for per-tick fire
  };

  // Main 5.2: BITMAP_BOSS_LASER SubType 0 — Aqua Beam laser from caster forward
  // 20 billboard sprites (BITMAP_SPARK+1) along a line, LifeTime=20 ticks
  struct AquaBeam {
    glm::vec3 startPosition;  // CalcAddPosition result (offset origin)
    glm::vec3 direction;      // Per-step direction (50 units * facing)
    glm::vec3 light;          // (0.5, 0.7, 1.0) blue tint
    float scale;              // Billboard size (~30 world units)
    float lifetime;           // 0.8s (20 ticks)
    float gatherTimer = 0.0f; // Accumulator for gathering particle spawns
    float sparkTimer = 0.0f;  // Accumulator for per-tick spark trail particles
    static constexpr int NUM_SEGMENTS = 20;
  };

  // Main 5.2: MODEL_LASER SubType 0 — 1-tick dark flash at spirit beam head
  // RENDER_DARK, Scale=1.3, BlendMeshLight = beam's light intensity
  struct LaserFlash {
    glm::vec3 position;
    float yaw;       // Beam heading (degrees)
    float pitch;     // Beam pitch (degrees)
    float light;     // BlendMeshLight intensity
    float lifetime;  // 0.04s (1 tick)
  };

  // Main 5.2: MODEL_ICE SubType 0 — ice crystal at target for Ice spell
  struct IceCrystal {
    glm::vec3 position;
    float scale;         // 0.8
    float alpha;         // 1.0 → fades to 0
    float lifetime;      // 50 ticks = 2.0s
    float maxLifetime;
    bool fadePhase;      // true after growth animation, starts fading
    float smokeTimer;    // Accumulator for smoke particle spawning
  };

  // Main 5.2: MODEL_ICE_SMALL SubType 0 — ice debris with bouncing physics
  struct IceShard {
    glm::vec3 position;
    glm::vec3 velocity;  // Random outward direction (decays 10%/tick)
    float gravity;       // 200-575 units/sec upward (8-23 per tick), decreases by 75/sec
    float angleX;        // Tumble rotation
    float scale;         // 0.8-1.1 (Main 5.2: rand()%4/10 + 0.8)
    float lifetime;      // 1.28-1.88s (32-47 ticks)
    float smokeTimer;
  };

  std::vector<Particle> m_particles;
  std::vector<Ribbon> m_ribbons;
  std::vector<GroundCircle> m_groundCircles;
  std::vector<LevelUpEffect> m_levelUpEffects;
  std::vector<SpellProjectile> m_spellProjectiles;
  std::vector<LightningBolt> m_lightningBolts;
  std::vector<MeteorBolt> m_meteorBolts;
  std::vector<IceCrystal> m_iceCrystals;
  std::vector<IceShard> m_iceShards;
  std::vector<PoisonCloud> m_poisonClouds;
  std::vector<FlameGround> m_flameGrounds;
  std::vector<TwisterStorm> m_twisterStorms;
  std::vector<SpiritBeam> m_spiritBeams;
  std::vector<HellfireBeam> m_hellfireBeams;
  std::vector<HellfireEffect> m_hellfireEffects;
  std::vector<LaserFlash> m_laserFlashes;
  std::vector<AquaBeam> m_aquaBeams;
  std::vector<InfernoEffect> m_infernoEffects;

  // Per-frame hero bone world positions (for bone-attached particles)
  std::vector<glm::vec3> m_heroBoneWorldPositions;

  // Textures
  GLuint m_bloodTexture = 0;
  GLuint m_hitTexture = 0;   // Legacy (Interface/hit.OZT)
  GLuint m_sparkTexture = 0; // Main 5.2: BITMAP_SPARK (Effect/Spark01.OZJ)
  GLuint m_flareTexture = 0; // Main 5.2: BITMAP_FLASH (Effect/flare01.OZJ)
  GLuint m_smokeTexture = 0;
  GLuint m_fireTexture = 0;
  GLuint m_energyTexture = 0;
  GLuint m_lightningTexture = 0; // JointThunder01.OZJ for ribbons
  GLuint m_thunderTexture = 0;   // Main 5.2: BITMAP_ENERGY (Effect/Thunder01.OZJ)
  GLuint m_magicGroundTexture =
      0; // Main 5.2: Magic_Ground2.OZJ (level-up circle)
  GLuint m_ringTexture = 0;         // ring_of_gradation.OZJ (level-up ring)
  GLuint m_bitmapFlareTexture = 0;  // Main 5.2: BITMAP_FLARE (Effect/Flare.OZJ)
  GLuint m_flameTexture = 0;        // Main 5.2: BITMAP_FLAME (Effect/Flame01.OZJ)
  GLuint m_jointSpiritTexture = 0;  // Main 5.2: BITMAP_JOINT_SPIRIT (Effect/JointSpirit01.OZJ)
  GLuint m_spark3Texture = 0;       // Main 5.2: BITMAP_SPARK+1 (Effect/Spark03.OZJ)
  GLuint m_flareBlueTexture = 0;    // Aqua Beam outer glow (Effect/flareBlue.OZJ)
  GLuint m_explosionTexture = 0;   // Main 5.2: BITMAP_EXPLOTION (Explotion01.OZJ, 4x4 sprite)
  GLuint m_infernoFireTexture = 0; // Main 5.2: Inferno fire (inferno.OZJ)
  GLuint m_hellfireCircleTex = 0;   // Main 5.2: Circle01.bmd texture (Skill/magic_a01.OZJ)
  GLuint m_hellfireLightTex = 0;    // Main 5.2: Circle02.bmd texture (Skill/magic_a02.OZJ)

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_lineShader;

  // Fire Ball 3D model (Main 5.2: MODEL_FIRE = Data/Skill/Fire01.bmd)
  std::unique_ptr<BMDData> m_fireBmd;
  std::vector<MeshBuffers> m_fireMeshes;
  std::unique_ptr<Shader> m_modelShader; // model.vert/model.frag for 3D rendering

  // Lightning sky-strike (Main 5.2: MODEL_SKILL_BLAST = Data/Skill/Blast01.bmd)
  std::unique_ptr<BMDData> m_blastBmd;
  std::vector<MeshBuffers> m_blastMeshes;

  // Poison cloud (Main 5.2: MODEL_POISON = Data/Skill/Poison01.bmd)
  std::unique_ptr<BMDData> m_poisonBmd;
  std::vector<MeshBuffers> m_poisonMeshes;

  // Ice crystal (Main 5.2: MODEL_ICE = Data/Skill/Ice01.bmd)
  std::unique_ptr<BMDData> m_iceBmd;
  std::vector<MeshBuffers> m_iceMeshes;

  // Ice shard debris (Main 5.2: MODEL_ICE_SMALL = Data/Skill/Ice02.bmd)
  std::unique_ptr<BMDData> m_iceSmallBmd;
  std::vector<MeshBuffers> m_iceSmallMeshes;

  // Storm tornado (Main 5.2: MODEL_STORM = Data/Skill/Storm01.bmd)
  std::unique_ptr<BMDData> m_stormBmd;
  std::vector<MeshBuffers> m_stormMeshes;

  // Hellfire ground circles (Main 5.2: MODEL_CIRCLE / MODEL_CIRCLE_LIGHT)
  std::unique_ptr<BMDData> m_circleBmd;       // Circle01.bmd
  std::vector<MeshBuffers> m_circleMeshes;
  std::unique_ptr<BMDData> m_circleLightBmd;   // Circle02.bmd
  std::vector<MeshBuffers> m_circleLightMeshes;

  // Inferno ring (Main 5.2: MODEL_SKILL_INFERNO = Data/Skill/Inferno01.bmd)
  std::unique_ptr<BMDData> m_infernoBmd;
  std::vector<MeshBuffers> m_infernoMeshes;

  // Evil Spirit beam head flash (Main 5.2: MODEL_LASER = Data/Skill/Laser01.bmd)
  std::unique_ptr<BMDData> m_laserBmd;
  std::vector<MeshBuffers> m_laserMeshes;

  // Terrain height callback for ground collision
  std::function<float(float, float)> m_getTerrainHeight;

  // Billboard particle buffers
  GLuint m_quadVAO = 0, m_quadVBO = 0, m_quadEBO = 0;
  GLuint m_instanceVBO = 0;

  // Ribbon buffers (pos + uv per vertex)
  GLuint m_ribbonVAO = 0, m_ribbonVBO = 0;
  static constexpr int MAX_RIBBON_VERTS =
      4000; // Increased for Hellfire 36-beam ring + Evil Spirit + ribbons

  static constexpr int MAX_PARTICLES = 8192;

  void initBuffers();
  void updateRibbon(Ribbon &r, float dt);
  void renderRibbons(const glm::mat4 &view, const glm::mat4 &projection);
  void renderGroundCircles(const glm::mat4 &view, const glm::mat4 &projection);
  void renderLevelUpEffects(const glm::mat4 &view, const glm::mat4 &projection);
  void updateSpellProjectiles(float dt);
  void renderSpellProjectiles(const glm::mat4 &view,
                              const glm::mat4 &projection);
  void renderFireModel(const SpellProjectile &p, const glm::mat4 &view,
                       const glm::mat4 &projection);
  void updateLightningBolts(float dt);
  void renderLightningBolts(const glm::mat4 &view, const glm::mat4 &projection);
  void updateMeteorBolts(float dt);
  void renderMeteorBolts(const glm::mat4 &view, const glm::mat4 &projection);
  void updatePoisonClouds(float dt);
  void renderPoisonClouds(const glm::mat4 &view, const glm::mat4 &projection);
  void updateIceCrystals(float dt);
  void renderIceCrystals(const glm::mat4 &view, const glm::mat4 &projection);
  void updateIceShards(float dt);
  void renderIceShards(const glm::mat4 &view, const glm::mat4 &projection);
  void updateFlameGrounds(float dt);
  void renderFlameGrounds(const glm::mat4 &view, const glm::mat4 &projection);
  void updateTwisterStorms(float dt);
  void renderTwisterStorms(const glm::mat4 &view, const glm::mat4 &projection);
  void updateSpiritBeams(float dt);
  void renderSpiritBeams(const glm::mat4 &view, const glm::mat4 &projection);
  void updateHellfireBeams(float dt);
  void renderHellfireBeams(const glm::mat4 &view, const glm::mat4 &projection);
  void updateHellfireEffects(float dt);
  void renderHellfireEffects(const glm::mat4 &view, const glm::mat4 &projection);
  void updateInfernoEffects(float dt);
  void renderInfernoEffects(const glm::mat4 &view, const glm::mat4 &projection);
  void updateLaserFlashes(float dt);
  void renderLaserFlashes(const glm::mat4 &view, const glm::mat4 &projection);
  void updateAquaBeams(float dt);
  void renderAquaBeams(const glm::mat4 &view, const glm::mat4 &projection);
};

#endif // VFX_MANAGER_HPP
