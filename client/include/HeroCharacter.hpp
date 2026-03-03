#ifndef HERO_CHARACTER_HPP
#define HERO_CHARACTER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "ViewerCommon.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct PointLight {
  glm::vec3 position;
  glm::vec3 color;
  float range;
  int objectType = 0; // World object type (for per-type flicker)
};

// Item identity received from server (equipment packet 0x24)
// The server only sends what the character has equipped — the client
// resolves all rendering config (animations, bones, rotation) locally.
struct WeaponEquipInfo {
  uint8_t category = 0xFF; // ItemCategory (0xFF = none equipped)
  uint8_t itemIndex = 0;
  uint8_t itemLevel = 0;
  bool twoHanded = false; // From ItemDatabase — determines animation set
  std::string modelFile;  // e.g. "Sword01.bmd"
};

// Client-side rendering config per weapon category
// Resolved locally from category — never stored in DB or sent over the wire
struct WeaponCategoryRender {
  uint8_t actionIdle; // Player action for combat idle
  uint8_t actionWalk; // Player action for combat walk
  uint8_t attachBone; // Bone index (33=R Hand mount, 42=L Hand mount)
};

// Client-side weapon category rendering config table
// Reference: ZzzCharacter.cpp CreateCharacterPointer(), _enum.h player actions
// Identity rotation (0,0,0) + zero offset — weapon BMD's own bone handles
// orientation
inline const WeaponCategoryRender &GetWeaponCategoryRender(uint8_t category) {
  // Indexed by ItemCategory enum (0=Sword..6=Shield)
  // NOTE: actionIdle/actionWalk here are base values for 1H weapons.
  // HeroCharacter uses weaponIdleAction()/weaponWalkAction() which handle
  // 2H/scythe/dual-wield overrides. attachBone is the primary use of this
  // table.
  static const WeaponCategoryRender table[] = {
      {4, 17, 33},  // SWORD:  PLAYER_STOP_SWORD / PLAYER_WALK_SWORD, R Hand
      {4, 17, 33},  // AXE:    same as sword
      {4, 17, 33},  // MACE:   same as sword
      {6, 19, 33},  // SPEAR:  PLAYER_STOP_SPEAR / PLAYER_WALK_SPEAR, R Hand
      {8, 21, 42},  // BOW:    PLAYER_STOP_BOW / PLAYER_WALK_BOW, L Hand
      {4, 17, 42},  // STAFF:  base=SWORD (WAND only for items 14-20), L Hand
      {4, 17, 42},  // SHIELD: PLAYER_STOP_SWORD / PLAYER_WALK_SWORD, L Hand
  };
  if (category < sizeof(table) / sizeof(table[0]))
    return table[category];
  static const WeaponCategoryRender fallback{1, 15, 33};
  return fallback;
}

// Damage type for colored floating numbers (from WSclient.cpp DamageType)
enum class DamageType {
  NORMAL = 0,    // Orange — standard hit
  EXCELLENT = 2, // Green-teal — 120% of max damage
  CRITICAL = 3,  // Sky blue — max damage + level bonus
  MISS = 7,      // Grey "MISS" text
  INCOMING = 8,  // Red — monster hits hero
};

struct DamageResult {
  int damage;
  DamageType type;
};

enum class AttackState { NONE, APPROACHING, SWINGING, COOLDOWN };
enum class HeroState { ALIVE, HIT_STUN, DYING, DEAD, RESPAWNING };
enum class HellfirePhase { NONE, BEGIN, START, BLAST };

class HeroCharacter {
public:
  // Player action indices (_enum.h)
  static constexpr int ACTION_STOP_MALE = 1;
  static constexpr int ACTION_WALK_MALE = 15;
  static constexpr int ACTION_SKILL_VITALITY = 67;

  void Init(const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadow(const glm::mat4 &view, const glm::mat4 &proj);
  void ProcessMovement(float deltaTime);
  void MoveTo(const glm::vec3 &target);
  void StopMoving();
  void Cleanup();

  // Equipment equipping (called after server sends equipment packet)
  void EquipWeapon(const WeaponEquipInfo &weapon);
  void EquipShield(const WeaponEquipInfo &shield);

  // Body part replacement (0=Helm, 1=Armor, 2=Pants, 3=Gloves, 4=Boots)
  // Pass empty modelFile to revert to default naked part
  void EquipBodyPart(int partIndex, const std::string &modelFile);

  // Combat: attack a monster by index
  void AttackMonster(int monsterIndex, const glm::vec3 &monsterPos);
  void SkillAttackMonster(int monsterIndex, const glm::vec3 &monsterPos,
                          uint8_t skillId);
  void CastSelfAoE(uint8_t skillId, const glm::vec3 &targetPos); // AoE toward target
  void TeleportTo(const glm::vec3 &target);
  void UpdateAttack(float deltaTime);
  bool CheckAttackHit();
  void CancelAttack();
  DamageResult RollAttack(int targetDefense, int targetDefSuccessRate) const;
  int GetAttackTarget() const { return m_attackTargetMonster; }
  AttackState GetAttackState() const { return m_attackState; }
  bool IsAttacking() const { return m_attackState != AttackState::NONE; }
  bool HasRegisteredHit() const { return m_attackHitRegistered; }
  uint8_t GetActiveSkillId() const { return m_activeSkillId; }
  float GetGlobalCooldown() const { return m_globalAttackCooldown; }
  float GetGlobalCooldownMax() const { return m_globalAttackCooldownMax; }
  static int GetSkillAction(uint8_t skillId);
  void SetVFXManager(class VFXManager *vfx) { m_vfxManager = vfx; }
  const std::vector<BoneWorldMatrix> &GetCachedBones() const {
    return m_cachedBones;
  }

  // HP and damage
  void TakeDamage(int damage);
  void ApplyHitReaction();
  void ForceDie();
  int GetHP() const { return m_hp; }
  void SetHP(int hp) { m_hp = hp; }
  int GetMaxHP() const { return m_maxHp; }
  int GetMana() const { return m_mana; }
  int GetMaxMana() const { return m_maxMana; }
  int GetAG() const { return m_ag; }
  int GetMaxAG() const { return m_maxAg; }
  bool IsDead() const {
    return m_heroState == HeroState::DYING || m_heroState == HeroState::DEAD;
  }
  bool ReadyToRespawn() const {
    return m_heroState == HeroState::DEAD && m_stateTimer <= 0.0f;
  }
  HeroState GetHeroState() const { return m_heroState; }
  // Animation and state control
  void SetAction(int newAction);
  void UpdateState(float deltaTime);
  void Respawn(const glm::vec3 &spawnPos);

  // Stats and leveling (MU DK formulas from MuEmu-0.97k)
  void RecalcStats();
  void GainExperience(uint64_t xp);
  // Load stats from server (overrides defaults, calls RecalcStats)
  void LoadStats(int level, uint16_t str, uint16_t dex, uint16_t vit,
                 uint16_t ene, uint64_t experience, int levelUpPoints,
                 int currentHp, int maxHp, int currentMana, int maxMana,
                 int currentAg, int maxAg, uint8_t charClass = 0);
  bool AddStatPoint(int stat); // 0=STR, 1=DEX, 2=VIT, 3=ENE
  int GetLevel() const { return m_level; }
  uint8_t GetClass() const { return m_class; }
  uint64_t GetExperience() const { return m_experience; }
  uint64_t GetNextExperience() const { return m_nextExperience; }
  int GetLevelUpPoints() const { return m_levelUpPoints; }
  uint16_t GetStrength() const { return m_strength; }
  uint16_t GetDexterity() const { return m_dexterity; }
  uint16_t GetVitality() const { return m_vitality; }
  uint16_t GetEnergy() const { return m_energy; }
  int GetDamageMin() const { return m_damageMin; }
  int GetDamageMax() const { return m_damageMax; }
  int GetDefense() const { return m_defense; }
  int GetAttackSuccessRate() const { return m_attackSuccessRate; }
  int GetDefenseSuccessRate() const { return m_defenseSuccessRate; }
  void SetAttackSpeed(int speed) { m_serverAttackSpeed = speed; }
  bool LeveledUpThisFrame() const { return m_leveledUpThisFrame; }
  void ClearLevelUpFlag() { m_leveledUpThisFrame = false; }

  // XP table: cubic curve (from gObjSetExperienceTable, MaxLevel=400)
  static uint64_t CalcXPForLevel(int level);

  // SafeZone state (called from main.cpp each frame)
  void SetInSafeZone(bool safe);
  bool IsInSafeZone() const { return m_inSafeZone; }
  void Heal(int amount);
  bool HasWeapon() const { return m_weaponBmd != nullptr; }
  int weaponIdleAction() const;
  void SetSlowAnimDuration(float d) { m_slowAnimDuration = d; }

  // Ground item pickup logic
  void SetPendingPickup(int dropIndex) { m_pendingPickupIndex = dropIndex; }
  int GetPendingPickup() const { return m_pendingPickupIndex; }
  void ClearPendingPickup() { m_pendingPickupIndex = -1; }

  // Flash delayed damage packet — store on cast, send when beam spawns at frame 7.0
  void SetPendingAquaPacket(uint16_t target, uint8_t skill, float x, float z) {
    m_aquaPacketReady = false;
    m_aquaPacketTarget = target;
    m_aquaPacketSkill = skill;
    m_aquaPacketX = x;
    m_aquaPacketZ = z;
  }
  bool PopPendingAquaPacket(uint16_t &target, uint8_t &skill, float &x, float &z) {
    if (!m_aquaPacketReady) return false;
    target = m_aquaPacketTarget;
    skill = m_aquaPacketSkill;
    x = m_aquaPacketX;
    z = m_aquaPacketZ;
    m_aquaPacketReady = false;
    return true;
  }

  // Equipment stat bonuses (weapon adds damage, armor/shield adds defense)
  void SetWeaponBonus(int dmin, int dmax);
  void SetDefenseBonus(int def);
  int GetWeaponBonusMin() const { return m_weaponDamageMin; }
  int GetWeaponBonusMax() const { return m_weaponDamageMax; }
  int GetDefenseBonus() const { return m_equipDefenseBonus; }

  // Accessors
  glm::vec3 GetPosition() const { return m_pos; }
  void SetPosition(const glm::vec3 &pos) { m_pos = pos; }
  float GetFacing() const { return m_facing; }
  bool IsMoving() const { return m_moving; }
  Shader *GetShader() { return m_shader.get(); }

  // Terrain linkage
  void SetTerrainData(const TerrainData *td) { m_terrainData = td; }
  void SetTerrainLightmap(const std::vector<glm::vec3> &lightmap) {
    m_terrainLightmap = lightmap;
  }
  void SetPointLights(const std::vector<PointLight> &lights) {
    m_pointLights = lights;
  }
  void SetLuminosity(float l) { m_luminosity = l; }

  // Snap hero Y to terrain height
  void SnapToTerrain();

private:
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;

  // Position & movement
  glm::vec3 m_pos{12800.0f, 0.0f, 12800.0f};
  glm::vec3 m_target{12800.0f, 0.0f, 12800.0f};
  float m_facing = 0.0f;
  float m_targetFacing = 0.0f;
  float m_speed = 334.0f;
  bool m_moving = false;

  // Animation
  int m_action = 1; // PLAYER_STOP1 (male idle)
  float m_animFrame = 0.0f;
  static constexpr float ANIM_SPEED = 8.25f;
  int m_rootBone = -1;
  bool m_foot[2] = {false, false}; // Main 5.2: c->Foot[0/1] for walk sound

  // Blending state
  int m_priorAction = -1;
  float m_priorAnimFrame = 0.0f;
  float m_blendAlpha = 1.0f;
  bool m_isBlending = false;
  static constexpr float BLEND_DURATION = 0.12f; // seconds

  // ── Weapon-specific idle/walk actions (_enum.h) ──
  static constexpr int ACTION_STOP_SWORD = 4;
  static constexpr int ACTION_STOP_TWO_HAND_SWORD = 5;
  static constexpr int ACTION_STOP_SPEAR = 6;
  static constexpr int ACTION_STOP_SCYTHE = 7;
  static constexpr int ACTION_STOP_BOW = 8;
  static constexpr int ACTION_STOP_CROSSBOW = 9;
  static constexpr int ACTION_STOP_WAND = 10;

  static constexpr int ACTION_WALK_SWORD = 17;
  static constexpr int ACTION_WALK_TWO_HAND_SWORD = 18;
  static constexpr int ACTION_WALK_SPEAR = 19;
  static constexpr int ACTION_WALK_SCYTHE = 20;
  static constexpr int ACTION_WALK_BOW = 21;
  static constexpr int ACTION_WALK_CROSSBOW = 22;
  static constexpr int ACTION_WALK_WAND = 23;

  // Attack actions (_enum.h)
  static constexpr int ACTION_ATTACK_FIST = 38;
  static constexpr int ACTION_ATTACK_SWORD_R1 = 39;
  static constexpr int ACTION_ATTACK_SWORD_R2 = 40;
  static constexpr int ACTION_ATTACK_SWORD_L1 = 41; // Dual-wield left hand
  static constexpr int ACTION_ATTACK_SWORD_L2 = 42; // Dual-wield left hand
  static constexpr int ACTION_ATTACK_TWO_HAND_SWORD1 = 43;
  static constexpr int ACTION_ATTACK_TWO_HAND_SWORD2 = 44;
  static constexpr int ACTION_ATTACK_TWO_HAND_SWORD3 = 45;
  static constexpr int ACTION_ATTACK_SPEAR1 = 46;
  static constexpr int ACTION_ATTACK_SCYTHE1 = 47;
  static constexpr int ACTION_ATTACK_SCYTHE2 = 48;
  static constexpr int ACTION_ATTACK_SCYTHE3 = 49;
  static constexpr int ACTION_ATTACK_BOW = 50;
  static constexpr int ACTION_ATTACK_CROSSBOW = 51;

  // DK Skill actions (_enum.h)
  static constexpr int ACTION_SKILL_SWORD1 = 60;     // Falling Slash
  static constexpr int ACTION_SKILL_SWORD2 = 61;     // Lunge
  static constexpr int ACTION_SKILL_SWORD3 = 62;     // Uppercut
  static constexpr int ACTION_SKILL_SWORD4 = 63;     // Cyclone
  static constexpr int ACTION_SKILL_SWORD5 = 64;     // Slash
  static constexpr int ACTION_SKILL_WHEEL = 65;      // Twisting Slash
  static constexpr int ACTION_SKILL_FURY = 66;       // Rageful Blow
  static constexpr int ACTION_SKILL_DEATH_STAB = 71; // Death Stab

  // DW Magic skill actions (_enum.h, Main 5.2)
  // Counted from PLAYER_SET=0, verified: PLAYER_ATTACK_SKILL_SWORD1=60 anchor
  static constexpr int ACTION_SKILL_HAND1 = 146;    // Energy Ball
  static constexpr int ACTION_SKILL_HAND2 = 147;    // Generic hand cast
  static constexpr int ACTION_SKILL_WEAPON1 = 148;   // Staff cast 1
  static constexpr int ACTION_SKILL_WEAPON2 = 149;   // Staff cast 2
  static constexpr int ACTION_SKILL_TELEPORT = 151;  // Teleport
  static constexpr int ACTION_SKILL_FLASH = 152;     // Aqua Beam
  static constexpr int ACTION_SKILL_INFERNO = 153;   // Inferno / Evil Spirit
  static constexpr int ACTION_SKILL_HELL = 154;      // Hell Fire
  static constexpr int ACTION_SKILL_HELL_BEGIN = 72;  // Hell Fire (cast begin)
  static constexpr int ACTION_SKILL_HELL_START = 73;  // Hell Fire (charge hold)

  // Hit/death actions (CharViewer: Shock=230, Die1=231, Die2=232)
  static constexpr int ACTION_SHOCK = 230;
  static constexpr int ACTION_DIE1 = 231;

  // ─── DK Stats (MuEmu-0.97k DefaultClassInfo.txt, Class=1) ───
  uint8_t m_class = 16; // Default DK, server overrides via LoadStats
  int m_level = 1;
  uint64_t m_experience = 0;
  uint64_t m_nextExperience = 0;
  int m_levelUpPoints = 0;
  bool m_leveledUpThisFrame = false;

  uint16_t m_strength = 28;  // DK starting STR
  uint16_t m_dexterity = 20; // DK starting DEX (Agility)
  uint16_t m_vitality = 25;  // DK starting VIT
  uint16_t m_energy = 10;    // DK starting ENE

  // DK class constants (from DefaultClassInfo.txt + GameServerInfo)
  static constexpr int DK_BASE_STR = 28, DK_BASE_DEX = 20;
  static constexpr int DK_BASE_VIT = 25, DK_BASE_ENE = 10;
  static constexpr int DK_BASE_HP = 110;
  static constexpr float DK_LEVEL_LIFE = 2.0f;
  static constexpr float DK_VIT_TO_LIFE = 3.0f;
  static constexpr int DK_POINTS_PER_LEVEL = 5;

  // Derived combat stats (recomputed by RecalcStats)
  int m_damageMin = 3;          // STR / 8 + weapon
  int m_damageMax = 7;          // STR / 4 + weapon
  int m_defense = 6;            // DEX / 3 + equipment
  int m_attackSuccessRate = 42; // Level*5 + DEX*3/2 + STR/4
  int m_defenseSuccessRate = 6; // DEX / 3

  // Equipment bonuses (set from inventory equip)
  int m_weaponDamageMin = 0;
  int m_weaponDamageMax = 0;
  int m_equipDefenseBonus = 0;

  // HP system (m_maxHp computed by RecalcStats)
  int m_hp = 110;
  int m_maxHp = 110;
  int m_mana = 25;
  int m_maxMana = 25;
  int m_ag = 50;
  int m_maxAg = 50;
  HeroState m_heroState = HeroState::ALIVE;
  float m_stateTimer = 0.0f;
  static constexpr float HIT_STUN_TIME = 0.4f;
  static constexpr float DEAD_WAIT_TIME = 3.0f;
  float m_hpRemainder = 0.0f; // Track fractional HP for smooth regeneration

  // Combat state
  bool m_inSafeZone = true;
  WeaponEquipInfo m_weaponInfo;

  // VFX manager for skill cast effects
  class VFXManager *m_vfxManager = nullptr;

  // Attack state machine
  AttackState m_attackState = AttackState::NONE;
  int m_attackTargetMonster = -1;
  glm::vec3 m_attackTargetPos{0.0f};
  float m_attackAnimTimer = 0.0f;
  bool m_attackHitRegistered = false;
  int m_swordSwingCount = 0;
  float m_attackCooldown = 0.0f;
  float m_globalAttackCooldown = 0.0f;    // GCD remaining
  float m_globalAttackCooldownMax = 0.0f; // GCD total (for UI progress)
  uint8_t m_activeSkillId = 0;     // Non-zero when using a skill attack
  float m_slowAnimDuration = 0.0f; // >0 = stretch heal anim to this duration
  HellfirePhase m_hellfirePhase = HellfirePhase::NONE; // Multi-phase Hellfire animation

  // Main 5.2: Flash (Aqua Beam) delayed beam spawn — beam at frame 7.0, gathering before
  bool m_pendingAquaBeam = false;   // True during wind-up before beam spawns
  bool m_aquaBeamSpawned = false;   // Set true once beam is fired
  float m_aquaGatherTimer = 0.0f;   // Tick accumulator for gathering particle spawns

  // Delayed skill packet for Flash — damage sent when beam spawns, not on click
  bool m_aquaPacketReady = false;    // True when beam spawns → main loop sends packet
  uint16_t m_aquaPacketTarget = 0;
  uint8_t  m_aquaPacketSkill = 0;
  float    m_aquaPacketX = 0.0f;
  float    m_aquaPacketZ = 0.0f;
  static constexpr float MELEE_ATTACK_RANGE = 150.0f;
  static constexpr float BOW_ATTACK_RANGE = 500.0f;
  static constexpr float MAGIC_ATTACK_RANGE = 500.0f;
  float getAttackRange() const {
    // DW with active spell uses magic range
    if (m_class == 0 && m_activeSkillId > 0)
      return MAGIC_ATTACK_RANGE;
    return (m_weaponInfo.category == 4) ? BOW_ATTACK_RANGE : MELEE_ATTACK_RANGE;
  }
  static constexpr float ATTACK_COOLDOWN_TIME = 0.6f;
  static constexpr float ATTACK_HIT_FRACTION = 0.4f;
  int m_serverAttackSpeed = 0; // From server (DEX/15 for DK)
  // Agility-based attack speed multiplier: 3% faster per attack speed point
  float attackSpeedMultiplier() const {
    return 1.0f + m_serverAttackSpeed * 0.03f;
  }

  // ── Weapon animation selection (Main 5.2 ZzzCharacter.cpp) ──
  // These resolve the correct action based on weapon category, twoHanded flag,
  // and dual-wield state. Used instead of GetWeaponCategoryRender for anim.
  int weaponWalkAction() const;
  int nextAttackAction(); // Returns next attack action, advances swing counter
  bool isDualWielding() const;

  // Skeleton + body parts
  std::unique_ptr<BMDData> m_skeleton;
  static const int PART_COUNT = 5;

  // Shadow rendering
  struct ShadowMesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    int indexCount = 0;
    int vertexCount = 0;
  };

  struct BodyPart {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    std::vector<ShadowMesh> shadowMeshes;
  };
  BodyPart m_parts[PART_COUNT];
  BodyPart m_baseHead; // Base head model (HelmClassXX.bmd) for accessory helms
  bool m_showBaseHead = false; // True when equipped helm needs face visible
  std::unique_ptr<Shader> m_shader;

  // Weapon (attached item model — right hand)
  std::unique_ptr<BMDData> m_weaponBmd;
  std::vector<MeshBuffers> m_weaponMeshBuffers;
  std::vector<ShadowMesh> m_weaponShadowMeshes;
  std::vector<BoneWorldMatrix> m_weaponLocalBones; // Cached static bind-pose
  std::string m_dataPath; // Cached for late weapon loading

  // Weapon glow (Main 5.2: ItemLight / BlendMesh system)
  int m_weaponBlendMesh = -1;  // Mesh index to render additively (-1 = none)
  int m_shieldBlendMesh = -1;  // Shield glow mesh index (-1 = none)

  // Shield (attached item model — left hand)
  WeaponEquipInfo m_shieldInfo;
  std::unique_ptr<BMDData> m_shieldBmd;
  std::vector<MeshBuffers> m_shieldMeshBuffers;
  std::vector<ShadowMesh> m_shieldShadowMeshes;
  std::vector<BoneWorldMatrix> m_shieldLocalBones; // Cached static bind-pose

  // Shadow rendering
  std::unique_ptr<Shader> m_shadowShader;
  std::vector<BoneWorldMatrix> m_cachedBones; // cached from last Render()

  // External data (non-owning)
  int m_pendingPickupIndex = -1;
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
};

#endif // HERO_CHARACTER_HPP
