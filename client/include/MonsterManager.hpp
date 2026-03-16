#ifndef MONSTER_MANAGER_HPP
#define MONSTER_MANAGER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "HeroCharacter.hpp" // For PointLight
#include "MeshBuffers.hpp"
#include "PathFinder.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "VFXManager.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ImDrawList;
struct ImFont;

// Client-side monster visual state — driven entirely by server packets.
// The client NEVER decides state transitions on its own (except cosmetic
// animation completion like ATTACKING->previous state).
enum class MonsterState {
  IDLE,      // Standing still, playing idle animation
  WALKING,   // Moving to server-given wander/return target (0x35, chasing=0)
  CHASING,   // Moving to server-given chase target (0x35, chasing=1)
  ATTACKING, // Playing attack animation (triggered by 0x2F monster-attack
             // packet)
  HIT,       // Playing shock/flinch animation (triggered by 0x29 damage-result
             // packet)
  DYING,     // Playing death animation (triggered by 0x2A death packet)
  DEAD       // Corpse fading, waiting for server respawn (0x30)
};

struct ServerMonsterSpawn {
  uint16_t serverIndex; // Unique server-assigned index (from 0x34 packet)
  uint16_t monsterType;
  uint8_t gridX;
  uint8_t gridY;
  uint8_t dir;
  int hp = 30;
  int maxHp = 30;
  uint8_t state = 0;
};

struct MonsterInfo {
  glm::vec3 position;
  float radius;
  float height;
  float bodyOffset;
  std::string name;
  uint16_t type;
  uint16_t serverIndex;
  int level;
  int hp;
  int maxHp;
  int defense;
  int defenseRate;
  MonsterState state;
};

class MonsterManager {
public:
  void InitModels(const std::string &dataPath);
  void AddMonster(uint16_t monsterType, uint8_t gridX, uint8_t gridY,
                  uint8_t dir, uint16_t serverIndex = 0, int hp = 30,
                  int maxHp = 30, uint8_t state = 0);

  void Update(float deltaTime);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadows(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram);
  void SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx);
  void RenderSilhouetteOutline(int monsterIndex, const glm::mat4 &view,
                               const glm::mat4 &proj);
  void RenderNameplates(ImDrawList *dl, ImFont *font, const glm::mat4 &view,
                        const glm::mat4 &proj, int winW, int winH,
                        const glm::vec3 &camPos, int hoveredMonster,
                        int attackTarget, int playerLevel = 1);
  void Cleanup();
  void ClearMonsters();

  int GetMonsterCount() const { return (int)m_monsters.size(); }
  MonsterInfo GetMonsterInfo(int index) const;
  int CalcXPReward(int monsterIndex, int playerLevel) const;
  uint16_t GetServerIndex(int index) const;
  int FindByServerIndex(uint16_t serverIndex) const;

  // ── Server-driven state updates (called from packet handlers in main.cpp) ──

  // 0x29: Server damage result — update HP, play hit animation
  void SetMonsterHP(int index, int hp, int maxHp);
  void TriggerHitAnimation(int index);

  // 0x2A: Server death notification — play die animation
  void SetMonsterDying(int index);

  // 0x2F: Monster attacks player — play attack animation
  void TriggerAttackAnimation(int index);

  // 0x30: Server respawn — reset monster at new position
  void RespawnMonster(int index, uint8_t gridX, uint8_t gridY, int hp);

  // 0x35: Server movement — set target position for smooth interpolation
  void SetMonsterServerPosition(int index, float worldX, float worldZ,
                                bool chasing);

  // Arrow projectile VFX (Main 5.2: CreateArrow)
  void SpawnArrow(const glm::vec3 &from, const glm::vec3 &to,
                  float speed = 1500.0f);

  // Player position for cosmetic facing during CHASING/ATTACKING states
  void SetPlayerPosition(const glm::vec3 &pos) { m_playerPos = pos; }
  void SetPlayerDead(bool dead) { m_playerDead = dead; }

  // Main 5.2: PushingCharacter — apply StormTime spin stun (Twister hit)
  void ApplyStormTime(uint16_t serverIndex, int ticks = 10);

  // ── External data linkage ──

  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lm) {
    m_terrainLightmap = lm;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetMapId(int mapId) { m_mapId = mapId; }
  void SetVFXManager(VFXManager *vfx) { m_vfxManager = vfx; }

private:
  // Weapon attached to a monster bone (Main 5.2: c->Weapon[n])
  struct WeaponDef {
    BMDData *bmd = nullptr; // Weapon BMD (owned by m_ownedBmds)
    std::string texDir;     // Texture directory for weapon meshes
    int attachBone = 33;    // Player.bmd bone index (33=R Hand, 42=L Hand)
    glm::vec3 rot{0};       // Local rotation (degrees) — Main 5.2 AngleMatrix
    glm::vec3 offset{0}; // Local offset in bone space — Main 5.2 Matrix[i][3]
    std::vector<BoneWorldMatrix> cachedLocalBones; // Static bind-pose bones (computed once)
  };

  struct MonsterModel {
    std::string name;
    std::string texDir; // Texture directory for this model's meshes
    BMDData *bmd;       // Mesh source (& animation for normal monsters)
    BMDData *animBmd =
        nullptr; // Animation/bone source (skeleton monsters: Player.bmd)
    float scale = 1.0f;
    float collisionRadius = 60.0f;
    float collisionHeight = 80.0f;
    float bodyOffset = 0.0f;
    int rootBone = -1; // Index of root bone (Parent == -1) for LockPositions
    std::vector<MeshBuffers> meshBuffers;
    // Stats from Monster.txt (used for display/UI only — combat is server-side)
    int level = 1;
    int defense = 0;
    int defenseRate = 0;
    int attackRate = 0;
    // Action mapping: monster actions (0-6) → BMD action indices
    // Default: identity for normal monsters; overridden for skeleton types
    int actionMap[7] = {0, 1, 2, 3, 4, 5, 6};
    // Weapons (skeleton types: sword, shield, bow attached to Player.bmd bones)
    std::vector<WeaponDef> weaponDefs;
    // BlendMesh: mesh with this TextureId renders additive (Main 5.2 BlendMesh)
    // -1 = disabled. Lich: 0, Skeleton: 0.
    int blendMesh = -1;
    // Per-type alpha: <1.0 for semi-transparent monsters (Main 5.2: Ghost=0.4)
    float typeAlpha = 1.0f;
    // HiddenMesh: mesh index to hide (-1=none). Main 5.2: HiddenMesh=N hides mesh N.
    int hiddenMesh = -1;
    // AABB from mesh upload — actual model bounding box for nameplate positioning
    AABB meshBounds{};

    // Helper: get the BMD to use for bone/animation computation
    BMDData *getAnimBmd() const { return animBmd ? animBmd : bmd; }
  };

  struct MonsterInstance {
    int modelIdx;
    glm::vec3 position;
    glm::vec3 spawnPosition;
    float facing;
    float animFrame = 0.0f;
    int action = 0;
    float scale = 1.0f;
    uint16_t monsterType = 0;
    uint16_t serverIndex = 0; // Server-assigned unique index
    std::string name;

    MonsterState state = MonsterState::IDLE;
    float stateTimer = 0.0f;
    float bobTimer = 0.0f; // Budge Dragon hover timer (ZzzCharacter.cpp:6224)

    int hp = 30;
    int maxHp = 30;
    float corpseTimer = 0.0f;
    float corpseAlpha = 1.0f;
    float spawnAlpha = 1.0f; // Fade-in on spawn (0→1)
    int swordCount = 0;      // Attack alternation counter (ATTACK1/ATTACK2)
    float instanceScale =
        0.0f; // Per-instance scale override (0=use model default)
    bool deathSmokeDone = false;  // Giant death smoke burst (one-shot)
    float ambientVfxTimer = 0.0f; // Throttle for ambient VFX (smoke, etc.)

    // Main 5.2: StormTime — spinning stun from Twister hit
    // Angle[2] += StormTime*10 each tick, StormTime-- (10→0 over 0.4s)
    int stormTime = 0;
    float stormTickTimer = 0.0f;

    // Blending state (cross-fade on stop)
    int priorAction = -1;
    float priorAnimFrame = 0.0f;
    float blendAlpha = 1.0f;
    bool isBlending = false;
    static constexpr float BLEND_DURATION = 0.12f;

    // Server-driven position target (from 0x35 packet)
    glm::vec3 serverTargetPos{0.0f};
    bool serverChasing = false;

    // A* path following with Catmull-Rom spline smoothing
    std::vector<glm::vec3> splinePoints; // World-space waypoints from A* path
    float splineT = 0.0f;               // Parametric position along spline
    float splineRate = 0.0f;            // Pre-computed t advance per second


    std::vector<MeshBuffers> meshBuffers;
    // Per-weapon mesh buffers (parallel to model.weaponDefs)
    struct WeaponMeshSet {
      std::vector<MeshBuffers> meshBuffers;
    };
    std::vector<WeaponMeshSet> weaponMeshes;
    struct ShadowMesh {
      bgfx::DynamicVertexBufferHandle vbo = BGFX_INVALID_HANDLE;
      int vertexCount = 0;
    };
    std::vector<ShadowMesh> shadowMeshes;
    // Per-weapon shadow meshes (parallel to weaponMeshes)
    struct WeaponShadowSet {
      std::vector<ShadowMesh> meshes;
    };
    std::vector<WeaponShadowSet> weaponShadowMeshes;
    std::vector<BoneWorldMatrix> cachedBones;
  };

  struct DebrisInstance {
    int modelIdx;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    glm::vec3 rotVelocity;
    float scale;
    float lifetime;
  };

  // Arrow projectile (Main 5.2: CreateArrow → MODEL_ARROW)
  struct ArrowProjectile {
    glm::vec3 position;
    glm::vec3 direction; // Normalized XZ direction
    float speed;         // World units/sec (~1500)
    float pitch;         // Pitch angle in radians (increases with gravity)
    float yaw;           // Heading yaw in radians
    float scale;
    float lifetime; // Seconds remaining (30 ticks / 25fps = 1.2s)
  };

  std::vector<std::unique_ptr<BMDData>> m_ownedBmds;
  std::vector<MonsterModel> m_models;
  std::vector<MonsterInstance> m_monsters;
  std::vector<DebrisInstance> m_debris;
  std::vector<ArrowProjectile> m_arrows;

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_shadowShader;
  std::unique_ptr<Shader> m_outlineShader;
  VFXManager *m_vfxManager = nullptr;

  // Shadow map state
  bgfx::TextureHandle m_shadowMapTex = BGFX_INVALID_HANDLE;
  glm::mat4 m_lightMtx{1.0f};

  std::string m_monsterTexPath;
  std::string m_dataPath; // Root data path for loading Player.bmd etc.

  // Player.bmd for skeleton monster animations (types 14, 15, 16)
  std::unique_ptr<BMDData> m_playerBmd;
  PathFinder m_pathFinder; // A* pathfinder for client-side movement
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
  int m_mapId = 0; // 0=Lorencia, 1=Dungeon
  float m_worldTime = 0.0f; // Accumulated time for UV scroll effects

  std::unordered_map<uint16_t, int> m_typeToModel;
  bool m_modelsLoaded = false;

  // ── Monster action constants (_define.h: MONSTER01_*) ──
  static constexpr int ACTION_STOP1 = 0;
  static constexpr int ACTION_STOP2 = 1;
  static constexpr int ACTION_WALK = 2;
  static constexpr int ACTION_ATTACK1 = 3;
  static constexpr int ACTION_ATTACK2 = 4;
  static constexpr int ACTION_SHOCK = 5;
  static constexpr int ACTION_DIE = 6;

  // ── Animation speeds (PlaySpeed * 25fps, from ZzzOpenData.cpp) ──
  static constexpr float SPEED_STOP = 6.25f;   // 0.25 * 25
  static constexpr float SPEED_WALK = 8.5f;    // 0.34 * 25
  static constexpr float SPEED_ATTACK = 8.25f; // 0.33 * 25
  static constexpr float SPEED_SHOCK = 12.5f;  // 0.50 * 25
  static constexpr float SPEED_DIE = 13.75f;   // 0.55 * 25
  float getAnimSpeed(uint16_t monsterType, int action) const;

  // ── Client-side visual constants ──
  static constexpr float CORPSE_FADE_TIME = 3.0f;
  static constexpr float CHASE_SPEED =
      250.0f; // Match server grid speed (100 units / 0.4s moveDelay)
  static constexpr float WANDER_SPEED =
      220.0f; // Slightly slower than chase for relaxed feel (server: 250)

  // Player position for cosmetic facing (not used for AI — that's server-side)
  glm::vec3 m_playerPos{0.0f};
  bool m_playerDead = false;

  int m_boneModelIdx = -1;
  int m_stoneModelIdx = -1;
  int m_arrowModelIdx = -1;

  int loadMonsterModel(const std::string &bmdFile, const std::string &name,
                       float scale, float radius, float height,
                       float bodyOffset = 0.0f,
                       const std::string &texDirOverride = "");
  float snapToTerrain(float worldX, float worldZ);
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;
  void updateStateMachine(MonsterInstance &mon, float dt);

  void spawnDebris(int modelIdx, const glm::vec3 &pos, int count);
  void updateDebris(float dt);
  void renderDebris(const glm::mat4 &view, const glm::mat4 &projection,
                    const glm::vec3 &camPos);
  void updateArrows(float dt);
  void renderArrows(const glm::mat4 &view, const glm::mat4 &projection,
                    const glm::vec3 &camPos);
  void setAction(MonsterInstance &mon, int action);
};

#endif // MONSTER_MANAGER_HPP
