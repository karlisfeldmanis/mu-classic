#ifndef HERO_CHARACTER_HPP
#define HERO_CHARACTER_HPP

#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Shader.hpp"
#include "TerrainParser.hpp"
#include "ViewerCommon.hpp"
#include <bgfx/bgfx.h>
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
  uint8_t quantity = 0;    // Stack count (arrows/bolts)
  bool twoHanded = false;  // From ItemDatabase — determines animation set
  std::string modelFile;   // e.g. "Sword01.bmd"
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
  static constexpr int ACTION_STOP_FEMALE = 2; // Elf idle (Main 5.2: PLAYER_STOP_FEMALE)
  static constexpr int ACTION_WALK_MALE = 15;
  static constexpr int ACTION_WALK_FEMALE = 16; // Elf walk (Main 5.2: PLAYER_WALK_FEMALE)
  static constexpr int ACTION_SKILL_VITALITY = 67;

  void Init(const std::string &dataPath);
  void Render(const glm::mat4 &view, const glm::mat4 &proj,
              const glm::vec3 &camPos, float deltaTime);
  void RenderShadow(const glm::mat4 &view, const glm::mat4 &proj);
  void RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram);
  void SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx);
  void ProcessMovement(float deltaTime);
  void MoveTo(const glm::vec3 &target);
  void StopMoving();
  void Cleanup();

  // Equipment equipping (called after server sends equipment packet)
  void EquipWeapon(const WeaponEquipInfo &weapon);
  void EquipShield(const WeaponEquipInfo &shield);

  // Wings (category 12, items 0-6 — attached to back bone 47)
  void EquipWings(const WeaponEquipInfo &wing);
  void UnequipWings();

  // Pet companion (Guardian Angel / Imp — floating orbit)
  void EquipPet(uint8_t itemIndex);
  void UnequipPet();
  void EquipMount(uint8_t itemIndex);
  void UnequipMount();
  bool IsMounted() const { return m_mount.active; }
  bool HasMountEquipped() const { return m_mountEquippedIndex == 2 || m_mountEquippedIndex == 3; }
  uint8_t GetMountItemIndex() const { return m_mountEquippedIndex; }
  void SetMountIndex(uint8_t idx) { m_mountEquippedIndex = idx; }
  bool IsPetActive() const { return m_pet.active; }
  uint8_t GetPetItemIndex() const { return m_pet.itemIndex; }
  // Mount is visually active and riding (allowed everywhere including safe zone)
  bool isMountRiding() const { return m_mount.active; }

  // Body part replacement (0=Helm, 1=Armor, 2=Pants, 3=Gloves, 4=Boots)
  // Pass empty modelFile to revert to default naked part
  // level = item enhancement level (0-15), used for +7/+9/+11 glow
  // itemIndex = item index within category, used for per-item glow color
  void EquipBodyPart(int partIndex, const std::string &modelFile,
                     uint8_t level = 0, uint8_t itemIndex = 0);

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
  bool IsGCDFromSkill() const { return m_gcdFromSkill; }
  void ClearGlobalCooldown() { m_globalAttackCooldown = 0.0f; }
  float GetTeleportCooldown() const { return m_teleportCooldown; }
  float GetTeleportCooldownMax() const { return TELEPORT_COOLDOWN_TIME; }
  void SetTeleportCooldown() { m_teleportCooldown = TELEPORT_COOLDOWN_TIME; }
  void TickTeleportCooldown(float dt) { if (m_teleportCooldown > 0.0f) m_teleportCooldown -= dt; }
  static int GetSkillAction(uint8_t skillId);
  void SetVFXManager(class VFXManager *vfx) { m_vfxManager = vfx; }
  const std::vector<BoneWorldMatrix> &GetCachedBones() const {
    return m_cachedBones;
  }

  // Get world-space position of a bone (for spell spawn points etc.)
  glm::vec3 GetBoneWorldPosition(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= (int)m_cachedBones.size())
      return m_pos + glm::vec3(0, 120, 0); // Fallback: chest height
    // Extract local bone position from bone matrix column 3
    const auto &bm = m_cachedBones[boneIdx];
    glm::vec3 bmd(bm[0][3], bm[1][3], bm[2][3]);
    // BMD→world: same transform as weapon trail (HeroCharacter.cpp:1078-1081)
    // rotZ(facing) → rotY(-90) → rotZ(-90): (x,y,z) → (ry, z, rx)
    float cf = cosf(m_facing), sf = sinf(m_facing);
    float rx = bmd.x * cf - bmd.y * sf;
    float ry = bmd.x * sf + bmd.y * cf;
    return m_pos + glm::vec3(ry, bmd.z, rx);
  }

  // Weapon blur trail (Main 5.2: BlurType 1 for DK skills 19-23)
  bool IsWeaponTrailActive() const { return m_weaponTrailActive; }
  bool HasValidTrailPoints() const { return m_weaponTrailValid; }
  glm::vec3 GetWeaponTrailTip() const { return m_weaponTrailTip; }
  glm::vec3 GetWeaponTrailBase() const { return m_weaponTrailBase; }
  uint8_t GetWeaponLevel() const { return m_weaponInfo.itemLevel; }
  uint8_t GetWeaponCategory() const { return m_weaponInfo.category; }

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
  void SetMagicSpeed(int speed) { m_serverMagicSpeed = speed; }
  bool LeveledUpThisFrame() const { return m_leveledUpThisFrame; }
  void ClearLevelUpFlag() { m_leveledUpThisFrame = false; }
  void SetLevelUpFlag() { m_leveledUpThisFrame = true; }

  // XP table: cubic curve (from gObjSetExperienceTable, MaxLevel=400)
  static uint64_t CalcXPForLevel(int level);

  // SafeZone state (called from main.cpp each frame)
  void SetInSafeZone(bool safe);
  bool IsInSafeZone() const { return m_inSafeZone; }
  void Heal(int amount);
  bool HasWeapon() const { return m_weaponBmd != nullptr; }
  int weaponIdleAction() const;
  int defaultIdleAction() const;  // CLASS_ELF → STOP_FEMALE, others → STOP_MALE
  int defaultWalkAction() const;  // CLASS_ELF → WALK_FEMALE, others → WALK_MALE
  float idlePlaySpeed(int action) const; // Main 5.2 ZzzOpenData.cpp per-weapon idle speed
  void SetSlowAnimDuration(float d) { m_slowAnimDuration = d; }

  // Ground item pickup logic
  void SetPendingPickup(int dropIndex) { m_pendingPickupIndex = dropIndex; }
  int GetPendingPickup() const { return m_pendingPickupIndex; }
  void ClearPendingPickup() { m_pendingPickupIndex = -1; }

  // Sit/Pose system (Main 5.2 OPERATE)
  void StartSitPose(bool isSit, float facingAngleDeg, bool alignToObject,
                    const glm::vec3 &snapPos);
  void CancelSitPose();
  bool IsSittingOrPosing() const { return m_sittingOrPosing; }

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
  glm::vec3 GetMoveTarget() const { return m_target; }
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
  // Pet companion dynamic light (soft glow emitter)
  void GetPetLight(std::vector<glm::vec3> &positions,
                   std::vector<glm::vec3> &colors,
                   std::vector<float> &ranges,
                   std::vector<int> &objectTypes) const {
    if (!m_pet.active || m_pet.alpha < 0.1f) return;
    positions.push_back(m_pet.pos);
    // Guardian Angel = soft white-blue, Imp = warm orange
    if (m_pet.itemIndex == 0)
      colors.push_back(glm::vec3(0.45f, 0.50f, 0.65f));
    else
      colors.push_back(glm::vec3(0.65f, 0.40f, 0.18f));
    ranges.push_back(350.0f);
    objectTypes.push_back(0);
  }
  // Main 5.2 AddTerrainLight: specific weapons emit colored point light
  void GetWeaponLight(std::vector<glm::vec3> &positions,
                      std::vector<glm::vec3> &colors,
                      std::vector<float> &ranges,
                      std::vector<int> &objectTypes) const {
    if (!m_weaponBmd || m_inSafeZone) return;
    uint8_t cat = m_weaponInfo.category, idx = m_weaponInfo.itemIndex;
    float lum = m_luminosity;
    glm::vec3 c; float r;
    // Heliacal Sword (Sword+12): warm orange
    if (cat == 0 && idx == 12) { c = glm::vec3(lum, lum * 0.8f, lum * 0.5f); r = 300.f; }
    // Spirit Sword (Sword+19), Staff of Imperial (Staff+10),
    // Saint Scepter (Mace+13), Divine CB (Bow+18): cooler glow
    else if ((cat == 0 && idx == 19) || (cat == 5 && idx == 10) ||
             (cat == 2 && idx == 13) || (cat == 4 && idx == 18))
      { c = glm::vec3(lum * 0.8f, lum * 0.5f, lum * 0.3f); r = 200.f; }
    else return;
    positions.push_back(m_pos + glm::vec3(0, 80, 0));
    colors.push_back(c);
    ranges.push_back(r);
    objectTypes.push_back(0);
  }
  void SetLuminosity(float l) { m_luminosity = l; }
  void SetMapId(int mapId) { m_mapId = mapId; }
  void SetPoisoned(bool p) { m_poisoned = p; }
  void SetFrozen(bool f) { m_frozen = f; }

  // Elf buff aura state (Main 5.2: eBuff_Defense / eBuff_Attack)
  void SetBuffDefense(bool active) { m_buffDefenseActive = active; }
  void SetBuffDamage(bool active) { m_buffDamageActive = active; }
  bool HasBuffDefense() const { return m_buffDefenseActive; }
  bool HasBuffDamage() const { return m_buffDamageActive; }

  // Snap hero Y to terrain height
  void SnapToTerrain();

private:
  glm::vec3 sampleTerrainLightAt(const glm::vec3 &worldPos) const;
  void renderMountModel(const glm::mat4 &view, const glm::mat4 &proj,
                        const glm::vec3 &camPos, float deltaTime,
                        const glm::vec3 &tLight);
  void renderPetCompanion(const glm::mat4 &view, const glm::mat4 &proj,
                          const glm::vec3 &camPos, float deltaTime,
                          const glm::vec3 &tLight);

  // Position & movement
  glm::vec3 m_pos{12800.0f, 0.0f, 12800.0f};
  glm::vec3 m_target{12800.0f, 0.0f, 12800.0f};
  float m_facing = 0.0f;
  float m_targetFacing = 0.0f;
  float m_speed = 375.0f; // Main 5.2: 15 units/tick * 25fps (run speed)
  bool m_moving = false;

  // Animation
  int m_action = 1; // PLAYER_STOP1 (male idle)
  float m_animFrame = 0.0f;
  bool m_idleReversing = false; // Ping-pong: true = playing backward
  static constexpr float ANIM_SPEED = 8.25f;
  int m_rootBone = -1;
  bool m_foot[2] = {false, false}; // Main 5.2: c->Foot[0/1] for walk sound

  // Blending state
  int m_priorAction = -1;
  float m_priorAnimFrame = 0.0f;
  float m_blendAlpha = 1.0f;
  bool m_isBlending = false;
  static constexpr float BLEND_DURATION = 0.25f; // seconds (Main 5.2: ~2 frames)

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
  static constexpr int ACTION_WALK_SWIM = 24;  // Atlans swimming walk
  static constexpr int ACTION_RUN_SWIM = 33;   // Atlans swimming run

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

  // Mount riding actions (_enum.h)
  static constexpr int ACTION_STOP_RIDE = 13;
  static constexpr int ACTION_STOP_RIDE_WEAPON = 14;
  static constexpr int ACTION_RUN_RIDE = 36;
  static constexpr int ACTION_RUN_RIDE_WEAPON = 37;
  static constexpr int ACTION_ATTACK_RIDE_SWORD = 54;
  static constexpr int ACTION_ATTACK_RIDE_TWO_HAND_SWORD = 55;
  static constexpr int ACTION_ATTACK_RIDE_SPEAR = 56;
  static constexpr int ACTION_ATTACK_RIDE_SCYTHE = 57;
  static constexpr int ACTION_ATTACK_RIDE_BOW = 58;
  static constexpr int ACTION_ATTACK_RIDE_CROSSBOW = 59;

  // Sit/Pose actions (_enum.h: PLAYER_SIT1=233, PLAYER_POSE1=239)
  static constexpr int ACTION_SIT1 = 233;
  static constexpr int ACTION_SIT2 = 234;
  static constexpr int ACTION_POSE1 = 239;

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
  bool m_forceHitThisFrame = false; // Set when swing completes without hit
  int m_swordSwingCount = 0;
  float m_attackCooldown = 0.0f;
  float m_globalAttackCooldown = 0.0f;    // GCD remaining
  float m_globalAttackCooldownMax = 0.0f; // GCD total (for UI progress)
  int m_gcdTargetMonster = -1; // Persists through CancelAttack to prevent move-cancel exploit
  int m_attackCycleCount = 0;  // Re-engage counter per target (safety timeout)
  float m_teleportCooldown = 0.0f;        // 60s cooldown after teleport use
  static constexpr float TELEPORT_COOLDOWN_TIME = 60.0f;
  uint8_t m_activeSkillId = 0;     // Non-zero when using a skill attack
  bool m_gcdFromSkill = false;     // True if GCD was set by a skill (persists after CancelAttack)
  // Queued action during GCD — separate from active attack state
  int m_queuedTarget = -1;
  glm::vec3 m_queuedPos = glm::vec3(0);
  uint8_t m_queuedSkill = 0;
  float m_slowAnimDuration = 0.0f; // >0 = stretch heal anim to this duration
  HellfirePhase m_hellfirePhase = HellfirePhase::NONE; // Multi-phase Hellfire animation

  // Main 5.2: Flash (Aqua Beam) delayed beam spawn — beam at frame 7.0, gathering before
  bool m_pendingAquaBeam = false;   // True during wind-up before beam spawns
  bool m_aquaBeamSpawned = false;   // Set true once beam is fired
  float m_aquaGatherTimer = 0.0f;   // Tick accumulator for gathering particle spawns

  // Hellfire: VFX triggers on landing, not at cast start
  bool m_pendingHellfire = false;
  bool m_hellfireSpawned = false;

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
  int m_serverAttackSpeed = 0; // From server (DEX-based, class-specific)
  int m_serverMagicSpeed = 0;  // From server (DEX/10 for DW)
  // Main 5.2: MagicSpeed2 = MagicSpeed * 0.002 added to base ~0.29 PlaySpeed
  // That's roughly 0.7% per point (0.002/0.29). We use 0.7% for magic, 1.5% for melee.
  float attackSpeedMultiplier() const {
    return 1.0f + m_serverAttackSpeed * 0.015f;
  }
  float magicSpeedMultiplier() const {
    return 1.0f + m_serverMagicSpeed * 0.007f;
  }
  // Use magic speed for DW spells (skill IDs 1-17), attack speed for DK melee
  float currentSpeedMultiplier() const {
    if (m_activeSkillId > 0 && m_activeSkillId <= 17)
      return magicSpeedMultiplier();
    return attackSpeedMultiplier();
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
    bgfx::DynamicVertexBufferHandle vbo = BGFX_INVALID_HANDLE;
    int indexCount = 0;
    int vertexCount = 0;
  };

  struct BodyPart {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    std::vector<ShadowMesh> shadowMeshes;
  };
  BodyPart m_parts[PART_COUNT];
  uint8_t m_partLevels[PART_COUNT] = {};      // Item enhancement level per body part (+0 to +15)
  uint8_t m_partItemIndices[PART_COUNT] = {}; // Item index within category (for glow color)
  int m_equipmentLevelSet = 0;                // Full armor set min level (0 = no set bonus)
  glm::vec3 m_setGlowColor{1.0f};            // Set bonus particle color
  float m_setParticleTimer = 0.0f;            // Set bonus particle spawn timer
  void CheckFullArmorSet();                   // Recalculate m_equipmentLevelSet
  BodyPart m_baseHead; // Base head model (HelmClassXX.bmd) for accessory helms
  bool m_showBaseHead = false; // True when equipped helm needs face visible
  std::unique_ptr<Shader> m_shader;

  // Shadow map state
  bgfx::TextureHandle m_shadowMapTex = BGFX_INVALID_HANDLE;
  glm::mat4 m_lightMtx{1.0f};

  // Weapon (attached item model — right hand)
  std::unique_ptr<BMDData> m_weaponBmd;
  std::vector<MeshBuffers> m_weaponMeshBuffers;
  std::vector<ShadowMesh> m_weaponShadowMeshes;
  std::vector<BoneWorldMatrix> m_weaponLocalBones; // Cached static bind-pose
  float m_weaponAnimFrame = 0.0f;        // Current weapon animation frame
  bool m_weaponIsAnimated = false;        // True if weapon BMD has >1 keyframes
  bool m_weaponAnimContinuous = false;    // true = continuous loop (staff), false = attack-synced (bow)
  std::string m_dataPath; // Cached for late weapon loading

  // Weapon glow (Main 5.2: ItemLight / BlendMesh system)
  int m_weaponBlendMesh = -1;  // >=0: additive mesh, -1: none, -2: whole-object pulse
  int m_weaponHiddenMesh = -1; // Mesh index to hide (-1 = none)
  int m_shieldBlendMesh = -1;  // Shield glow mesh index
  int m_shieldHiddenMesh = -1; // Shield hidden mesh index

  // Shield (attached item model — left hand)
  WeaponEquipInfo m_shieldInfo;
  std::unique_ptr<BMDData> m_shieldBmd;
  std::vector<MeshBuffers> m_shieldMeshBuffers;
  std::vector<ShadowMesh> m_shieldShadowMeshes;
  std::vector<BoneWorldMatrix> m_shieldLocalBones; // Cached static bind-pose

  // Wings (attached to back bone 47 — Main 5.2 RenderCharacterBackItem)
  WeaponEquipInfo m_wingInfo;
  std::unique_ptr<BMDData> m_wingBmd;
  std::vector<MeshBuffers> m_wingMeshBuffers;
  std::vector<ShadowMesh> m_wingShadowMeshes;
  std::vector<BoneWorldMatrix> m_wingLocalBones; // Cached static bind-pose
  int m_wingBlendMesh = -1;  // Wing glow mesh index (-1 = none)
  float m_wingAnimFrame = 0.0f;  // Independent wing animation frame

  // Pet companion (Guardian Angel / Imp — Main 5.2 GOBoid.cpp Direction-vector movement)
  struct PetCompanion {
    bool active = false;
    uint8_t itemIndex = 0;          // 0=Guardian Angel, 1=Imp
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    glm::vec3 pos{0.0f};           // Current world position
    float alpha = 0.0f;            // Fade-in alpha (0→1, exponential smoothing)
    float animFrame = 0.0f;        // BMD animation frame
    float sparkTimer = 0.0f;       // Spark particle spawn timer
    int blendMesh = -1;            // Additive mesh Texture index (wings)
    // Polar orbit system: pet circles character at smooth angular velocity
    glm::vec3 orbitCenter{0.0f};   // Smoothed follow point (trails behind character)
    float orbitAngle = 0.0f;       // Current angle around orbit center (radians)
    float orbitSpeed = 0.3f;       // Angular velocity (radians/sec, + = CCW)
    float orbitSpeedTarget = 0.3f; // Target angular velocity (smooth lerp)
    float orbitRadius = 120.0f;    // Current distance from orbit center
    float orbitHeight = 0.0f;      // Current height offset (smoothed)
    float orbitHeightTarget = 0.0f;// Target height offset
    float bobPhase = 0.0f;        // Sine wave phase for gentle vertical bob
    float facing = 0.0f;           // Visual facing angle (yaw, radians)
    float pitch = 0.0f;            // Vertical tilt toward character head (radians)
    float sparkAccum = 0.0f;       // Spark particle timer
    float decisionTimer = 0.0f;    // Timer for orbit behavior changes
    float modelRadius = 50.0f;     // Bounding sphere radius (scaled)
  };
  PetCompanion m_pet;

  // Mount (Uniria / Dinorant — character rides on mount model)
  struct MountData {
    bool active = false;
    uint8_t itemIndex = 0;          // 2=Uniria, 3=Dinorant
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    std::vector<ShadowMesh> shadowMeshes;
    std::vector<BoneWorldMatrix> cachedBones; // For shadow rendering
    int blendMesh = -1;             // Additive mesh Texture index
    float animFrame = 0.0f;         // Mount walk/idle animation frame
    float alpha = 0.0f;
    int rootBone = 0;               // Skeleton root bone index (0=Rider01, 1=Rider02)
    float zBounce = 0.0f;           // Current frame Z bounce from mount root bone
    // Animation blending (walk→idle only)
    int action = 0;
    int priorAction = -1;
    float priorAnimFrame = 0.0f;
    bool isBlending = false;
    float blendAlpha = 1.0f;
    // VFX / Sound
    float dustTimer = 0.0f;           // Accumulator for smoke particle spawning
    float hoofTimer = 0.0f;           // Accumulator for hoofbeat sound timing
    int hoofIndex = 0;                // Cycles through step1/2/3
    bool hoofFoot[2] = {false, false}; // Frame-based triggers (like player m_foot)
  };
  MountData m_mount;
  uint8_t m_mountEquippedIndex = 0xFF; // Persists across safe zone dismount (0xFF = none)

  // Weapon blur trail — computed each frame during Render() when active
  bool m_weaponTrailActive = false;  // Trail capture active
  bool m_weaponTrailValid = false;   // Trail points computed this frame
  glm::vec3 m_weaponTrailTip{0.0f};  // World-space weapon tip
  glm::vec3 m_weaponTrailBase{0.0f}; // World-space weapon base

  // Twisting Slash ghost weapon effect (Main 5.2: MODEL_SKILL_WHEEL2)
  struct WheelGhost {
    float orbitAngle = 0.0f;   // Orbital position (degrees)
    float spinAngle = 0.0f;    // Self-rotation (degrees)
    float spinVelocity = 0.0f; // Accelerating spin (degrees/sec)
    float alpha = 0.0f;        // Transparency (0.6, 0.5, 0.4, 0.3)
    float lifetime = 0.0f;     // Remaining seconds
    bool active = false;
  };
  static constexpr int MAX_WHEEL_GHOSTS = 5;
  WheelGhost m_wheelGhosts[MAX_WHEEL_GHOSTS]{};
  bool m_twistingSlashActive = false;
  float m_wheelSpawnTimer = 0.0f;
  int m_wheelSpawnCount = 0;
  float m_wheelSmokeTimer = 0.0f; // Per-tick smoke particle spawning
  std::vector<MeshBuffers> m_ghostWeaponMeshBuffers; // Static bind-pose copy

  void StartTwistingSlash();
  void UpdateTwistingSlash(float dt);

  // Shadow rendering
  std::unique_ptr<Shader> m_shadowShader;
  std::vector<BoneWorldMatrix> m_cachedBones; // cached from last Render()

  // Sit/Pose state (Main 5.2 OPERATE)
  bool m_sittingOrPosing = false;

  // External data (non-owning)
  int m_pendingPickupIndex = -1;
  const TerrainData *m_terrainData = nullptr;
  std::vector<glm::vec3> m_terrainLightmap;
  std::vector<PointLight> m_pointLights;
  static constexpr int MAX_POINT_LIGHTS = 64;
  float m_luminosity = 1.0f;
  int m_mapId = 0;
  bool m_poisoned = false; // Green tint when poisoned by monster
  bool m_frozen = false;   // Blue tint + 50% speed when frozen by ice monster
  float m_damageFlashTimer = 0.0f; // Brief white flash on hit (Main 5.2)

  // Elf buff aura VFX state (Main 5.2: eBuff_Defense / eBuff_Attack)
  bool m_buffDefenseActive = false;
  bool m_buffDamageActive = false;
  bool m_prevBuffDefense = false;  // Edge detection for SetBuffAura calls
  bool m_prevBuffDamage = false;
  float m_buffAuraTimer = 0.0f; // Weapon sparkle spawn accumulator
};

#endif // HERO_CHARACTER_HPP
