#include "HeroCharacter.hpp"
#include "ChromeGlow.hpp"
#include "ItemModelManager.hpp"
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

// Class code → body part suffix: DW=Class01, DK=Class02, ELF=Class03, MG=Class04
static const char *GetClassBodySuffix(uint8_t classCode) {
  switch (classCode) {
  case 0:  return "Class01"; // DW
  case 16: return "Class02"; // DK
  case 32: return "Class03"; // ELF
  case 48: return "Class04"; // MG
  default: return "Class02";
  }
}

// ─── DK Stat Formulas (MuEmu-0.97k ObjectManager.cpp) ──────────────────

uint64_t HeroCharacter::CalcXPForLevel(int level) {
  if (level <= 1)
    return 0;
  // gObjSetExperienceTable: cubic curve, MaxLevel=400
  // scaleFactor = (UINT32_MAX * 0.95) / 400^3 ≈ 63.7
  static constexpr double kScale =
      ((double)0xFFFFFFFF * 0.95) / (400.0 * 400.0 * 400.0);
  double lv = (double)level - 1.0;
  return (uint64_t)(kScale * lv * lv * lv);
}

void HeroCharacter::RecalcStats() {
  // MaxHP per class (OpenMU formulas, matching server StatCalculator)
  if (m_class == 0) { // CLASS_DW
    m_maxHp = std::max(1, 60 + (m_level - 1) * 1 + ((int)m_vitality - 15) * 2);
  } else if (m_class == 32) { // CLASS_ELF
    m_maxHp = std::max(1, 80 + (m_level - 1) * 1 + ((int)m_vitality - 20) * 2);
  } else if (m_class == 48) { // CLASS_MG
    m_maxHp = std::max(1, 110 + (m_level - 1) * 1 + ((int)m_vitality - 26) * 2);
  } else { // CLASS_DK (16) and default
    m_maxHp = (int)(DK_BASE_HP + DK_LEVEL_LIFE * (m_level - 1) +
                    (m_vitality - DK_BASE_VIT) * DK_VIT_TO_LIFE);
    if (m_maxHp < 1)
      m_maxHp = 1;
  }

  // AG/Mana per class (OpenMU formulas, matching server StatCalculator)
  if (m_class == 16) { // CLASS_DK — AG
    m_maxMana = (int)(m_energy * 1.0f + m_vitality * 0.3f + m_dexterity * 0.2f +
                      m_strength * 0.15f);
  } else if (m_class == 0) { // CLASS_DW
    m_maxMana = 60 + (m_level - 1) * 2 + (m_energy - 30) * 2;
  } else if (m_class == 32) { // CLASS_ELF
    m_maxMana = (int)(30 + (m_level - 1) * 1.5f + (m_energy - 15) * 1.5f);
  } else if (m_class == 48) { // CLASS_MG
    m_maxMana = 60 + (m_level - 1) * 1 + (m_energy - 26) * 2;
  }
  if (m_maxMana < 1)
    m_maxMana = 1;

  // Damage per class (OpenMU formulas, matching server StatCalculator)
  bool hasBow = (m_weaponInfo.category == 4);
  if (m_class == 0) { // CLASS_DW
    m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
    m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);
  } else if (m_class == 32) { // CLASS_ELF
    if (hasBow) {
      m_damageMin = std::max(1, (int)m_strength / 14 + (int)m_dexterity / 7 +
                                    m_weaponDamageMin);
      m_damageMax = std::max(m_damageMin, (int)m_strength / 8 +
                                              (int)m_dexterity / 4 +
                                              m_weaponDamageMax);
    } else {
      m_damageMin = std::max(1, ((int)m_strength + (int)m_dexterity) / 7 +
                                    m_weaponDamageMin);
      m_damageMax = std::max(m_damageMin, ((int)m_strength + (int)m_dexterity) / 4 +
                                              m_weaponDamageMax);
    }
  } else if (m_class == 48) { // CLASS_MG
    m_damageMin = std::max(1, (int)m_strength / 6 + (int)m_energy / 12 +
                                  m_weaponDamageMin);
    m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + (int)m_energy / 8 +
                                            m_weaponDamageMax);
  } else { // CLASS_DK (16) and default
    m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
    m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);
  }

  // Defense per class (OpenMU formulas, matching server StatCalculator)
  if (m_class == 0) { // CLASS_DW
    m_defense = (int)m_dexterity / 4 + m_equipDefenseBonus;
  } else if (m_class == 32) { // CLASS_ELF
    m_defense = (int)m_dexterity / 10 + m_equipDefenseBonus;
  } else if (m_class == 48) { // CLASS_MG
    m_defense = (int)m_dexterity / 4 + m_equipDefenseBonus;
  } else { // CLASS_DK (16) and default
    m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;
  }

  // AttackSuccessRate = Level*5 + (DEX*3)/2 + STR/4
  m_attackSuccessRate =
      m_level * 5 + ((int)m_dexterity * 3) / 2 + (int)m_strength / 4;

  // DefenseSuccessRate = DEX / 3
  m_defenseSuccessRate = (int)m_dexterity / 3;

  // XP threshold for next level
  m_nextExperience = CalcXPForLevel(m_level + 1);
}

void HeroCharacter::GainExperience(uint64_t xp) {
  m_experience += xp;
  m_leveledUpThisFrame = false;

  while (m_experience >= m_nextExperience && m_level < 400) {
    m_level++;
    m_levelUpPoints += DK_POINTS_PER_LEVEL;
    m_leveledUpThisFrame = true;
    RecalcStats();
    m_hp = m_maxHp; // Full refill on level-up
    m_mana = m_maxMana;
    m_ag = m_maxAg;
    std::cout << "[Hero] Level up! Now level " << m_level << " (HP=" << m_maxHp
              << ", MP=" << m_maxMana << ", AG=" << m_maxAg
              << ", points=" << m_levelUpPoints
              << ", nextXP=" << m_nextExperience << ")" << std::endl;
  }
}

bool HeroCharacter::AddStatPoint(int stat) {
  if (m_levelUpPoints <= 0)
    return false;
  switch (stat) {
  case 0:
    m_strength++;
    break;
  case 1:
    m_dexterity++;
    break;
  case 2:
    m_vitality++;
    break;
  case 3:
    m_energy++;
    break;
  default:
    return false;
  }
  m_levelUpPoints--;
  int oldMaxHp = m_maxHp;
  RecalcStats();
  // If max HP increased, add the difference to current HP
  if (m_maxHp > oldMaxHp)
    m_hp += (m_maxHp - oldMaxHp);
  return true;
}

void HeroCharacter::LoadStats(int level, uint16_t str, uint16_t dex,
                              uint16_t vit, uint16_t ene, uint64_t experience,
                              int levelUpPoints, int currentHp, int maxHp,
                              int currentMana, int maxMana, int currentAg,
                              int maxAg, uint8_t charClass) {
  uint8_t oldClass = m_class;
  m_level = level;
  m_class = charClass;

  // Reload default body parts if class changed (e.g. DK→DW)
  if (m_class != oldClass && m_skeleton) {
    for (int i = 0; i < PART_COUNT; i++)
      EquipBodyPart(i, ""); // empty = reload class default
    std::cout << "[Hero] Class changed " << (int)oldClass << " -> "
              << (int)m_class << ", reloaded body parts" << std::endl;
  }
  m_strength = str;
  m_dexterity = dex;
  m_vitality = vit;
  m_energy = ene;
  m_experience = experience;
  m_levelUpPoints = levelUpPoints;
  RecalcStats();

  // Override with server authoritative maximums
  m_maxHp = maxHp > 0 ? maxHp : m_maxHp;
  m_maxMana = maxMana > 0 ? maxMana : m_maxMana;
  m_maxAg = maxAg > 0 ? maxAg : m_maxAg;

  // Restore current HP/Mana/AG from server (clamped to new max values)
  m_hp = std::min(currentHp, m_maxHp);
  if (m_hp <= 0 && currentHp > 0)
    m_hp = m_maxHp; // Don't load as dead if server says alive
  m_mana = std::min(currentMana, m_maxMana);
  m_ag = std::min(currentAg, m_maxAg);

  std::cout << "[Hero] Loaded stats from server: Lv" << m_level
            << " STR=" << m_strength << " DEX=" << m_dexterity
            << " VIT=" << m_vitality << " ENE=" << m_energy << " HP=" << m_hp
            << "/" << m_maxHp << " MP=" << m_mana << "/" << m_maxMana
            << " AG=" << m_ag << "/" << m_maxAg << " XP=" << m_experience
            << " pts=" << m_levelUpPoints << std::endl;
}

void HeroCharacter::Heal(int amount) {
  if (m_heroState != HeroState::ALIVE)
    return;
  m_hp = std::min(m_hp + amount, m_maxHp);
}

void HeroCharacter::SetWeaponBonus(int dmin, int dmax) {
  m_weaponDamageMin = dmin;
  m_weaponDamageMax = dmax;
  // Only recalc damage — don't call RecalcStats which clobbers server-authoritative HP/AG
  m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
  m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);
}

void HeroCharacter::SetDefenseBonus(int def) {
  m_equipDefenseBonus = def;
  // Only recalc defense — don't call RecalcStats which clobbers server-authoritative HP/AG
  m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;
}

DamageResult HeroCharacter::RollAttack(int targetDefense,
                                       int targetDefSuccessRate) const {
  // 1. Miss check — OpenMU formula (matches server)
  int atkRate = m_attackSuccessRate;
  int defRate = targetDefSuccessRate;
  int hitChance;
  if (atkRate > 0 && defRate < atkRate) {
    hitChance = 100 - (defRate * 100) / atkRate;
  } else {
    hitChance = 5;
  }
  if (hitChance < 5)
    hitChance = 5;
  if (rand() % 100 >= hitChance)
    return {0, DamageType::MISS};

  // 2. Excellent check: 1% chance, 1.2x max damage (matches server)
  int critRoll = rand() % 100;
  if (critRoll < 1) {
    int dmg = (m_damageMax * 120) / 100;
    return {std::max(1, dmg - targetDefense), DamageType::EXCELLENT};
  }

  // 3. Critical check: 5% chance, max damage (matches server)
  if (critRoll < 6) {
    int dmg = m_damageMax;
    return {std::max(1, dmg - targetDefense), DamageType::CRITICAL};
  }

  // 4. Normal hit: random in [min, max]
  int dmg = m_damageMin;
  if (m_damageMax > m_damageMin)
    dmg += rand() % (m_damageMax - m_damageMin + 1);
  dmg -= targetDefense;
  return {std::max(1, dmg), DamageType::NORMAL};
}

glm::vec3 HeroCharacter::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
  return TerrainUtils::SampleLightAt(m_terrainLightmap, worldPos);
}

// Helper for smooth rotation (MU DK style interpolation)
static float smoothFacing(float current, float target, float dt) {
  float diff = target - current;
  while (diff > (float)M_PI)
    diff -= 2.0f * (float)M_PI;
  while (diff < -(float)M_PI)
    diff += 2.0f * (float)M_PI;

  if (std::abs(diff) >= (float)M_PI / 4.0f) {
    return target; // Snap for large turns (> 45°) to feel responsive
  }
  // Exponential decay: 0.5^(dt*30)
  float factor = 1.0f - std::pow(0.5f, dt * 30.0f);
  float result = current + diff * factor;
  while (result > (float)M_PI)
    result -= 2.0f * (float)M_PI;
  while (result < -(float)M_PI)
    result += 2.0f * (float)M_PI;
  return result;
}

// ─── Weapon animation helpers (Main 5.2 _enum.h + ZzzCharacter.cpp) ────────

bool HeroCharacter::isDualWielding() const {
  // DK with weapon in right hand AND weapon (not shield/ammo) in left hand
  if (m_weaponInfo.category == 0xFF || m_shieldInfo.category == 0xFF)
    return false;
  if (m_shieldInfo.category == 6)
    return false; // Shield
  // Arrows (idx 15) and Bolts (idx 7) are ammo, not dual-wield weapons
  if (m_shieldInfo.category == 4 &&
      (m_shieldInfo.itemIndex == 7 || m_shieldInfo.itemIndex == 15))
    return false;
  return true;
}

int HeroCharacter::defaultIdleAction() const {
  return (m_class == 32) ? ACTION_STOP_FEMALE : ACTION_STOP_MALE;
}

int HeroCharacter::defaultWalkAction() const {
  return (m_class == 32) ? ACTION_WALK_FEMALE : ACTION_WALK_MALE;
}

// Main 5.2 ZzzOpenData.cpp:329-338 — per-weapon idle PlaySpeed values
float HeroCharacter::idlePlaySpeed(int action) const {
  switch (action) {
  case ACTION_STOP_SWORD:          return 0.26f;
  case ACTION_STOP_TWO_HAND_SWORD: return 0.24f;
  case ACTION_STOP_SPEAR:          return 0.24f;
  case ACTION_STOP_SCYTHE:         return 0.24f; // Same as spear
  case ACTION_STOP_BOW:            return 0.22f;
  case ACTION_STOP_CROSSBOW:       return 0.22f;
  case ACTION_STOP_WAND:           return 0.30f;
  case ACTION_STOP_FEMALE:         return 0.20f; // Elf: slower breathing
  default:                         return 0.28f; // STOP_MALE, etc.
  }
}

int HeroCharacter::weaponIdleAction() const {
  // Atlans: swim idle (uses swim walk at slower speed, handled in animation update)
  if (m_mapId == 7 && !m_inSafeZone)
    return ACTION_WALK_SWIM;

  if (isMountRiding())
    return m_weaponBmd ? ACTION_STOP_RIDE_WEAPON : ACTION_STOP_RIDE;

  if (!m_weaponBmd)
    return defaultIdleAction();

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    return twoH ? ACTION_STOP_TWO_HAND_SWORD : ACTION_STOP_SWORD;
  case 3: // Spear / Scythe (index >= 7 = scythe-class: Berdysh+)
    return (m_weaponInfo.itemIndex >= 7) ? ACTION_STOP_SCYTHE
                                         : ACTION_STOP_SPEAR;
  case 4: // Bow / Crossbow (index >= 8 = crossbow)
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_STOP_CROSSBOW
                                         : ACTION_STOP_BOW;
  case 5: // Staff — Main 5.2: WAND animation only for items 14-20 (Season 2+)
    if (m_weaponInfo.itemIndex >= 14 && m_weaponInfo.itemIndex <= 20)
      return ACTION_STOP_WAND;
    return twoH ? ACTION_STOP_SCYTHE : ACTION_STOP_SWORD;
  default:
    return ACTION_STOP_SWORD;
  }
}

int HeroCharacter::weaponWalkAction() const {
  // Atlans: swimming overrides all weapon walk actions
  if (m_mapId == 7 && !m_inSafeZone)
    return ACTION_WALK_SWIM;

  // Both mounts bounce — character uses running ride animation
  if (isMountRiding())
    return m_weaponBmd ? ACTION_RUN_RIDE_WEAPON : ACTION_RUN_RIDE;

  if (!m_weaponBmd)
    return defaultWalkAction();

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    return twoH ? ACTION_WALK_TWO_HAND_SWORD : ACTION_WALK_SWORD;
  case 3: // Spear / Scythe
    return (m_weaponInfo.itemIndex >= 7) ? ACTION_WALK_SCYTHE
                                         : ACTION_WALK_SPEAR;
  case 4: // Bow / Crossbow
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_WALK_CROSSBOW
                                         : ACTION_WALK_BOW;
  case 5: // Staff — Main 5.2: WAND animation only for items 14-20 (Season 2+)
    if (m_weaponInfo.itemIndex >= 14 && m_weaponInfo.itemIndex <= 20)
      return ACTION_WALK_WAND;
    return twoH ? ACTION_WALK_SCYTHE : ACTION_WALK_SWORD;
  default:
    return ACTION_WALK_SWORD;
  }
}

int HeroCharacter::nextAttackAction() {
  if (isMountRiding()) {
    if (!m_weaponBmd) return ACTION_ATTACK_RIDE_SWORD;
    uint8_t rideCat = m_weaponInfo.category;
    switch (rideCat) {
    case 0: case 1: case 2:
      return m_weaponInfo.twoHanded ? ACTION_ATTACK_RIDE_TWO_HAND_SWORD
                                    : ACTION_ATTACK_RIDE_SWORD;
    case 3:
      return (m_weaponInfo.itemIndex >= 7) ? ACTION_ATTACK_RIDE_SCYTHE
                                           : ACTION_ATTACK_RIDE_SPEAR;
    case 4:
      return (m_weaponInfo.itemIndex >= 8) ? ACTION_ATTACK_RIDE_CROSSBOW
                                           : ACTION_ATTACK_RIDE_BOW;
    default: return ACTION_ATTACK_RIDE_SWORD;
    }
  }

  if (!m_weaponBmd) {
    return ACTION_ATTACK_FIST;
  }

  uint8_t cat = m_weaponInfo.category;
  bool twoH = m_weaponInfo.twoHanded;
  int sc = m_swordSwingCount++;

  // Dual-wield: R1→L1→R2→L2 cycle (Main 5.2 SwordCount%4)
  if (isDualWielding()) {
    static constexpr int cycle[4] = {
        ACTION_ATTACK_SWORD_R1, ACTION_ATTACK_SWORD_L1, ACTION_ATTACK_SWORD_R2,
        ACTION_ATTACK_SWORD_L2};
    return cycle[sc % 4];
  }

  switch (cat) {
  case 0:
  case 1:
  case 2: // Sword / Axe / Mace
    if (twoH) {
      // Two-hand: 3 attack variants (SwordCount%3)
      return ACTION_ATTACK_TWO_HAND_SWORD1 + (sc % 3);
    }
    // One-hand: 2 attack variants (SwordCount%2)
    return (sc % 2 == 0) ? ACTION_ATTACK_SWORD_R1 : ACTION_ATTACK_SWORD_R2;
  case 3: // Spear / Scythe
    if (m_weaponInfo.itemIndex >= 7) {
      // Scythe: 3 attack variants (SwordCount%3)
      return ACTION_ATTACK_SCYTHE1 + (sc % 3);
    }
    return ACTION_ATTACK_SPEAR1; // Spear: single attack
  case 4:                        // Bow / Crossbow
    return (m_weaponInfo.itemIndex >= 8) ? ACTION_ATTACK_CROSSBOW
                                         : ACTION_ATTACK_BOW;
  case 5: // Staff — use matching sword/two-hand attack anim based on handedness
    if (twoH) {
      return ACTION_ATTACK_TWO_HAND_SWORD1 + (sc % 3);
    }
    return (sc % 2 == 0) ? ACTION_ATTACK_SWORD_R1 : ACTION_ATTACK_SWORD_R2;
  default:
    return ACTION_ATTACK_SWORD_R1;
  }
}

void HeroCharacter::Init(const std::string &dataPath) {
  m_dataPath = dataPath;
  std::string playerPath = dataPath + "/Player/";

  // Load skeleton (Player.bmd — bones + actions, zero meshes)
  m_skeleton = BMDParser::Parse(playerPath + "player.bmd");
  if (!m_skeleton) {
    std::cerr << "[Hero] Failed to load Player.bmd skeleton" << std::endl;
    return;
  }
  std::cout << "[Hero] Player.bmd: " << m_skeleton->Bones.size() << " bones, "
            << m_skeleton->Actions.size() << " actions" << std::endl;

  // Load naked body parts for current class
  const char *suffix = GetClassBodySuffix(m_class);
  char partFiles[5][64];
  snprintf(partFiles[0], 64, "Helm%s.bmd", suffix);
  snprintf(partFiles[1], 64, "Armor%s.bmd", suffix);
  snprintf(partFiles[2], 64, "Pant%s.bmd", suffix);
  snprintf(partFiles[3], 64, "Glove%s.bmd", suffix);
  snprintf(partFiles[4], 64, "Boot%s.bmd", suffix);

  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB totalAABB{};

  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3; // triangulated
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };

  for (int p = 0; p < PART_COUNT; ++p) {
    std::string fullPath = playerPath + partFiles[p];
    auto bmd = BMDParser::Parse(fullPath);
    if (!bmd) {
      std::cerr << "[Hero] Failed to load: " << partFiles[p] << std::endl;
      continue;
    }

    for (auto &mesh : bmd->Meshes) {
      UploadMeshWithBones(mesh, playerPath, bones, m_parts[p].meshBuffers,
                          totalAABB, true);
    }
    m_parts[p].shadowMeshes = createShadowMeshes(bmd.get());
    m_parts[p].bmd = std::move(bmd);
    std::cout << "[Hero] Loaded " << partFiles[p] << std::endl;
  }

  // Create shader (same model.vert/frag as ObjectRenderer)
  m_shader = Shader::Load("vs_model.bin", "fs_model.bin");

  // Cache root bone index and log walk animation info
  if (m_skeleton) {
    for (int i = 0; i < (int)m_skeleton->Bones.size(); ++i) {
      if (m_skeleton->Bones[i].Parent == -1 && !m_skeleton->Bones[i].Dummy) {
        m_rootBone = i;
        break;
      }
    }
    const int WALK_ACTION = 15;
    if (m_rootBone >= 0 && WALK_ACTION < (int)m_skeleton->Actions.size()) {
      int numKeys = m_skeleton->Actions[WALK_ACTION].NumAnimationKeys;
      auto &bm = m_skeleton->Bones[m_rootBone].BoneMatrixes[WALK_ACTION];
      if ((int)bm.Position.size() >= numKeys && numKeys > 1) {
        glm::vec3 p0 = bm.Position[0];
        glm::vec3 pN = bm.Position[numKeys - 1];
        float strideY = pN.y - p0.y;
        std::cout << "[Hero] Root bone " << m_rootBone
                  << ": walk stride=" << strideY << " MU-Y over " << numKeys
                  << " keys, LockPositions="
                  << m_skeleton->Actions[WALK_ACTION].LockPositions
                  << std::endl;
      }
    }
  }
  // Create shadow shader
  m_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");

  // Compute initial stats from DK formulas
  RecalcStats();
  m_hp = m_maxHp;
  std::cout << "[Hero] DK Level " << m_level << " — HP=" << m_maxHp
            << " Dmg=" << m_damageMin << "-" << m_damageMax
            << " Def=" << m_defense << " AtkRate=" << m_attackSuccessRate
            << " NextXP=" << m_nextExperience << std::endl;
  std::cout << "[Hero] Character initialized (DK Naked)" << std::endl;
}

void HeroCharacter::Render(const glm::mat4 &view, const glm::mat4 &proj,
                           const glm::vec3 &camPos, float deltaTime) {
  if (!m_skeleton || !m_shader)
    return;

  // Decay damage flash timer
  if (m_damageFlashTimer > 0.0f)
    m_damageFlashTimer = std::max(0.0f, m_damageFlashTimer - deltaTime);

  m_weaponTrailValid = false; // Reset each frame

  // Advance animation
  int numKeys = 1;
  bool lockPos = false;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size()) {
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;
    lockPos = m_skeleton->Actions[m_action].LockPositions;
  }
  if (numKeys > 1) {
    // Don't loop die animation — clamp to last frame when dying/dead
    bool clampAnim =
        (m_heroState == HeroState::DYING || m_heroState == HeroState::DEAD);
    // Heal/learn animation: stretch over 3 seconds, don't loop
    bool isHealAnim =
        (m_action == ACTION_SKILL_VITALITY && m_slowAnimDuration > 0.0f);
    if (isHealAnim)
      clampAnim = true;
    // Scale attack animations faster with agility (OpenMU: DEX/15 for DK)
    bool isAttacking = (m_action >= 38 && m_action <= 59) ||
                       (m_action >= 60 && m_action <= 71) ||
                       (m_action >= 146 && m_action <= 154);
    // Main 5.2 ride PlaySpeeds: idle 0.28 (7.0fps), walk 0.3 (7.5fps)
    bool isRideIdle = (m_action == ACTION_STOP_RIDE ||
                       m_action == ACTION_STOP_RIDE_WEAPON);
    bool isRideWalk = (m_action == ACTION_RUN_RIDE ||
                       m_action == ACTION_RUN_RIDE_WEAPON);
    // Main 5.2 ZzzOpenData.cpp:329-338: idle PlaySpeed varies by weapon type
    bool isIdleAction = (m_action >= 0 && m_action <= 10);
    float speed;
    if (isHealAnim)
      speed = (float)numKeys / m_slowAnimDuration; // Stretch to fit duration
    else if (isAttacking)
      speed = ANIM_SPEED * currentSpeedMultiplier();
    else if (isRideIdle)
      speed = 7.0f;  // Main 5.2: PlaySpeed 0.28 * 25fps
    else if (isRideWalk)
      speed = 7.5f;  // Main 5.2: PlaySpeed 0.3 * 25fps
    else if (isIdleAction)
      speed = idlePlaySpeed(m_action) * 25.0f;
    else
      speed = ANIM_SPEED;
    // Main 5.2: Flash animation slowdown during gathering phase (frames 1.0-3.0)
    // PlaySpeed /= 2 creates dramatic wind-up before beam release
    if (m_activeSkillId == 12 && m_animFrame >= 1.0f && m_animFrame < 3.0f)
      speed *= 0.5f;
    // Main 5.2: eDeBuff_Freeze — 50% animation speed when frozen
    if (m_frozen)
      speed *= 0.5f;

    // Idle animations use ping-pong (forward-backward) to eliminate loop seam
    if (isIdleAction && !clampAnim) {
      float maxFrame = (float)(numKeys - 1);
      if (m_idleReversing) {
        m_animFrame -= speed * deltaTime;
        if (m_animFrame <= 0.0f) {
          m_animFrame = -m_animFrame; // Bounce off start
          m_idleReversing = false;
        }
      } else {
        m_animFrame += speed * deltaTime;
        if (m_animFrame >= maxFrame) {
          m_animFrame = maxFrame - (m_animFrame - maxFrame); // Bounce off end
          m_idleReversing = true;
        }
      }
      // Clamp to valid range
      m_animFrame = std::clamp(m_animFrame, 0.0f, maxFrame);
    } else {
      m_animFrame += speed * deltaTime;
      if (clampAnim) {
        if (m_animFrame >= (float)(numKeys - 1))
          m_animFrame = (float)(numKeys - 1);
      } else {
        int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
        if (m_animFrame >= (float)wrapKeys)
          m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
      }
    }

    // Main 5.2: footstep sounds at animation frames 1.5 and 4.5 during walk
    // City (safe zone) uses soil footsteps, fields use grass
    // Slight pitch variation (0.9-1.1) so steps don't sound identical
    if (m_moving) {
      int walkSound = (m_mapId == 7 && !m_inSafeZone) ? SOUND_SWIM_STEP
                     : m_inSafeZone ? SOUND_WALK_SOIL : SOUND_WALK_GRASS;
      if (m_animFrame >= 1.5f && !m_foot[0]) {
        m_foot[0] = true;
        SoundManager::PlayPitched(walkSound, 0.9f, 1.1f);
      }
      if (m_animFrame >= 4.5f && !m_foot[1]) {
        m_foot[1] = true;
        SoundManager::PlayPitched(walkSound, 0.9f, 1.1f);
      }
      // Reset feet on animation wrap
      if (m_animFrame < 1.0f) {
        m_foot[0] = false;
        m_foot[1] = false;
      }
    } else {
      m_foot[0] = false;
      m_foot[1] = false;
    }
  }

  // Wing animation — independent from character (Main 5.2: PlaySpeed 0.25f idle)
  if (m_wingBmd && !m_wingBmd->Actions.empty()) {
    static constexpr float WING_PLAY_SPEED_IDLE = 0.25f;
    float wingSpeed = WING_PLAY_SPEED_IDLE * 25.0f; // Convert tick-based to per-second
    m_wingAnimFrame += wingSpeed * deltaTime;
    int wingKeys = m_wingBmd->Actions[0].NumAnimationKeys;
    if (wingKeys > 1) {
      if (m_wingAnimFrame >= (float)wingKeys)
        m_wingAnimFrame = std::fmod(m_wingAnimFrame, (float)wingKeys);
    } else {
      m_wingAnimFrame = 0.0f;
    }
  }

  // Weapon animation — bows/crossbows: attack-synced, staves: continuous loop
  if (m_weaponIsAnimated && m_weaponBmd && !m_weaponBmd->Actions.empty()) {
    int weaponKeys = m_weaponBmd->Actions[0].NumAnimationKeys;
    if (m_weaponAnimContinuous) {
      // Continuous loop (Staff07 lightning etc.) — same pattern as wings
      float playSpeed = m_weaponBmd->Actions[0].PlaySpeed;
      if (playSpeed <= 0.0f) playSpeed = 0.25f;
      float weaponSpeed = playSpeed * 25.0f; // tick-based to per-second
      m_weaponAnimFrame += weaponSpeed * deltaTime;
      if (m_weaponAnimFrame >= (float)weaponKeys)
        m_weaponAnimFrame = std::fmod(m_weaponAnimFrame, (float)weaponKeys);
    } else {
      // Attack-synced (bow/crossbow): map attack progress to weapon keyframes
      if (m_attackState == AttackState::SWINGING) {
        int charKeys = 1;
        if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
          charKeys = m_skeleton->Actions[m_action].NumAnimationKeys;
        float spdMul = currentSpeedMultiplier();
        float atkAnimSpeed = ANIM_SPEED * spdMul;
        float animDuration =
            (charKeys > 1) ? (float)charKeys / atkAnimSpeed : 0.5f;
        float progress =
            std::clamp(m_attackAnimTimer / animDuration, 0.0f, 1.0f);
        m_weaponAnimFrame = progress * (float)(weaponKeys - 1);
      } else {
        // Not attacking: smooth return to rest pose (bowstring relaxed)
        if (m_weaponAnimFrame > 0.01f)
          m_weaponAnimFrame = std::max(0.0f, m_weaponAnimFrame - 15.0f * deltaTime);
        else
          m_weaponAnimFrame = 0.0f;
      }
    }
  }

  // Main 5.2: Flash (Aqua Beam) — gathering during wind-up, beam at frame 7.0
  if (m_pendingAquaBeam && m_activeSkillId == 12 && m_vfxManager) {
    // Gathering particles during frames 1.2-3.0 (BITMAP_GATHERING SubType 2)
    if (m_animFrame >= 1.2f && m_animFrame < 3.0f) {
      m_aquaGatherTimer += deltaTime;
      while (m_aquaGatherTimer >= 0.04f) {
        m_aquaGatherTimer -= 0.04f;
        glm::vec3 handPos = m_pos + glm::vec3(0.0f, 120.0f, 0.0f);
        m_vfxManager->SpawnAquaGathering(handPos);
      }
    }
    // Main 5.2: visual beam triggers at frames 7.0-8.0 (hands fully extended)
    if (m_animFrame >= 7.0f && !m_aquaBeamSpawned) {
      m_aquaBeamSpawned = true;
      m_vfxManager->SpawnAquaBeam(m_pos, m_facing);
      m_pendingAquaBeam = false;
      m_aquaPacketReady = true; // Signal main loop to send damage packet now
    }
  }

  // Hellfire: spawn ground fire ring on landing frame (~frame 5)
  if (m_pendingHellfire && m_activeSkillId == 10 && m_vfxManager) {
    if (m_animFrame >= 5.0f && !m_hellfireSpawned) {
      m_hellfireSpawned = true;
      m_vfxManager->SpawnHellfire(m_pos);
      m_pendingHellfire = false;
    }
  }

  // Handle cross-fade blending animation
  if (m_isBlending) {
    m_blendAlpha += deltaTime / BLEND_DURATION;
    if (m_blendAlpha >= 1.0f) {
      m_blendAlpha = 1.0f;
      m_isBlending = false;
    }
  }

  // Compute bones for current animation frame (reuse pre-allocated buffer)
  if (m_isBlending && m_priorAction != -1) {
    ComputeBoneMatricesBlended(m_skeleton.get(), m_priorAction,
                               m_priorAnimFrame, m_action, m_animFrame,
                               m_blendAlpha, m_cachedBones);
  } else {
    ComputeBoneMatricesInterpolated(m_skeleton.get(), m_action,
                                    m_animFrame, m_cachedBones);
  }
  auto &bones = m_cachedBones;

  // LockPositions: root bone X/Y locked to frame 0
  if (m_rootBone >= 0) {
    int i = m_rootBone;

    float dx = 0.0f, dy = 0.0f;

    if (m_isBlending && m_priorAction != -1) {
      // Blend root offsets from both actions if they have lockPos.
      // When mounted: force lock for all actions to keep player on mount.
      bool mounted = isMountRiding();
      bool lock1 = mounted, lock2 = mounted;
      if (m_priorAction < (int)m_skeleton->Actions.size())
        lock1 = lock1 || m_skeleton->Actions[m_priorAction].LockPositions;
      if (m_action < (int)m_skeleton->Actions.size())
        lock2 = lock2 || m_skeleton->Actions[m_action].LockPositions;

      float dx1 = 0.0f, dy1 = 0.0f, dx2 = 0.0f, dy2 = 0.0f;

      if (lock1) {
        glm::vec3 p1;
        glm::vec4 q1;
        if (GetInterpolatedBoneData(m_skeleton.get(), m_priorAction,
                                    m_priorAnimFrame, i, p1, q1)) {
          auto &bm1 = m_skeleton->Bones[i].BoneMatrixes[m_priorAction];
          if (!bm1.Position.empty()) {
            dx1 = p1.x - bm1.Position[0].x;
            dy1 = p1.y - bm1.Position[0].y;
          }
        }
      }
      if (lock2) {
        glm::vec3 p2;
        glm::vec4 q2;
        if (GetInterpolatedBoneData(m_skeleton.get(), m_action, m_animFrame, i,
                                    p2, q2)) {
          auto &bm2 = m_skeleton->Bones[i].BoneMatrixes[m_action];
          if (!bm2.Position.empty()) {
            dx2 = p2.x - bm2.Position[0].x;
            dy2 = p2.y - bm2.Position[0].y;
          }
        }
      }

      // Final blended offset
      dx = dx1 * (1.0f - m_blendAlpha) + dx2 * m_blendAlpha;
      dy = dy1 * (1.0f - m_blendAlpha) + dy2 * m_blendAlpha;
    } else if (lockPos || isMountRiding()) {
      // Standard single-action lock.
      // When mounted: always strip XY root motion to keep player aligned with mount,
      // even if ride actions don't have LockPositions set in BMD.
      auto &bm = m_skeleton->Bones[i].BoneMatrixes[m_action];
      if (!bm.Position.empty()) {
        dx = bones[i][0][3] - bm.Position[0].x;
        dy = bones[i][1][3] - bm.Position[0].y;
      }
    }

    // When riding: strip Z root motion relative to ride IDLE frame 0.
    // Ride walk has root Z=185 vs ride idle Z=141 — normalizing to ride idle
    // prevents the character from jumping up when transitioning idle→walk.
    // Mount's zBounce is applied as shared offset to both player and mount.
    float dz = 0.0f;
    if (isMountRiding() && m_rootBone >= 0) {
      int refAction = m_weaponBmd ? ACTION_STOP_RIDE_WEAPON : ACTION_STOP_RIDE;
      if (refAction < (int)m_skeleton->Actions.size()) {
        auto &bmRef = m_skeleton->Bones[m_rootBone].BoneMatrixes[refAction];
        if (!bmRef.Position.empty()) {
          dz = bones[m_rootBone][2][3] - bmRef.Position[0].z;
        }
      }
    }

    if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
      for (int b = 0; b < (int)bones.size(); ++b) {
        bones[b][0][3] -= dx;
        bones[b][1][3] -= dy;
        if (dz != 0.0f) bones[b][2][3] -= dz;
      }
    }

  }

  // Cache bones for shadow rendering
  m_cachedBones = bones;

  // Re-skin all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd)
      continue;
    for (int mi = 0; mi < (int)m_parts[p].meshBuffers.size() &&
                     mi < (int)m_parts[p].bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(m_parts[p].bmd->Meshes[mi], bones,
                               m_parts[p].meshBuffers[mi]);
    }
  }
  // Re-skin base head (for accessory helms that show face)
  if (m_showBaseHead && m_baseHead.bmd) {
    for (int mi = 0; mi < (int)m_baseHead.meshBuffers.size() &&
                     mi < (int)m_baseHead.bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(m_baseHead.bmd->Meshes[mi], bones,
                               m_baseHead.meshBuffers[mi]);
    }
  }

  // Build model matrix: translate -> MU->GL coord conversion -> facing rotation
  // Ride animations elevate the character via root bone Z. A small saddle offset
  // keeps the character on top of the mount, not inside it.
  glm::vec3 renderPos = m_pos;
  if (isMountRiding()) {
    if (m_mount.itemIndex == 3) // Dinorant
      renderPos.y += 10.0f;
    renderPos.y += m_mount.zBounce; // Shared bounce for all mounts
  }
  glm::mat4 model = glm::translate(glm::mat4(1.0f), renderPos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, m_facing, glm::vec3(0, 0, 1));

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

  // Terrain lightmap at hero position
  glm::vec3 tLight = sampleTerrainLightAt(m_pos);

  // Point lights (pre-cached locations)
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);

  // BGFX: no shader->use() — program bound at submit time
  // View/proj set via bgfx::setViewTransform() in caller
  float fogNear, fogFar;
  float useFog = 1.0f;
  glm::vec3 fogColor;
  if (m_mapId == 1) {
    fogColor = glm::vec3(0.0f);
    fogNear = 800.0f;
    fogFar = 2500.0f;
  } else {
    fogColor = glm::vec3(0.117f, 0.078f, 0.039f);
    fogNear = 1500.0f;
    fogFar = 3500.0f;
  }

  // Helper lambda: set all per-submit uniforms (BGFX consumes per submit)
  auto setHeroUniforms = [&](float bml, float chromeMode, float chromeTime,
                             const glm::vec3 &glowColor,
                             const glm::vec3 &baseTint = glm::vec3(1.0f),
                             float objAlpha = 1.0f) {
    m_shader->setVec4("u_params", glm::vec4(objAlpha, bml, chromeMode, chromeTime));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(glowColor, 0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(baseTint, 0.0f));
    m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_shader->setVec4("u_fogParams", glm::vec4(fogNear, fogFar, useFog, 0.0f));
    m_shader->setVec4("u_fogColor", glm::vec4(fogColor, 0.0f));
    m_shader->uploadPointLights(plCount, m_pointLights.data());
    // Disable shadow map for player character — Main 5.2 uses stencil shadows
    // for characters, not shadow mapping. Shadow map causes self-shadowing
    // artifacts (body casts shadows on own sword/shield/armor).
    m_shader->setVec4("u_shadowParams", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
  };

  // BGFX draw helper
  auto bgfxDrawMesh = [&](MeshBuffers &mb, uint64_t state) {
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    m_shader->setTexture(0, "s_texColor", mb.texture);
    if (bgfx::isValid(m_shadowMapTex))
      m_shader->setTexture(1, "s_shadowMap", m_shadowMapTex);
    bgfx::setState(state);
    bgfx::submit(0, m_shader->program);
  };

  uint64_t stateAlpha = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint64_t stateAdditive = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                          | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ADD;
  uint64_t stateNone = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;


  // Draw all body part meshes
  // Main 5.2: base body tinting (+3/+5) and dimming (+7/+9/+11)
  // g_Luminosity approximation for +3/+5 tint pulsing
  float g_Lum = 0.8f + 0.2f * sinf((float)glfwGetTime() * 2.0f);
  for (int p = 0; p < PART_COUNT; ++p) {
    // +3/+5 base color tinting (Main 5.2 ZzzObject.cpp:10183-10196)
    glm::vec3 partTint(1.0f);
    bool hasTint = false;
    if (m_partLevels[p] >= 5 && m_partLevels[p] < 7) {
      partTint = glm::vec3(g_Lum * 0.5f, g_Lum * 0.7f, g_Lum);
      hasTint = true;
    } else if (m_partLevels[p] >= 3 && m_partLevels[p] < 7) {
      partTint = glm::vec3(g_Lum, g_Lum * 0.6f, g_Lum * 0.6f);
      hasTint = true;
    }

    // Dim base body for glow parts (Main 5.2 ZzzObject.cpp line 10201/10213/10227)
    float partDim = 1.0f;
    if (m_partLevels[p] >= 9) partDim = 0.9f;
    else if (m_partLevels[p] >= 7) partDim = 0.8f;

    // Poison green tint (Main 5.2: RGB 0.3, 1.0, 0.5)
    glm::vec3 finalTint = hasTint ? partTint : glm::vec3(1.0f);
    if (m_poisoned)
      finalTint *= glm::vec3(0.3f, 1.0f, 0.5f);
    // Freeze blue tint (Main 5.2: eDeBuff_Freeze BodyLight 0.3, 0.5, 1.0)
    if (m_frozen)
      finalTint *= glm::vec3(0.3f, 0.5f, 1.0f);
    // Damage flash: brief white flash on hit (Main 5.2 visual feedback)
    if (m_damageFlashTimer > 0.0f)
      finalTint = glm::mix(finalTint, glm::vec3(2.0f), m_damageFlashTimer / 0.15f);

    for (auto &mb : m_parts[p].meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden) continue;
      setHeroUniforms(partDim, 0.0f, 0.0f, glm::vec3(0.0f), finalTint);
      uint64_t st = mb.noneBlend ? stateNone : (mb.bright ? stateAdditive : stateAlpha);
      bgfxDrawMesh(mb, st);
    }
  }
  // Draw base head for accessory helms (face visible underneath helm)
  if (m_showBaseHead) {
    for (auto &mb : m_baseHead.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
      bgfxDrawMesh(mb, stateAlpha);
    }
  }

  // ── +7/+9/+11/+13 armor glow passes (ChromeGlow module) ──
  // Main 5.2 ZzzObject.cpp RenderPartObjectEffect (line 10197-10264):
  // +7:  1 pass  = CHROME+BRIGHT (Chrome01.OZJ)
  // +9:  2 passes = CHROME+BRIGHT + METAL+BRIGHT (Chrome01 + Shiny01)
  // +11: 3 passes = CHROME2+BRIGHT + METAL+BRIGHT + CHROME+BRIGHT
  // Glow-pass uniform helper: disables shadow sampling for additive overlay
  auto setGlowUniforms = [&](float chromeMode, float chromeTime,
                             const glm::vec3 &glowColor) {
    m_shader->setVec4("u_params", glm::vec4(1.0f, 1.0f, chromeMode, chromeTime));
    m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
    m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_glowColor", glm::vec4(glowColor, 0.0f));
    m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    m_shader->setVec4("u_fogParams", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    m_shader->setVec4("u_fogColor", glm::vec4(0.0f));
    m_shader->setVec4("u_shadowParams", glm::vec4(0.0f)); // No shadow on glow
  };
  {
    bool anyGlow = false;
    for (int p = 0; p < PART_COUNT; ++p) {
      if (m_partLevels[p] >= 7) { anyGlow = true; break; }
    }
    if (anyGlow && TexValid(ChromeGlow::GetTextures().chrome1)) {
      float t = (float)glfwGetTime();
      for (int p = 0; p < PART_COUNT; ++p) {
        if (m_partLevels[p] < 7) continue;
        ChromeGlow::GlowPass passes[3];
        int n = ChromeGlow::GetGlowPasses(m_partLevels[p], 7 + p, m_partItemIndices[p], passes);
        for (int gp = 0; gp < n; ++gp) {
          for (auto &mb : m_parts[p].meshBuffers) {
            if (mb.indexCount == 0 || mb.hidden) continue;
            setGlowUniforms((float)passes[gp].chromeMode, t, passes[gp].color);
            bgfx::setTransform(glm::value_ptr(model));
            if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
            else bgfx::setVertexBuffer(0, mb.vbo);
            bgfx::setIndexBuffer(mb.ebo);
            m_shader->setTexture(0, "s_texColor", passes[gp].texture);
            bgfx::setState(stateAdditive);
            bgfx::submit(0, m_shader->program);
          }
        }
      }
    }
    // Reset uniforms after glow passes to prevent leaking into subsequent renderers
    setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
  }

  // Draw weapon (if equipped)
  // SafeZone: weapon renders on bone 47 (back) with rotation/offset
  // Combat: weapon renders on hand bone (33 or 42) with identity offset
  // Reference: ZzzCharacter.cpp RenderCharacterBackItem (line 14634)
  // Use shared constants from MuMath (BMDUtils.hpp)
  constexpr int BONE_BACK = MuMath::BONE_BACK;
  constexpr int BONE_R_HAND = MuMath::BONE_R_HAND;
  auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);

  // Main 5.2: crossbows are Weapon[0] (right hand, bone 33),
  // regular bows are Weapon[1] (left hand, bone 42).
  // Our WeaponCategoryRender table uses bone 42 for all BOW category,
  // so we override to bone 33 for crossbows in combat.
  bool isCrossbowWep =
      MuMath::IsCrossbow(m_weaponInfo.category, m_weaponInfo.itemIndex);

  int combatBone = isCrossbowWep ? BONE_R_HAND : wCat.attachBone;
  // Main 5.2: weapons move to back during swimming (bBindBack=true)
  bool isSwimming = (m_mapId == 7 && !m_inSafeZone && m_moving);
  bool weaponOnBack = m_inSafeZone || isMountRiding() || isSwimming;
  int attachBone = (weaponOnBack && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : combatBone;
  if (m_weaponBmd && !m_weaponMeshBuffers.empty() &&
      attachBone < (int)bones.size()) {

    BoneWorldMatrix weaponOffsetMat;
    if (weaponOnBack) {
      // SafeZone back carry (bone 47) — shared offsets with char select
      glm::vec3 backRot, backOff;
      MuMath::GetWeaponBackOffsets(m_weaponInfo.category, m_weaponInfo.itemIndex,
                                   backRot, backOff);
      weaponOffsetMat = MuMath::BuildWeaponOffsetMatrix(backRot, backOff);
    } else {
      // Combat hand: identity for ALL weapons (Main 5.2 uses Link=false mode —
      // no offset matrix, weapon BMD renders at bone position directly)
      weaponOffsetMat = MuMath::BuildWeaponOffsetMatrix(
          glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));
    }

    // parentMat = CharBone[attachBone] * OffsetMatrix
    BoneWorldMatrix parentMat;
    MuMath::ConcatTransforms((const float(*)[4])bones[attachBone].data(),
                             (const float(*)[4])weaponOffsetMat.data(),
                             (float(*)[4])parentMat.data());

    // Animated or static weapon local bones
    std::vector<BoneWorldMatrix> weaponAnimBones;
    const std::vector<BoneWorldMatrix> *wLocalBonesPtr = &m_weaponLocalBones;
    if (m_weaponIsAnimated) {
      weaponAnimBones = ComputeBoneMatricesInterpolated(
          m_weaponBmd.get(), 0, m_weaponAnimFrame);
      wLocalBonesPtr = &weaponAnimBones;
    }
    const auto &wLocalBones = *wLocalBonesPtr;
    std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
    for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                               (const float(*)[4])wLocalBones[bi].data(),
                               (float(*)[4])wFinalBones[bi].data());
    }

    // Compute weapon blur trail points (Main 5.2: BlurType per weapon category)
    // Weapon-local offsets transformed through parentMat → BMD-local → world
    if (m_weaponTrailActive && !m_inSafeZone) {
      // Main 5.2: BlurType 3 for spears/scythes (tip=-100), BlurType 1 for rest (tip=-20)
      // Exception: Spear idx 9 (Bill of Balrog) and idx 10 use BlurType 1
      float tipY = -20.f; // BlurType 1 default (swords, axes, maces)
      if (m_weaponInfo.category == 3 &&
          m_weaponInfo.itemIndex != 9 && m_weaponInfo.itemIndex != 10)
        tipY = -100.f; // BlurType 3: spears/scythes — wider trail arc
      glm::vec3 tipLocal(0.f, tipY, 0.f);
      glm::vec3 baseLocal(0.f, -120.f, 0.f);   // Blade base (same for all types)
      glm::vec3 tipBmd = MuMath::TransformPoint(
          (const float(*)[4])parentMat.data(), tipLocal);
      glm::vec3 baseBmd = MuMath::TransformPoint(
          (const float(*)[4])parentMat.data(), baseLocal);
      // Apply character model transform: rotZ(facing) → rotY(-90) → rotZ(-90)
      float cosF = cosf(m_facing), sinF = sinf(m_facing);
      auto toWorld = [&](const glm::vec3 &bmd) -> glm::vec3 {
        float rx = bmd.x * cosF - bmd.y * sinF;
        float ry = bmd.x * sinF + bmd.y * cosF;
        return m_pos + glm::vec3(ry, bmd.z, rx);
      };
      m_weaponTrailTip = toWorld(tipBmd);
      m_weaponTrailBase = toWorld(baseBmd);
      m_weaponTrailValid = true;
    }

    // Re-skin weapon vertices using final bone matrices
    for (int mi = 0; mi < (int)m_weaponMeshBuffers.size() &&
                     mi < (int)m_weaponBmd->Meshes.size();
         ++mi) {
      auto &mesh = m_weaponBmd->Meshes[mi];
      auto &mb = m_weaponMeshBuffers[mi];
      if (mb.indexCount == 0)
        continue;

      // Upload re-skinned vertices to GPU
      RetransformMeshWithBones(mesh, wFinalBones, mb);

      // +3/+5 weapon tinting + +7 dimming
      uint8_t wlvl = m_weaponInfo.itemLevel;
      glm::vec3 wTint(1.0f);
      float wDim = 1.0f;
      if (wlvl >= 5 && wlvl < 7) wTint = glm::vec3(g_Lum * 0.5f, g_Lum * 0.7f, g_Lum);
      else if (wlvl >= 3 && wlvl < 7) wTint = glm::vec3(g_Lum, g_Lum * 0.6f, g_Lum * 0.6f);
      if (wlvl >= 9) wDim = 0.9f;
      else if (wlvl >= 7) wDim = 0.8f;

      // Skip hidden meshes (Main 5.2: HiddenMesh)
      if (m_weaponHiddenMesh >= 0 && mb.bmdTextureId == m_weaponHiddenMesh)
        continue;

      if (m_weaponBlendMesh >= 0 && mb.bmdTextureId == m_weaponBlendMesh) {
        // Additive glow mesh
        float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
        setHeroUniforms(pulseLight, 0.0f, 0.0f, glm::vec3(0.0f), wTint);
        bgfxDrawMesh(mb, stateAdditive);
      } else if (m_weaponBlendMesh == -2) {
        // Whole-object oscillating brightness (Main 5.2: BlendMesh=-2)
        float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
        setHeroUniforms(wDim * pulseLight, 0.0f, 0.0f, glm::vec3(0.0f), wTint);
        bgfxDrawMesh(mb, stateAlpha);
      } else {
        setHeroUniforms(wDim, 0.0f, 0.0f, glm::vec3(0.0f), wTint);
        bgfxDrawMesh(mb, stateAlpha);
      }
    }

    // ── +7/+9/+11/+13 weapon glow passes (ChromeGlow module) ──
    if (m_weaponInfo.itemLevel >= 7 && TexValid(ChromeGlow::GetTextures().chrome1)) {
      float t = (float)glfwGetTime();
      ChromeGlow::GlowPass passes[3];
      int n = ChromeGlow::GetGlowPasses(m_weaponInfo.itemLevel, m_weaponInfo.category, m_weaponInfo.itemIndex, passes);
      for (int gp = 0; gp < n; ++gp) {
        for (int mi2 = 0; mi2 < (int)m_weaponMeshBuffers.size(); ++mi2) {
          auto &mb2 = m_weaponMeshBuffers[mi2];
          if (mb2.indexCount == 0) continue;
          if (m_weaponBlendMesh >= 0 && mb2.bmdTextureId == m_weaponBlendMesh) continue;
          if (m_weaponHiddenMesh >= 0 && mb2.bmdTextureId == m_weaponHiddenMesh) continue;
          setGlowUniforms((float)passes[gp].chromeMode, t, passes[gp].color);
          bgfx::setTransform(glm::value_ptr(model));
          if (mb2.isDynamic) bgfx::setVertexBuffer(0, mb2.dynVbo);
          else bgfx::setVertexBuffer(0, mb2.vbo);
          bgfx::setIndexBuffer(mb2.ebo);
          m_shader->setTexture(0, "s_texColor", passes[gp].texture);
          bgfx::setState(stateAdditive);
          bgfx::submit(0, m_shader->program);
        }
      }
      // Reset uniforms after weapon glow passes
      setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
    }

    // Main 5.2: Per-weapon bone glow effects (BITMAP_SHINY/BITMAP_LIGHT sprites)
    // Throttled to 25fps (Main 5.2 tick rate) to avoid particle flood
    if (m_vfxManager && !m_inSafeZone) {
      static float sparkTimer = 0.0f;
      sparkTimer += deltaTime;
      if (sparkTimer >= 0.04f) {
        sparkTimer = 0.0f;
        float t = (float)glfwGetTime();
        float lum = 0.8f + 0.2f * sinf(t * 2.0f); // Luminosity pulsing
        float cosF = cosf(m_facing), sinF = sinf(m_facing);
        auto bmdToWorld = [&](const glm::vec3 &bmd) -> glm::vec3 {
          float rx = bmd.x * cosF - bmd.y * sinF;
          float ry = bmd.x * sinF + bmd.y * cosF;
          return m_pos + glm::vec3(ry, bmd.z, rx);
        };
        auto spawnAt = [&](const glm::vec3 &local, const glm::vec3 &color) {
          glm::vec3 bmd = MuMath::TransformPoint(
              (const float(*)[4])parentMat.data(), local);
          m_vfxManager->SpawnWeaponSparkle(bmdToWorld(bmd), color);
        };
        uint8_t cat = m_weaponInfo.category;
        uint8_t idx = m_weaponInfo.itemIndex;

        bool hasCustom = true; // Set false to trigger default glow
        // ── Swords ──
        if (cat == 0 && idx == 17) { // Dark Breaker: pulsing flare edges
          float s = sinf(t * 4.0f) * 0.3f + 0.7f;
          glm::vec3 c(s * 0.8f, s * 0.6f, s * 1.0f);
          spawnAt({0, -20, -40}, c); spawnAt({0, -160, -10}, c);
          spawnAt({0, -10, 28}, c);  spawnAt({0, -145, 18}, c);
        } else if (cat == 0 && idx == 18) { // Thunder Blade: blue electrical
          float s = sinf(t * 4.0f) * 0.3f + 0.3f;
          glm::vec3 c(s * 0.2f, s * 0.2f, s * 1.0f);
          spawnAt({0, -20, 15}, c); spawnAt({0, -133, 7}, c);
          spawnAt({0, -80, 10}, c);
        }
        // ── Maces / Scepters ──
        else if (cat == 2 && idx == 4) { // Crystal Morning Star: pulsing red
          float s = sinf(t * 4.0f) * 0.3f + 0.7f;
          glm::vec3 c(s * 1.0f, s * 0.2f, s * 0.1f);
          spawnAt({0, -84, 0}, c);
          for (int j = 0; j < 5; ++j)
            spawnAt({0, (float)(-j * 20 - 10), 0}, c);
        } else if (cat == 2 && idx == 5) { // Crystal Sword: orange shaft trail
          glm::vec3 c(lum * 1.0f, lum * 0.6f, lum * 0.4f);
          for (int j = 0; j < 8; ++j)
            if (rand() % 4 < 3)
              spawnAt({0, (float)(-j * 20 - 30), 0}, c);
        } else if (cat == 2 && idx == 6) { // Chaos Dragon Axe: pulsing red sparkle
          float s = sinf(t * 4.0f) * 0.3f + 0.7f;
          glm::vec3 c(s * 1.0f, s * 0.2f, s * 0.1f);
          spawnAt({0, -84, 0}, c);
          for (int j = 0; j < 5; ++j)
            spawnAt({0, (float)(-j * 20 - 10), 0}, c);
        } else if (cat == 2 && idx == 7) { // Battle Scepter: yellow/orange
          glm::vec3 c(lum * 1.0f, lum * 0.9f, lum * 0.0f);
          spawnAt({0, 0, 0}, c);
          float s2 = sinf(t * 2.0f) * 0.5f + 0.5f;
          spawnAt({0, 0, 0}, glm::vec3(0.5f, 0.5f, 0.5f) * s2);
        } else if (cat == 2 && idx == 8) { // Master Scepter: blue + rotating
          float s = sinf(t * 1.0f) + 1.0f;
          glm::vec3 c(lum * 0.2f, lum * 0.1f, lum * 3.0f);
          spawnAt({-15, 0, 0}, c); spawnAt({10, 0, 0}, c);
        } else if (cat == 2 && idx == 9) { // Great Scepter: cyan multi-bone
          float l2 = sinf(t * 2.0f) * 0.35f + 0.65f;
          glm::vec3 c(l2 * 0.6f, l2 * 0.8f, l2 * 1.0f);
          for (int i = 0; i < 4; ++i)
            spawnAt({-10, (float)(-i * 15), 0}, c);
        } else if (cat == 2 && idx == 10) { // Lord Scepter: orange/tan dual
          float l2 = sinf(t * 1.0f) * 0.5f + 0.7f;
          glm::vec3 c(l2 * 1.0f, l2 * 0.8f, l2 * 0.6f);
          spawnAt({0, 0, 0}, c);
          spawnAt({-10, 0, 0}, glm::vec3(0.6f, 0.8f, 1.0f));
        } else if (cat == 2 && idx == 11) { // Great Lord Scepter: orange 3-point
          glm::vec3 c(1.0f, 0.6f, 0.3f);
          spawnAt({0, 0, 0}, c);
          for (int i = 0; i < 3; ++i)
            spawnAt({(float)(i * 15 - 10), 0, 0}, c);
        } else if (cat == 2 && idx == 12) { // Divine Scepter: rotating shiny
          float l2 = sinf(t * 2.0f) * 0.3f + 0.7f;
          glm::vec3 c(l2, l2, l2);
          spawnAt({0, 0, 0}, c);
        } else if (cat == 2 && idx == 13) { // Saint Scepter: orange/red sparkle
          glm::vec3 c(lum * 1.0f, lum * 0.3f, lum * 0.1f);
          if (rand() % 3 == 0)
            spawnAt({0, 0, 0}, glm::vec3(0.4f, 0.4f, 0.4f));
          spawnAt({0, 0, 0}, c);
        }
        // ── Spears ──
        else if (cat == 3 && idx == 10) { // Dragon Spear: 8-point blue glow
          glm::vec3 c(lum * 0.2f, lum * 0.1f, lum * 0.8f);
          for (int i = 1; i < 9; ++i)
            spawnAt({0, (float)(-i * 18), 0}, c);
        }
        // ── Bows ──
        else if (cat == 4 && (idx == 4 || idx == 5)) { // Tiger/Silver Bow: orange
          glm::vec3 c(lum * 1.0f, lum * 0.6f, lum * 0.2f);
          spawnAt({0, -40, 0}, c); spawnAt({0, -100, 0}, c);
        } else if (cat == 4 && idx == 6) { // Chaos Nature Bow: green
          glm::vec3 c(lum * 0.6f, lum * 1.0f, lum * 0.8f);
          for (int i = 0; i < 3; ++i)
            spawnAt({0, (float)(-i * 40 - 30), 0}, c);
        } else if (cat == 4 && idx == 17) { // Celestial Bow: silver-blue on many bones
          glm::vec3 c(lum * 0.5f, lum * 0.5f, lum * 0.8f);
          for (int i = 0; i < 6; ++i) // Bones 13-18 approximate
            spawnAt({0, (float)(-i * 20 - 30), 0}, c);
          for (int i = 0; i < 4; ++i) // Bones 5-8 approximate
            spawnAt({0, (float)(-i * 15 + 20), 0}, c);
        }
        // ── Crossbows ──
        else if (cat == 4 && idx == 13) { // Bluewing CB: blue glow
          glm::vec3 c(lum * 0.3f, lum * 0.3f, lum * 1.0f);
          spawnAt({0, -40, 0}, c); spawnAt({0, -80, 0}, c);
        } else if (cat == 4 && idx == 14) { // Aquagold CB: red glow
          glm::vec3 c(lum * 1.0f, lum * 0.3f, lum * 0.3f);
          spawnAt({0, -40, 0}, c); spawnAt({0, -80, 0}, c);
        } else if (cat == 4 && idx == 16) { // Saint CB: blue multi-point
          glm::vec3 c(lum * 0.3f, lum * 0.5f, lum * 1.0f);
          for (int i = 0; i < 3; ++i)
            spawnAt({0, (float)(-i * 30 - 20), 0}, c);
        }
        // ── Staves ──
        else if (cat == 5 && idx == 4) { // Gorgon Staff: cyan tip
          glm::vec3 c(lum * 0.4f, lum * 0.8f, lum * 0.6f);
          spawnAt({0, -90, 0}, c);
        } else if (cat == 5 && idx == 5) { // Legendary Staff: blue tip
          glm::vec3 c(lum * 0.4f, lum * 0.6f, lum * 1.0f);
          spawnAt({0, -145, 0}, c);
        } else if (cat == 5 && idx == 6) { // Staff of Resurrection: flame trail
          glm::vec3 c1(lum * 1.0f, lum * 0.6f, lum * 0.4f);
          spawnAt({0, -145, 0}, c1);
          for (int j = 0; j < 4; ++j)
            spawnAt({(float)(rand() % 20 - 10), (float)(rand() % 20 - 10 - 90),
                     (float)(rand() % 20 - 10)}, c1);
          glm::vec3 c2(lum * 1.0f, lum * 0.2f, lum * 0.1f);
          for (int j = 0; j < 10; ++j)
            if (rand() % 4 < 3)
              spawnAt({0, (float)(-j * 20 + 60), 0}, c2);
        } else if (cat == 5 && idx == 7) { // Chaos Lightning Staff: cyan bones
          glm::vec3 c(lum * 0.4f, lum * 0.6f, lum * 1.0f);
          spawnAt({0, -130, 0}, c);
          for (int j = 0; j < 5; ++j)
            spawnAt({0, (float)(-j * 15 - 70), 0}, c);
        } else if (cat == 5 && idx == 9) { // Dragon Soul Staff: dual blue
          glm::vec3 c(lum * 0.6f, lum * 0.6f, lum * 2.0f);
          spawnAt({0, -120, 5}, c); spawnAt({0, 100, 10}, c);
        } else if (cat == 5 && idx == 10) { // Staff of Imperial: fan sparkle
          glm::vec3 c(lum * 1.0f, lum * 0.3f, lum * 0.1f);
          for (int i = 0; i < 10; ++i)
            if (rand() % 3 == 0)
              spawnAt({(float)(i * 30 - 180), -40, 0}, c);
        } else if (cat == 5 && idx == 11) { // Divine Staff: blue multi-bone
          float l2 = sinf(t * 2.0f) * 0.3f + 0.7f;
          glm::vec3 c(l2 * 0.3f, l2 * 0.3f, l2 * 1.0f);
          for (int i = 0; i < 4; ++i)
            spawnAt({0, (float)(-i * 25 - 60), 0}, c);
        }
        // ── Default: BITMAP_LIGHT at weapon tip for all other weapons ──
        else {
          hasCustom = false;
          // Main 5.2 default case: CreateSprite(BITMAP_LIGHT, p, 1.4f)
          // Level-based light color (lines 9812-9827)
          glm::vec3 c;
          uint8_t wlvl = m_weaponInfo.itemLevel;
          if (wlvl >= 7)
            c = glm::vec3(lum * 0.5f, lum * 0.4f, lum * 0.3f);
          else if (wlvl >= 5)
            c = glm::vec3(lum * 0.3f, lum * 0.3f, lum * 0.5f);
          else if (wlvl >= 3)
            c = glm::vec3(lum * 0.5f, lum * 0.3f, lum * 0.3f);
          else
            c = glm::vec3(lum * 0.3f, lum * 0.3f, lum * 0.3f);
          spawnAt({0, -110, 0}, c); // Default weapon tip position
        }
      }
    }

  }

  // --- Render shield / left-hand item ---
  // SafeZone: renders on bone 47 (back) offset to not overlap weapon
  // Combat: renders on bone 42 (left hand) with identity offset
  // Arrows/Bolts (cat 4, idx 7 or 15): ALWAYS on back (Main 5.2 line 15012)
  auto &sCat = GetWeaponCategoryRender(6); // category 6 = shield
  bool isQuiver = (m_shieldInfo.category == 4 &&
                   (m_shieldInfo.itemIndex == 7 || m_shieldInfo.itemIndex == 15));
  int shieldBone = ((m_inSafeZone || isQuiver) && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : sCat.attachBone;
  if (m_shieldBmd && !m_shieldMeshBuffers.empty() &&
      shieldBone < (int)bones.size()) {

    // SafeZone/quiver back rendering — shared offsets from MuMath
    bool dualWieldLeft = m_inSafeZone && isDualWielding();
    bool onBack = m_inSafeZone || isQuiver;
    BoneWorldMatrix shieldOffsetMat;
    if (onBack) {
      glm::vec3 sRot, sOff;
      MuMath::GetShieldBackOffsets(dualWieldLeft, isQuiver, sRot, sOff);
      shieldOffsetMat = MuMath::BuildWeaponOffsetMatrix(sRot, sOff);
    } else {
      shieldOffsetMat = MuMath::BuildWeaponOffsetMatrix(
          glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));
    }

    BoneWorldMatrix shieldParentMat;
    MuMath::ConcatTransforms((const float(*)[4])bones[shieldBone].data(),
                             (const float(*)[4])shieldOffsetMat.data(),
                             (float(*)[4])shieldParentMat.data());

    const auto &sLocalBones = m_shieldLocalBones;
    std::vector<BoneWorldMatrix> sFinalBones(sLocalBones.size());
    for (int bi = 0; bi < (int)sLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])shieldParentMat.data(),
                               (const float(*)[4])sLocalBones[bi].data(),
                               (float(*)[4])sFinalBones[bi].data());
    }

    // Re-skin shield vertices
    for (int mi = 0; mi < (int)m_shieldMeshBuffers.size() &&
                     mi < (int)m_shieldBmd->Meshes.size();
         ++mi) {
      auto &mb = m_shieldMeshBuffers[mi];
      if (mb.indexCount == 0) continue;
      RetransformMeshWithBones(m_shieldBmd->Meshes[mi], sFinalBones, mb);
    }

    // Quivers render plain — no tinting or glow
    if (!isQuiver) {
      // +3/+5 shield base tinting & +7/+9 dimming (Main 5.2)
      uint8_t slvl = m_shieldInfo.itemLevel;
      glm::vec3 sTint(1.0f);
      float sDim = 1.0f;
      if (slvl >= 5 && slvl < 7)
        sTint = glm::vec3(g_Lum * 0.5f, g_Lum * 0.7f, g_Lum);
      else if (slvl >= 3 && slvl < 5)
        sTint = glm::vec3(g_Lum, g_Lum * 0.6f, g_Lum * 0.6f);
      if (slvl >= 9) sDim = 0.9f;
      else if (slvl >= 7) sDim = 0.8f;

      for (auto &mb : m_shieldMeshBuffers) {
        if (mb.indexCount == 0) continue;
        // Skip hidden meshes
        if (m_shieldHiddenMesh >= 0 && mb.bmdTextureId == m_shieldHiddenMesh)
          continue;
        if (m_shieldBlendMesh >= 0 && mb.bmdTextureId == m_shieldBlendMesh) {
          // Additive glow mesh
          float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
          setHeroUniforms(pulseLight, 0.0f, 0.0f, glm::vec3(0.0f), sTint);
          bgfxDrawMesh(mb, stateAdditive);
        } else {
          setHeroUniforms(sDim, 0.0f, 0.0f, glm::vec3(0.0f), sTint);
          bgfxDrawMesh(mb, stateAlpha);
        }
      }

      // ── +7/+9/+11/+13 shield glow passes (ChromeGlow module) ──
      if (m_shieldInfo.itemLevel >= 7 && TexValid(ChromeGlow::GetTextures().chrome1)) {
        float t = (float)glfwGetTime();
        ChromeGlow::GlowPass passes[3];
        int n = ChromeGlow::GetGlowPasses(m_shieldInfo.itemLevel, 6, m_shieldInfo.itemIndex, passes);
        for (int gp = 0; gp < n; ++gp) {
          for (auto &mb : m_shieldMeshBuffers) {
            if (mb.indexCount == 0) continue;
            if (m_shieldBlendMesh >= 0 && mb.bmdTextureId == m_shieldBlendMesh) continue;
            if (m_shieldHiddenMesh >= 0 && mb.bmdTextureId == m_shieldHiddenMesh) continue;
            setGlowUniforms((float)passes[gp].chromeMode, t, passes[gp].color);
            bgfx::setTransform(glm::value_ptr(model));
            if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
            else bgfx::setVertexBuffer(0, mb.vbo);
            bgfx::setIndexBuffer(mb.ebo);
            m_shader->setTexture(0, "s_texColor", passes[gp].texture);
            bgfx::setState(stateAdditive);
            bgfx::submit(0, m_shader->program);
          }
        }
        // Reset uniforms after shield glow passes
        setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
      }
    } else {
      // Quiver: plain render, no tinting/glow
      for (auto &mb : m_shieldMeshBuffers) {
        if (mb.indexCount == 0) continue;
        setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f), glm::vec3(1.0f));
        bgfxDrawMesh(mb, stateAlpha);
      }
    }

    // Main 5.2: Shield bone glow effects
    if (m_vfxManager && m_shieldInfo.category == 6 && !m_inSafeZone && !isQuiver) {
      static float shieldSparkTimer = 0.0f;
      shieldSparkTimer += deltaTime;
      if (shieldSparkTimer >= 0.04f) {
        shieldSparkTimer = 0.0f;
        float lum = 0.8f + 0.2f * sinf((float)glfwGetTime() * 2.0f);
        float cosF = cosf(m_facing), sinF = sinf(m_facing);
        auto shieldToWorld = [&](const glm::vec3 &local) -> glm::vec3 {
          glm::vec3 bmd = MuMath::TransformPoint(
              (const float(*)[4])shieldParentMat.data(), local);
          float rx = bmd.x * cosF - bmd.y * sinF;
          float ry = bmd.x * sinF + bmd.y * cosF;
          return m_pos + glm::vec3(ry, bmd.z, rx);
        };
        uint8_t sIdx = m_shieldInfo.itemIndex;
        // Legendary Shield (Shield+14): cyan glow
        if (sIdx == 14) {
          glm::vec3 c(lum * 0.4f, lum * 0.6f, lum * 1.5f);
          m_vfxManager->SpawnWeaponSparkle(shieldToWorld({20, 0, 0}), c);
        }
        // Grand Soul Shield (Shield+15): bright blue glow at bone offset
        else if (sIdx == 15) {
          glm::vec3 c(lum * 0.6f, lum * 0.6f, lum * 2.0f);
          m_vfxManager->SpawnWeaponSparkle(shieldToWorld({15, -15, 0}), c);
        }
      }
    }
  }

  // ── Wings rendering (Main 5.2 RenderCharacterBackItem — bone 47) ──
  static constexpr int WING_BONE = 47;
  if (m_wingBmd && !m_wingMeshBuffers.empty() &&
      WING_BONE < (int)bones.size()) {

    // Wing05/06 have full Player.bmd-compatible biped skeletons (75-80 bones).
    // Their vertices reference player bone indices directly — skin them using
    // the player's animated bones so wings follow body movement naturally.
    // Standalone wings (01-04, 07) attach rigidly to bone 47 with offset.
    bool isBipedWing =
        ((int)m_wingBmd->Bones.size() > MuMath::PLAYER_BONE_COUNT);

    std::vector<BoneWorldMatrix> wFinalBones;

    if (isBipedWing) {
      // Hybrid bone approach: biped wing BMDs (75-80 bones) share the player
      // skeleton layout for bones 0-59 and add wing-specific bones at 60+.
      int wingBoneCount = (int)m_wingBmd->Bones.size();
      wFinalBones.resize(wingBoneCount);

      int copyCount = std::min(MuMath::PLAYER_BONE_COUNT, wingBoneCount);
      for (int bi = 0; bi < copyCount && bi < (int)bones.size(); ++bi) {
        wFinalBones[bi] = bones[bi];
      }

      for (int bi = MuMath::PLAYER_BONE_COUNT; bi < wingBoneCount; ++bi) {
        glm::vec3 pos;
        glm::vec4 q;
        if (!GetInterpolatedBoneData(m_wingBmd.get(), 0, m_wingAnimFrame, bi,
                                     pos, q))
          continue;
        float quat[4] = {q.x, q.y, q.z, q.w};
        float local[3][4];
        MuMath::QuaternionMatrix(quat, local);
        local[0][3] = pos.x;
        local[1][3] = pos.y;
        local[2][3] = pos.z;

        int parent = m_wingBmd->Bones[bi].Parent;
        if (parent >= 0 && parent < (int)wFinalBones.size()) {
          MuMath::ConcatTransforms(
              (const float(*)[4])wFinalBones[parent].data(), local,
              (float(*)[4])wFinalBones[bi].data());
        } else {
          memcpy(wFinalBones[bi].data(), local, sizeof(float) * 12);
        }
      }
    } else {
      // Standalone wing: attach to bone 47 with shared offset
      BoneWorldMatrix wingOffsetMat =
          MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                          MuMath::WING_BACK_OFFSET);
      BoneWorldMatrix wingParentMat;
      MuMath::ConcatTransforms((const float(*)[4])bones[WING_BONE].data(),
                               (const float(*)[4])wingOffsetMat.data(),
                               (float(*)[4])wingParentMat.data());

      // Use animated wing bones if wing has animation, otherwise static
      std::vector<BoneWorldMatrix> wingAnimBones;
      const std::vector<BoneWorldMatrix> *wLocalBonesPtr = &m_wingLocalBones;
      if (!m_wingBmd->Actions.empty() &&
          m_wingBmd->Actions[0].NumAnimationKeys > 1) {
        wingAnimBones = ComputeBoneMatricesInterpolated(m_wingBmd.get(), 0,
                                                        m_wingAnimFrame);
        wLocalBonesPtr = &wingAnimBones;
      }
      const auto &wLocalBones = *wLocalBonesPtr;
      wFinalBones.resize(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])wingParentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }
    }

    // Re-skin wing vertices
    for (int mi = 0; mi < (int)m_wingMeshBuffers.size() &&
                     mi < (int)m_wingBmd->Meshes.size();
         ++mi) {
      auto &mb = m_wingMeshBuffers[mi];
      if (mb.indexCount == 0) continue;
      RetransformMeshWithBones(m_wingBmd->Meshes[mi], wFinalBones, mb);
    }

    // Wing rendering: Main 5.2 LightEnable=false for ALL wings — bypass
    // per-vertex lighting. Wings render with flat BodyLight(1,1,1).
    // Use glowColor shader path (chromeMode=0) for flat white lighting.
    // BGFX: no cull face state — set per-submit via state flags (omit BGFX_STATE_CULL_*)
    for (auto &mb : m_wingMeshBuffers) {
      if (mb.indexCount == 0) continue;
      setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(1.0f)); // glowColor=(1,1,1) flat lit
      uint64_t st = mb.bright ? stateAdditive : stateAlpha;
      bgfxDrawMesh(mb, st);
    }
  }

  // ── Full armor set bonus particles (Main 5.2 EquipmentLevelSet) ──
  // Main 5.2 ZzzCharacter.cpp:10615-10695: 3x BITMAP_WATERFALL_2 at fixed body offsets
  // Spawn probability per 25fps tick: +10=25%, +11=33%, +12=50%, +13+=100%
  // At 60fps we scale: check every frame with adjusted probability
  if (m_equipmentLevelSet >= 10 && m_vfxManager) {
    // Waterfall rising particles: probability-per-frame model
    // Main 5.2 ticks at 25fps. At 60fps, scale probabilities: p60 = 1-(1-p25)^(25/60)
    // +10: ~10%/frame, +11: ~14%/frame, +12: ~21%/frame, +13: ~42%/frame
    int chance = 10; // +10 default
    if (m_equipmentLevelSet >= 13)      chance = 3;  // ~33% per frame (always at 25fps)
    else if (m_equipmentLevelSet >= 12) chance = 5;  // ~20% per frame
    else if (m_equipmentLevelSet >= 11) chance = 7;  // ~14% per frame

    if ((rand() % chance) == 0) {
      // Main 5.2: 3 particles at fixed body offsets (bone 0 local space)
      // Position 1: (0, -18, 50) — left side
      // Position 2: (0,   0, 70) — center (higher)
      // Position 3: (0,  18, 50) — right side
      // In our coordinate system: Y=height, X/Z=horizontal
      float cosF = cosf(m_facing), sinF = sinf(m_facing);
      glm::vec3 offsets[3] = {
        glm::vec3(-18.0f * sinF, 50.0f, -18.0f * cosF), // Left
        glm::vec3(0.0f,          70.0f, 0.0f),           // Center
        glm::vec3( 18.0f * sinF, 50.0f,  18.0f * cosF), // Right
      };
      for (int i = 0; i < 3; ++i) {
        glm::vec3 spawnPos = m_pos + offsets[i];
        m_vfxManager->SpawnBurstColored(ParticleType::SET_WATERFALL, spawnPos,
                                         m_setGlowColor, 1);
      }
    }

    // Occasional bright flare sparkle (Main 5.2: rand()%20==0, ~5% per tick)
    // Scale flare count by enhancement level
    int flareChance = 20; // ~5% per frame at 60fps ≈ 3/sec
    int flareCount = 1;
    if (m_equipmentLevelSet >= 13)      { flareChance = 10; flareCount = 3; }
    else if (m_equipmentLevelSet >= 12) { flareChance = 12; flareCount = 2; }
    else if (m_equipmentLevelSet >= 11) { flareChance = 20; flareCount = 2; }
    if ((rand() % flareChance) == 0) {
      glm::vec3 flarePos = m_pos + glm::vec3(0.0f, 50.0f + (rand() % 80), 0.0f);
      m_vfxManager->SpawnBurstColored(ParticleType::FLARE, flarePos,
                                       m_setGlowColor, flareCount);
    }
  }

  // ── Elf buff aura ribbon trails (Main 5.2: MODEL_SPEARSKILL SubType 3/4) ──
  // Activate/deactivate ribbon auras when buff state changes, update center each frame
  if (m_vfxManager) {
    // Defense buff (type 1)
    if (m_buffDefenseActive != m_prevBuffDefense) {
      m_vfxManager->SetBuffAura(1, m_buffDefenseActive, m_pos);
      m_prevBuffDefense = m_buffDefenseActive;
    }
    if (m_buffDefenseActive)
      m_vfxManager->UpdateBuffAuraCenter(1, m_pos);

    // Damage buff (type 2)
    if (m_buffDamageActive != m_prevBuffDamage) {
      m_vfxManager->SetBuffAura(2, m_buffDamageActive, m_pos);
      m_prevBuffDamage = m_buffDamageActive;
    }
    if (m_buffDamageActive) {
      m_vfxManager->UpdateBuffAuraCenter(2, m_pos);
      // Main 5.2: BITMAP_SHINY+1 weapon sparkles while damage buff active
      m_buffAuraTimer += deltaTime;
      if (m_buffAuraTimer >= 0.1f) { // ~10 sparkles/sec
        m_buffAuraTimer -= 0.1f;
        // Spawn sparkle near weapon hand bone (bone 18 = right hand)
        if (m_cachedBones.size() > 18) {
          const auto &bone = m_cachedBones[18];
          glm::vec3 bonePos(bone[0][3], bone[1][3], bone[2][3]);
          m_vfxManager->SpawnWeaponSparkle(
            bonePos + glm::vec3((float)(rand()%20-10), (float)(rand()%20-10), (float)(rand()%20-10)),
            glm::vec3(1.0f, 0.7f, 0.3f));
        }
      }
    } else {
      m_buffAuraTimer = 0.0f;
    }
  }

  // ── Atlans: underwater bubble emission from head bone ──
  // Main 5.2: if(WorldActive==WD_7ATLANSE && WorldTime%10000<1000) CreateParticle(BUBBLE)
  if (m_mapId == 7 && !m_inSafeZone && m_vfxManager) {
    static float bubbleTimer = 0.0f;
    bubbleTimer += deltaTime;
    if (bubbleTimer >= 10.0f) { // Every ~10 seconds
      bubbleTimer -= 10.0f;
      // Spawn a few bubbles from head area
      glm::vec3 headPos = m_pos + glm::vec3(0, 160, 0);
      for (int b = 0; b < 3; ++b) {
        glm::vec3 off((float)(rand() % 20 - 10), (float)(rand() % 10),
                      (float)(rand() % 20 - 10));
        m_vfxManager->SpawnBurstColored(ParticleType::SPELL_WATER, headPos + off,
                                         glm::vec3(0.5f, 0.7f, 1.0f), 1);
      }
    }
  }

  // ── Twisting Slash: render ghost weapon copies orbiting the hero ──
  if (m_twistingSlashActive && !m_ghostWeaponMeshBuffers.empty()) {
    for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i) {
      const auto &g = m_wheelGhosts[i];
      if (!g.active)
        continue;

      // Ghost world position: orbit around hero at radius 150, height +100
      float orbitRad = glm::radians(g.orbitAngle);
      glm::vec3 ghostPos =
          m_pos + glm::vec3(sinf(orbitRad) * 150.0f, 100.0f,
                            cosf(orbitRad) * 150.0f);

      glm::mat4 ghostModel = glm::translate(glm::mat4(1.0f), ghostPos);
      ghostModel = glm::rotate(ghostModel, glm::radians(-90.0f),
                                glm::vec3(0, 0, 1));
      ghostModel = glm::rotate(ghostModel, glm::radians(-90.0f),
                                glm::vec3(0, 1, 0));
      ghostModel = glm::rotate(ghostModel, glm::radians(g.orbitAngle),
                                glm::vec3(0, 0, 1));
      ghostModel = glm::rotate(ghostModel, glm::radians(90.0f),
                                glm::vec3(0, 1, 0));
      ghostModel = glm::rotate(ghostModel, glm::radians(g.spinAngle),
                                glm::vec3(0, 0, 1));
      ghostModel = glm::scale(ghostModel, glm::vec3(0.8f));

      uint64_t ghostState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_DEPTH_TEST_LESS
                           | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
      for (auto &mb : m_ghostWeaponMeshBuffers) {
        if (mb.indexCount == 0) continue;
        bgfx::setTransform(glm::value_ptr(ghostModel));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        m_shader->setTexture(0, "s_texColor", mb.texture);
        setHeroUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f), glm::vec3(1.0f), g.alpha);
        bgfx::setState(ghostState);
        bgfx::submit(0, m_shader->program);
      }
    }

  }


  // Mount rendering (extracted to HeroCharacterMount.cpp)
  renderMountModel(view, proj, camPos, deltaTime, tLight);

  // Pet companion rendering (extracted to HeroCharacterPet.cpp)
  renderPetCompanion(view, proj, camPos, deltaTime, tLight);
}

void HeroCharacter::RenderShadow(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_skeleton || !m_shadowShader || m_cachedBones.empty())
    return;
  // Atlans: no character shadows underwater (light diffusion)
  if (m_mapId == 7)
    return;

  // Shadow model matrix: NO facing rotation (facing is baked into vertices
  // before shadow projection so the shadow direction stays fixed in world
  // space)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  // BGFX: stencil-based shadow merging — draw each pixel at most once.
  // Stencil buffer is cleared to 0 at frame start (setViewClear).
  uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint32_t shadowStencil = BGFX_STENCIL_TEST_EQUAL
                         | BGFX_STENCIL_FUNC_REF(0)
                         | BGFX_STENCIL_FUNC_RMASK(0xFF)
                         | BGFX_STENCIL_OP_FAIL_S_KEEP
                         | BGFX_STENCIL_OP_FAIL_Z_KEEP
                         | BGFX_STENCIL_OP_PASS_Z_INCR;

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 300.0f;   // Gentle side offset for natural angle
  const float sy = 3500.0f;  // Ground plane — compact shadow

  // Pre-compute facing rotation in MU-local space (around MU Z = height axis)
  float cosF = cosf(m_facing);
  float sinF = sinf(m_facing);

  auto renderShadowBatch = [&](const BMDData *bmd,
                               std::vector<ShadowMesh> &shadowMeshes,
                               int attachBone = -1,
                               const std::vector<BoneWorldMatrix> *weaponFinalBones = nullptr) {
    if (!bmd)
      return;
    for (int mi = 0;
         mi < (int)bmd->Meshes.size() && mi < (int)shadowMeshes.size(); ++mi) {
      auto &sm = shadowMeshes[mi];
      if (sm.vertexCount == 0 || !bgfx::isValid(sm.vbo))
        continue;

      auto &mesh = bmd->Meshes[mi];
      static std::vector<glm::vec3> shadowVerts;
      shadowVerts.clear();

      const float(*boneMatrix)[4] = nullptr;
      if (!weaponFinalBones && attachBone >= 0 && attachBone < (int)m_cachedBones.size()) {
        boneMatrix = (const float(*)[4])m_cachedBones[attachBone].data();
      }

      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        int steps = (tri.Polygon == 3) ? 3 : 4;
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;

          if (weaponFinalBones) {
            // Weapon/Shield: per-vertex bone from precomputed final bones
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)weaponFinalBones->size())
              pos = MuMath::TransformPoint(
                  (const float(*)[4])(*weaponFinalBones)[boneIdx].data(), pos);
          } else if (boneMatrix) {
            // Single attach bone (legacy path)
            pos = MuMath::TransformPoint(boneMatrix, pos);
          } else {
            // Body parts: transform by per-vertex bone
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
            }
          }

          // Apply facing rotation in MU space
          float rx = pos.x * cosF - pos.y * sinF;
          float ry = pos.x * sinF + pos.y * cosF;
          pos.x = rx;
          pos.y = ry;

          // Shadow projection
          if (pos.z < sy) {
            float factor = 1.0f / (pos.z - sy);
            pos.x += pos.z * (pos.x + sx) * factor;
            pos.y += pos.z * (pos.y + sx) * factor;
          }
          pos.z = 5.0f;
          shadowVerts.push_back(pos);
        }
        if (steps == 4) {
          int quadIndices[3] = {0, 2, 3};
          for (int v : quadIndices) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;

            if (weaponFinalBones) {
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)weaponFinalBones->size())
                pos = MuMath::TransformPoint(
                    (const float(*)[4])(*weaponFinalBones)[boneIdx].data(), pos);
            } else if (boneMatrix) {
              pos = MuMath::TransformPoint(boneMatrix, pos);
            } else {
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)m_cachedBones.size()) {
                pos = MuMath::TransformPoint(
                    (const float(*)[4])m_cachedBones[boneIdx].data(), pos);
              }
            }

            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;

            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
        }
      }

      if (!shadowVerts.empty()) {
        bgfx::update(sm.vbo, 0,
                     bgfx::copy(shadowVerts.data(), shadowVerts.size() * sizeof(glm::vec3)));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, sm.vbo, 0, (uint32_t)shadowVerts.size());
        bgfx::setState(shadowState);
        bgfx::setStencil(shadowStencil);
        bgfx::submit(0, m_shadowShader->program);
      }
    }
  };

  // Render all active parts
  for (int p = 0; p < PART_COUNT; ++p) {
    if (m_parts[p].bmd) {
      renderShadowBatch(m_parts[p].bmd.get(), m_parts[p].shadowMeshes, -1);
    }
  }
  // Base head shadow (accessory helms)
  if (m_showBaseHead && m_baseHead.bmd) {
    renderShadowBatch(m_baseHead.bmd.get(), m_baseHead.shadowMeshes, -1);
  }

  // Weapons and shields — compute full bone matrices matching visible rendering
  // (parentMat * weaponLocalBones[i] for per-vertex skinning)
  constexpr int SHADOW_BONE_BACK = MuMath::BONE_BACK;
  if (m_weaponBmd) {
    auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);
    int bone = (m_inSafeZone && SHADOW_BONE_BACK < (int)m_cachedBones.size())
                   ? SHADOW_BONE_BACK
                   : wCat.attachBone;
    if (bone < (int)m_cachedBones.size()) {
      BoneWorldMatrix off =
          m_inSafeZone
              ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                                glm::vec3(-20.f, 5.f, 40.f))
              : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                glm::vec3(0, 0, 0));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])m_cachedBones[bone].data(),
                               (const float(*)[4])off.data(),
                               (float(*)[4])parentMat.data());
      std::vector<BoneWorldMatrix> weaponAnimBones;
      const std::vector<BoneWorldMatrix> *wLocalBonesPtr = &m_weaponLocalBones;
      if (m_weaponIsAnimated) {
        weaponAnimBones = ComputeBoneMatricesInterpolated(
            m_weaponBmd.get(), 0, m_weaponAnimFrame);
        wLocalBonesPtr = &weaponAnimBones;
      }
      const auto &wLocalBones = *wLocalBonesPtr;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      renderShadowBatch(m_weaponBmd.get(), m_weaponShadowMeshes, -1, &wFinalBones);
    }
  }
  if (m_shieldBmd) {
    int bone = (m_inSafeZone && SHADOW_BONE_BACK < (int)m_cachedBones.size())
                   ? SHADOW_BONE_BACK
                   : GetWeaponCategoryRender(6).attachBone;
    if (bone < (int)m_cachedBones.size()) {
      bool dw = m_inSafeZone && isDualWielding();
      BoneWorldMatrix off =
          m_inSafeZone
              ? (dw ? MuMath::BuildWeaponOffsetMatrix(
                          glm::vec3(-110.f, 180.f, 90.f),
                          glm::vec3(20.f, 15.f, 40.f))
                    : MuMath::BuildWeaponOffsetMatrix(
                          glm::vec3(70.f, 0.f, 90.f),
                          glm::vec3(-10.f, 0.f, 0.f)))
              : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                glm::vec3(0, 0, 0));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms((const float(*)[4])m_cachedBones[bone].data(),
                               (const float(*)[4])off.data(),
                               (float(*)[4])parentMat.data());
      const auto &sLocalBones = m_shieldLocalBones;
      std::vector<BoneWorldMatrix> sFinalBones(sLocalBones.size());
      for (int bi = 0; bi < (int)sLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])sLocalBones[bi].data(),
                                 (float(*)[4])sFinalBones[bi].data());
      renderShadowBatch(m_shieldBmd.get(), m_shieldShadowMeshes, -1, &sFinalBones);
    }
  }

  // Wing shadow
  if (m_wingBmd && !m_wingShadowMeshes.empty()) {
    static constexpr int WING_SHADOW_BONE = 47;
    bool isBipedShadow = ((int)m_wingBmd->Bones.size() > MuMath::PLAYER_BONE_COUNT);
    if (isBipedShadow) {
      // Biped wings use player bones — shadow uses m_cachedBones directly
      renderShadowBatch(m_wingBmd.get(), m_wingShadowMeshes, -1,
                        &m_cachedBones);
    } else if (WING_SHADOW_BONE < (int)m_cachedBones.size()) {
      BoneWorldMatrix off =
          MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                          glm::vec3(0, 0, 15));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms(
          (const float(*)[4])m_cachedBones[WING_SHADOW_BONE].data(),
          (const float(*)[4])off.data(), (float(*)[4])parentMat.data());
      const auto &wLocalBones = m_wingLocalBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi)
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      renderShadowBatch(m_wingBmd.get(), m_wingShadowMeshes, -1, &wFinalBones);
    }
  }

  // Mount shadow — uses mount's own cached bones (not player bones)
  if (m_mount.active && m_mount.bmd && !m_mount.shadowMeshes.empty() &&
      !m_mount.cachedBones.empty() && m_mount.alpha > 0.0f) {
    for (int mi = 0;
         mi < (int)m_mount.bmd->Meshes.size() && mi < (int)m_mount.shadowMeshes.size(); ++mi) {
      auto &sm = m_mount.shadowMeshes[mi];
      if (sm.vertexCount == 0 || !bgfx::isValid(sm.vbo))
        continue;
      auto &mesh = m_mount.bmd->Meshes[mi];
      static std::vector<glm::vec3> shadowVerts;
      shadowVerts.clear();
      for (int i = 0; i < mesh.NumTriangles; ++i) {
        auto &tri = mesh.Triangles[i];
        for (int v = 0; v < 3; ++v) {
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 pos = srcVert.Position;
          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)m_mount.cachedBones.size())
            pos = MuMath::TransformPoint(
                (const float(*)[4])m_mount.cachedBones[boneIdx].data(), pos);
          float rx = pos.x * cosF - pos.y * sinF;
          float ry = pos.x * sinF + pos.y * cosF;
          pos.x = rx;
          pos.y = ry;
          if (pos.z < sy) {
            float factor = 1.0f / (pos.z - sy);
            pos.x += pos.z * (pos.x + sx) * factor;
            pos.y += pos.z * (pos.y + sx) * factor;
          }
          pos.z = 5.0f;
          shadowVerts.push_back(pos);
        }
        if (tri.Polygon == 4) {
          int quadIndices[3] = {0, 2, 3};
          for (int v : quadIndices) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)m_mount.cachedBones.size())
              pos = MuMath::TransformPoint(
                  (const float(*)[4])m_mount.cachedBones[boneIdx].data(), pos);
            float rx = pos.x * cosF - pos.y * sinF;
            float ry = pos.x * sinF + pos.y * cosF;
            pos.x = rx;
            pos.y = ry;
            if (pos.z < sy) {
              float factor = 1.0f / (pos.z - sy);
              pos.x += pos.z * (pos.x + sx) * factor;
              pos.y += pos.z * (pos.y + sx) * factor;
            }
            pos.z = 5.0f;
            shadowVerts.push_back(pos);
          }
        }
      }
      if (!shadowVerts.empty()) {
        bgfx::update(sm.vbo, 0,
                     bgfx::copy(shadowVerts.data(), shadowVerts.size() * sizeof(glm::vec3)));
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, sm.vbo, 0, (uint32_t)shadowVerts.size());
        bgfx::setState(shadowState);
        bgfx::setStencil(shadowStencil);
        bgfx::submit(0, m_shadowShader->program);
      }
    }
  }

}

void HeroCharacter::SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx) {
  m_shadowMapTex = tex;
  m_lightMtx = lightMtx;
}

void HeroCharacter::RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram) {
  if (!m_skeleton || m_cachedBones.empty())
    return;

  // Model matrix: same as normal render
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, m_facing, glm::vec3(0, 0, 1));

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW;

  // Submit body parts
  for (int p = 0; p < PART_COUNT; ++p) {
    if (!m_parts[p].bmd) continue;
    for (auto &mb : m_parts[p].meshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
  // Base head (accessory helms)
  if (m_showBaseHead && m_baseHead.bmd) {
    for (auto &mb : m_baseHead.meshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
  // Weapon
  if (m_weaponBmd) {
    for (auto &mb : m_weaponMeshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
  // Shield
  if (m_shieldBmd) {
    for (auto &mb : m_shieldMeshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
  // Wings
  if (m_wingBmd) {
    for (auto &mb : m_wingMeshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
  // Mount
  if (m_mount.active && m_mount.bmd && m_mount.alpha > 0.0f) {
    for (auto &mb : m_mount.meshBuffers) {
      if (mb.hidden || mb.indexCount == 0) continue;
      bgfx::setTransform(glm::value_ptr(model));
      if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
      else bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);
      bgfx::setState(state);
      bgfx::submit(viewId, depthProgram);
    }
  }
}

void HeroCharacter::ProcessMovement(float deltaTime) {
  if (!m_terrainData || !m_moving || IsDead())
    return;

  glm::vec3 dir = m_target - m_pos;
  dir.y = 0;
  float dist = glm::length(dir);

  if (dist < 10.0f) {
    StopMoving();
  } else {
    dir = glm::normalize(dir);
    m_targetFacing = atan2f(dir.z, -dir.x);
    m_facing = smoothFacing(m_facing, m_targetFacing, deltaTime);

    // Mount speed bonus: Uniria +20%, Dinorant +25%
    float speed = m_speed;
    if (isMountRiding()) {
      if (m_mountEquippedIndex == 3) speed *= 1.25f;      // Dinorant
      else if (m_mountEquippedIndex == 2) speed *= 1.20f;  // Uniria
    }
    // Main 5.2: eDeBuff_Freeze — 50% movement speed when frozen
    if (m_frozen)
      speed *= 0.5f;
    glm::vec3 step = dir * speed * deltaTime;
    glm::vec3 newPos = m_pos + step;

    const int S = TerrainParser::TERRAIN_SIZE;
    float curHeight = m_pos.y;
    auto isWalkableAt = [&](float wx, float wz) -> bool {
      int tgz = (int)(wx / 100.0f);
      int tgx = (int)(wz / 100.0f);
      if (tgx < 0 || tgz < 0 || tgx >= S || tgz >= S)
        return false;
      if (m_terrainData->mapping.attributes[tgz * S + tgx] & 0x04)
        return false;
      // Block walking up steep walls (height step > 50 units)
      float nextH = TerrainUtils::GetHeight(m_terrainData, wx, wz);
      if (nextH - curHeight > 50.0f)
        return false;
      return true;
    };

    // If currently on an unwalkable tile (e.g. snapped to chair), allow escape
    bool currentWalkable = isWalkableAt(m_pos.x, m_pos.z);

    // Wall sliding: try full move, then X-only, then Z-only
    // (Main 5.2 MapPath.cpp: direction fallback when diagonal is blocked)
    if (isWalkableAt(newPos.x, newPos.z)) {
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else if (!currentWalkable) {
      // Stuck on unwalkable tile — force move toward target to escape
      m_pos.x = newPos.x;
      m_pos.z = newPos.z;
    } else if (std::abs(step.x) > 0.01f &&
               isWalkableAt(m_pos.x + step.x, m_pos.z)) {
      m_pos.x += step.x; // Slide along X axis
    } else if (std::abs(step.z) > 0.01f &&
               isWalkableAt(m_pos.x, m_pos.z + step.z)) {
      m_pos.z += step.z; // Slide along Z axis
    } else {
      StopMoving();
    }
  }

  SnapToTerrain();
}

void HeroCharacter::MoveTo(const glm::vec3 &target) {
  if (IsDead())
    return;
  if (m_sittingOrPosing)
    CancelSitPose();
  m_target = target;
  // Only reset walk animation if not already walking
  int walkAction = (isMountRiding() || (!m_inSafeZone && m_weaponBmd))
                       ? weaponWalkAction()
                       : defaultWalkAction();
  if (!m_moving || m_action != walkAction) {
    SetAction(walkAction);
    m_animFrame = 0.0f;
  }
  m_moving = true;
  // Compute target facing angle (smoothFace handles interpolation)
  float dx = target.x - m_pos.x;
  float dz = target.z - m_pos.z;
  m_targetFacing = atan2f(dz, -dx);
}

void HeroCharacter::StopMoving() {
  m_moving = false;
  // Use weapon/mount-specific idle action
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(defaultIdleAction());
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::StartSitPose(bool isSit, float facingAngleDeg,
                                 bool alignToObject,
                                 const glm::vec3 &snapPos) {
  if (IsDead())
    return;
  SoundManager::Play(SOUND_DROP_ITEM01); // Main 5.2: PlayBuffer(SOUND_DROP_ITEM01)
  m_moving = false;
  CancelAttack();
  ClearPendingPickup();
  m_sittingOrPosing = true;

  // Snap character to the object's world position
  // Always snap — the object is the sit/pose target regardless of tile walkability
  m_pos = snapPos;
  m_target = snapPos;
  SnapToTerrain();

  if (alignToObject) {
    // Main 5.2: Object.Angle[2] is the raw MU Z rotation in degrees
    // Convert to our facing angle in radians
    m_facing = facingAngleDeg * (float)(M_PI / 180.0);
    m_targetFacing = m_facing;
  }

  if (isSit) {
    SetAction(ACTION_SIT1);
  } else {
    SetAction(ACTION_POSE1);
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::CancelSitPose() {
  if (!m_sittingOrPosing)
    return;
  m_sittingOrPosing = false;
  // Return to idle
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(defaultIdleAction());
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::SetInSafeZone(bool safe) {
  if (m_inSafeZone == safe)
    return;
  m_inSafeZone = safe;
  // Main 5.2: mount stays loaded in safe zone, alpha fades to 0.
  // Player uses ground animations. Mount auto-restores when leaving.
  // isMountRiding() handles the safe zone check for animation selection.

  // Switch animation: isMountRiding() returns false in safe zone,
  // so weaponIdleAction/weaponWalkAction will return ground animations.
  int safeIdle = defaultIdleAction();
  int safeWalk = defaultWalkAction();
  if (m_moving) {
    SetAction((isMountRiding() || (!safe && m_weaponBmd)) ? weaponWalkAction()
                                                          : safeWalk);
  } else {
    SetAction((isMountRiding() || (!safe && m_weaponBmd)) ? weaponIdleAction()
                                                          : safeIdle);
  }

  std::cout << "[Hero] " << (safe ? "Entered SafeZone" : "Left SafeZone")
            << ", action=" << m_action
            << (m_mount.active ? " (mount fading)" : "") << std::endl;
}

void HeroCharacter::EquipWeapon(const WeaponEquipInfo &weapon) {
  // Skip reload if same weapon already equipped (e.g. equipment refresh from
  // arrow consumption — only slot 1 quantity changed, not slot 0 weapon)
  if (m_weaponBmd && weapon.category == m_weaponInfo.category &&
      weapon.itemIndex == m_weaponInfo.itemIndex &&
      weapon.itemLevel == m_weaponInfo.itemLevel &&
      weapon.category != 0xFF) {
    return;
  }

  // Cleanup old weapon
  CleanupMeshBuffers(m_weaponMeshBuffers);
  CleanupMeshBuffers(m_ghostWeaponMeshBuffers);
  for (auto &sm : m_weaponShadowMeshes) {
    if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
  }
  m_weaponShadowMeshes.clear();
  m_twistingSlashActive = false;

  if (weapon.category == 0xFF) {
    m_weaponBmd.reset();
    m_weaponIsAnimated = false;
    m_weaponAnimContinuous = false;
    m_weaponAnimFrame = 0.0f;
    m_weaponInfo = weapon;
    m_inSafeZone = true;
    SetAction(defaultIdleAction());
    return;
  }

  m_weaponInfo = weapon;
  std::string fullPath = m_dataPath + "/Item/" + weapon.modelFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load weapon: " << fullPath << std::endl;
    return;
  }

  AABB weaponAABB{};
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Item/", {}, m_weaponMeshBuffers,
                        weaponAABB, true);
  }

  // Ghost weapon mesh buffers for Twisting Slash VFX (static bind-pose copy)
  AABB ghostAABB{};
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Item/", {}, m_ghostWeaponMeshBuffers,
                        ghostAABB, true);
  }

  // Shadow meshes for weapon
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_weaponShadowMeshes = createShadowMeshes(bmd.get());

  m_weaponBmd = std::move(bmd);
  m_weaponLocalBones = ComputeBoneMatrices(m_weaponBmd.get());

  // Detect animated weapons (bows/crossbows: attack-synced, staves: continuous loop)
  m_weaponAnimFrame = 0.0f;
  if (!m_weaponBmd->Actions.empty() &&
      m_weaponBmd->Actions[0].NumAnimationKeys > 1) {
    m_weaponIsAnimated = true;
    m_weaponAnimContinuous = (weapon.category != 4); // bows/crossbows sync to attack
  } else {
    m_weaponIsAnimated = false;
    m_weaponAnimContinuous = false;
  }

  // Main 5.2: ItemLight — per-weapon BlendMesh glow assignment
  m_weaponBlendMesh = ItemModelManager::GetItemBlendMesh(
      weapon.category, weapon.itemIndex);
  m_weaponHiddenMesh = ItemModelManager::GetItemHiddenMesh(
      weapon.category, weapon.itemIndex);

  auto &catRender = GetWeaponCategoryRender(weapon.category);
  std::cout << "[Hero] Loaded weapon " << weapon.modelFile << ": "
            << m_weaponBmd->Meshes.size() << " meshes, "
            << m_weaponBmd->Bones.size() << " bones"
            << " (bone=" << (int)catRender.attachBone
            << " idle=" << weaponIdleAction() << " walk=" << weaponWalkAction()
            << " 2H=" << weapon.twoHanded << ")" << std::endl;

  // Update animation to combat stance if outside SafeZone
  // Skip if mid-attack — don't reset attack animation on equipment refresh
  if (!m_inSafeZone && m_attackState == AttackState::NONE) {
    SetAction(m_moving ? weaponWalkAction() : weaponIdleAction());
    m_animFrame = 0.0f;
  }

  std::cout << "[Hero] Weapon equipped: " << weapon.modelFile << " ("
            << m_weaponMeshBuffers.size() << " GPU meshes)" << std::endl;
}

void HeroCharacter::EquipShield(const WeaponEquipInfo &shield) {
  // Cleanup old shield
  CleanupMeshBuffers(m_shieldMeshBuffers);
  for (auto &sm : m_shieldShadowMeshes) {
    if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
  }
  m_shieldShadowMeshes.clear();

  if (shield.category == 0xFF) {
    m_shieldBmd.reset();
    m_shieldInfo = shield;
    return;
  }

  m_shieldInfo = shield;
  std::string fullPath = m_dataPath + "/Item/" + shield.modelFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load shield: " << fullPath << std::endl;
    return;
  }

  AABB shieldAABB{};
  std::string texPath = m_dataPath + "/Item/";
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, {}, m_shieldMeshBuffers, shieldAABB,
                        false);
  }

  // Shadow meshes for shield
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_shieldShadowMeshes = createShadowMeshes(bmd.get());

  m_shieldBmd = std::move(bmd);
  if (!m_shieldBmd->Bones.empty()) {
    m_shieldLocalBones = ComputeBoneMatrices(m_shieldBmd.get());
  } else {
    BoneWorldMatrix identity{};
    identity[0] = {1, 0, 0, 0};
    identity[1] = {0, 1, 0, 0};
    identity[2] = {0, 0, 1, 0};
    m_shieldLocalBones = {identity};
  }

  std::cout << "[Hero] Loaded shield " << shield.modelFile << ": "
            << m_shieldBmd->Meshes.size() << " meshes, "
            << m_shieldBmd->Bones.size() << " bones" << std::endl;

  const auto &shieldBones = m_shieldLocalBones;

  CleanupMeshBuffers(m_shieldMeshBuffers);
  for (auto &mesh : m_shieldBmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, shieldBones, m_shieldMeshBuffers,
                        shieldAABB, true);
  }

  m_shieldBlendMesh = ItemModelManager::GetItemBlendMesh(
      shield.category, shield.itemIndex);
  m_shieldHiddenMesh = ItemModelManager::GetItemHiddenMesh(
      shield.category, shield.itemIndex);

  std::cout << "[Hero] Shield equipped: " << shield.modelFile << " ("
            << m_shieldMeshBuffers.size() << " GPU meshes)" << std::endl;
}

// ── Wing equip/unequip (Main 5.2 RenderCharacterBackItem — bone 47) ──

void HeroCharacter::EquipWings(const WeaponEquipInfo &wing) {
  UnequipWings();

  if (wing.category == 0xFF) {
    m_wingInfo = wing;
    return;
  }

  m_wingInfo = wing;
  std::string fullPath = m_dataPath + "/Item/" + wing.modelFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load wings: " << fullPath << std::endl;
    return;
  }

  AABB wingAABB{};
  std::string texPath = m_dataPath + "/Item/";
  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, {}, m_wingMeshBuffers, wingAABB, false);
  }

  // Shadow meshes
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_wingShadowMeshes = createShadowMeshes(bmd.get());

  m_wingBmd = std::move(bmd);
  if (!m_wingBmd->Bones.empty()) {
    m_wingLocalBones = ComputeBoneMatrices(m_wingBmd.get());
  } else {
    BoneWorldMatrix identity{};
    identity[0] = {1, 0, 0, 0};
    identity[1] = {0, 1, 0, 0};
    identity[2] = {0, 0, 1, 0};
    m_wingLocalBones = {identity};
  }

  // Re-upload with bones pre-applied
  CleanupMeshBuffers(m_wingMeshBuffers);
  for (auto &mesh : m_wingBmd->Meshes) {
    UploadMeshWithBones(mesh, texPath, m_wingLocalBones, m_wingMeshBuffers,
                        wingAABB, true);
  }

  // Main 5.2: Wing05/06 (biped, >60 bones) use standard RENDER_TEXTURE —
  // no additive blending. Only standalone JPEG wings (01-04) need additive
  // to hide black backgrounds. TGA wings (Wing07) have proper alpha.
  bool isBipedWingModel = ((int)m_wingBmd->Bones.size() > MuMath::PLAYER_BONE_COUNT);
  if (!isBipedWingModel) {
    for (auto &mb : m_wingMeshBuffers) {
      if (!mb.hasAlpha)
        mb.bright = true;
    }
  }

  m_wingBlendMesh = ItemModelManager::GetItemBlendMesh(wing.category, wing.itemIndex);
  m_wingAnimFrame = 0.0f;

  std::cout << "[Hero] Wings equipped: " << wing.modelFile << " ("
            << m_wingMeshBuffers.size() << " meshes, "
            << m_wingBmd->Bones.size() << " bones, blendMesh="
            << m_wingBlendMesh << ")" << std::endl;
}

void HeroCharacter::UnequipWings() {
  CleanupMeshBuffers(m_wingMeshBuffers);
  for (auto &sm : m_wingShadowMeshes) {
    if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
  }
  m_wingShadowMeshes.clear();
  m_wingBmd.reset();
  m_wingLocalBones.clear();
  m_wingBlendMesh = -1;
  m_wingAnimFrame = 0.0f;
  m_wingInfo = {};
}

// Main 5.2 ZzzCharacter.cpp:11718 — helm model indices that show the base head
// underneath (accessory helms that don't cover the full face).
// MODEL_HELM + index: 0=Bronze, 2=Pad, 10=Vine, 11=Silk, 12=Wind, 13=Spirit
static bool IsShowHeadHelm(const std::string &helmFile) {
  std::string lower = helmFile;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  // Male01=Bronze(idx0), Male03=Pad(idx2)
  if (lower.find("helmmale01") != std::string::npos) return true;
  if (lower.find("helmmale03") != std::string::npos) return true;
  // Elf01-Elf04 = Vine/Silk/Wind/Spirit (idx 10-13)
  if (lower.find("helmelf01") != std::string::npos) return true;
  if (lower.find("helmelf02") != std::string::npos) return true;
  if (lower.find("helmelf03") != std::string::npos) return true;
  if (lower.find("helmelf04") != std::string::npos) return true;
  return false;
}

void HeroCharacter::EquipBodyPart(int partIndex, const std::string &modelFile,
                                  uint8_t level, uint8_t itemIndex) {
  if (partIndex < 0 || partIndex >= PART_COUNT)
    return;

  m_partLevels[partIndex] = level;
  m_partItemIndices[partIndex] = itemIndex;

  // Default naked body parts for current class
  const char *suffix = GetClassBodySuffix(m_class);
  static const char *partPrefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  char defaultPart[64];
  snprintf(defaultPart, 64, "%s%s.bmd", partPrefixes[partIndex], suffix);

  std::string fileToLoad =
      modelFile.empty() ? defaultPart : modelFile;
  std::string fullPath = m_dataPath + "/Player/" + fileToLoad;

  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Hero] Failed to load body part: " << fullPath << std::endl;
    return;
  }

  // Cleanup old meshes
  CleanupMeshBuffers(m_parts[partIndex].meshBuffers);
  for (auto &sm : m_parts[partIndex].shadowMeshes) {
    if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
  }
  m_parts[partIndex].shadowMeshes.clear();

  // Recompute bones from skeleton bind pose
  auto bones = ComputeBoneMatrices(m_skeleton.get());
  AABB partAABB{};

  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, m_dataPath + "/Player/", bones,
                        m_parts[partIndex].meshBuffers, partAABB, true);
  }

  // Shadow meshes for body part
  static auto createShadowMeshes = [](const BMDData *bmd) {
    std::vector<HeroCharacter::ShadowMesh> meshes;
    if (!bmd)
      return meshes;
    for (auto &mesh : bmd->Meshes) {
      HeroCharacter::ShadowMesh sm;
      sm.vertexCount = mesh.NumTriangles * 3;
      sm.indexCount = sm.vertexCount;
      if (sm.vertexCount == 0) {
        meshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_parts[partIndex].shadowMeshes = createShadowMeshes(bmd.get());

  m_parts[partIndex].bmd = std::move(bmd);

  // For helms (partIndex 0): load base head model underneath accessory helms
  // Main 5.2 ZzzCharacter.cpp:11718 — certain helms show the face
  if (partIndex == 0) {
    // Cleanup old base head
    CleanupMeshBuffers(m_baseHead.meshBuffers);
    for (auto &sm : m_baseHead.shadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    m_baseHead.shadowMeshes.clear();
    m_baseHead.bmd.reset();
    m_showBaseHead = false;

    bool isDefault = modelFile.empty() || fileToLoad == std::string(defaultPart);
    if (!isDefault && IsShowHeadHelm(fileToLoad)) {
      // Load class default head (HelmClassXX.bmd) underneath
      std::string headPath = m_dataPath + "/Player/" + defaultPart;
      auto headBmd = BMDParser::Parse(headPath);
      if (headBmd) {
        AABB headAABB{};
        for (auto &mesh : headBmd->Meshes) {
          UploadMeshWithBones(mesh, m_dataPath + "/Player/", bones,
                              m_baseHead.meshBuffers, headAABB, true);
        }
        m_baseHead.shadowMeshes = createShadowMeshes(headBmd.get());
        m_baseHead.bmd = std::move(headBmd);
        m_showBaseHead = true;
        std::cout << "[Hero] Base head loaded: " << defaultPart << std::endl;
      }
    }
  }

  std::cout << "[Hero] Equipped body part[" << partIndex << "]: " << fileToLoad
            << " (" << m_parts[partIndex].meshBuffers.size() << " GPU meshes)"
            << std::endl;

  CheckFullArmorSet();
}

// Main 5.2 CheckFullSet (ZzzCharacter.cpp:5250-5336):
// All 5 armor pieces must be equipped, same itemIndex, all +10 or higher.
// m_equipmentLevelSet = minimum enhancement level, or 0 if no set bonus.
void HeroCharacter::CheckFullArmorSet() {
  m_equipmentLevelSet = 0;
  m_setGlowColor = glm::vec3(1.0f);

  // Check all 5 parts are equipped (level > 0 means an actual item, not default)
  // Also check all same itemIndex and all +10+
  uint8_t firstIdx = m_partItemIndices[0];
  int minLevel = 99;
  for (int i = 0; i < PART_COUNT; ++i) {
    if (m_partLevels[i] < 10) return;     // Not high enough
    if (m_parts[i].meshBuffers.empty()) return; // Not equipped
    if (m_partItemIndices[i] != firstIdx) return; // Different type
    minLevel = std::min(minLevel, (int)m_partLevels[i]);
  }

  m_equipmentLevelSet = minLevel;

  // Set-specific glow color (Main 5.2 ZzzCharacter.cpp:10608-10614)
  // Based on boots itemIndex (representative of the set)
  switch (firstIdx) {
  case 29: m_setGlowColor = glm::vec3(0.65f, 0.3f, 0.1f);  break; // Orange
  case 30: m_setGlowColor = glm::vec3(0.1f, 0.1f, 0.9f);   break; // Blue
  case 31: m_setGlowColor = glm::vec3(0.0f, 0.32f, 0.24f);  break; // Teal
  case 32: m_setGlowColor = glm::vec3(0.5f, 0.24f, 0.8f);   break; // Purple
  case 33: m_setGlowColor = glm::vec3(0.6f, 0.4f, 0.0f);    break; // Gold
  default: m_setGlowColor = ChromeGlow::GetPartObjectColor(11, firstIdx); break; // Use boots glow
  }
}

void HeroCharacter::AttackMonster(int monsterIndex,
                                  const glm::vec3 &monsterPos) {
  if (IsDead())
    return;
  if (m_sittingOrPosing)
    CancelSitPose();
  if (m_globalAttackCooldown > 0.0f) {
    // GCD active — queue for after cooldown, don't touch active attack state
    m_queuedTarget = monsterIndex;
    m_queuedPos = monsterPos;
    m_queuedSkill = 0;
    return;
  }

  // Already attacking same target — just update position, don't reset cycle
  if (monsterIndex == m_attackTargetMonster && m_activeSkillId == 0 &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

  if (m_attackTargetMonster != monsterIndex)
    m_attackCycleCount = 0; // Reset cycle counter on new target
  m_attackTargetMonster = monsterIndex;
  m_attackTargetPos = monsterPos;
  m_activeSkillId = 0; // Normal attack, no skill

  // Check distance
  glm::vec3 dir = monsterPos - m_pos;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  if (dist <= getAttackRange()) {
    // In range — start swinging
    m_attackState = AttackState::SWINGING;
    m_attackAnimTimer = 0.0f;
    m_attackHitRegistered = false;
    m_forceHitThisFrame = false;
    m_moving = false;

    // Face the target
    m_targetFacing = atan2f(dir.z, -dir.x);

    // Weapon-type-specific attack animation (Main 5.2 SwordCount cycle)
    int act = nextAttackAction();
    SetAction(act);
    // Main 5.2: weapon-type-specific swing sound (ZzzCharacter.cpp:1199-1204)
    if (HasWeapon()) {
      if (m_weaponInfo.category == 4) {
        // Main 5.2: enhanced bows (idx 13,14) use SOUND_MAGIC
        if (m_weaponInfo.itemIndex == 13 || m_weaponInfo.itemIndex == 14)
          SoundManager::Play(SOUND_MAGIC);
        else if (m_weaponInfo.itemIndex >= 8) // Crossbows (idx 8+)
          SoundManager::Play(SOUND_CROSSBOW);
        else
          SoundManager::Play(SOUND_BOW);
      } else if (m_weaponInfo.category == 3 ||
                 (m_weaponInfo.category == 0 && m_weaponInfo.itemIndex == 10)) {
        SoundManager::Play(SOUND_SWING_LIGHT);
      } else {
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
      }
    }

    // Normal melee: weapon blur trail (Main 5.2: BlurType 1, BlurMapping 0)
    // BlurMapping 0 = blur01.OZJ texture, level-based color
    // Skip for bows/crossbows (category 4) — no melee trail
    if (HasWeapon() && m_weaponInfo.category != 4 && m_vfxManager) {
      // Main 5.2 ZzzCharacter.cpp:3752-3774 weapon trail colors
      glm::vec3 trailColor(0.8f, 0.8f, 0.8f); // Default: gray/white
      uint8_t wlvl = m_weaponInfo.itemLevel;
      if (wlvl >= 7)
        trailColor = glm::vec3(1.0f, 0.6f, 0.2f); // Orange-gold
      else if (wlvl >= 5)
        trailColor = glm::vec3(0.2f, 0.4f, 1.0f); // Blue
      else if (wlvl >= 3)
        trailColor = glm::vec3(1.0f, 0.2f, 0.2f); // Red
      // Main 5.2: specific weapon models always use red trail
      if ((m_weaponInfo.category == 0 && m_weaponInfo.itemIndex == 13) || // Sword+13
          (m_weaponInfo.category == 2 && m_weaponInfo.itemIndex == 6) ||  // Mace+6
          (m_weaponInfo.category == 3 && m_weaponInfo.itemIndex == 9))    // Spear+9
        trailColor = glm::vec3(1.0f, 0.2f, 0.2f);
      m_weaponTrailActive = true;
      m_vfxManager->StartWeaponTrail(trailColor, false, m_weaponInfo.itemLevel);
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (act >= 0 && act < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[act].NumAnimationKeys : 1;
    float spd = ANIM_SPEED * attackSpeedMultiplier();
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    float cd = ATTACK_COOLDOWN_TIME / attackSpeedMultiplier();
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;
    m_gcdTargetMonster = monsterIndex; // Track for move-cancel exploit prevention
    m_gcdFromSkill = false; // Melee GCD — can be interrupted by spells
  } else {
    // Out of range — walk toward target
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
  }
}

void HeroCharacter::UpdateAttack(float deltaTime) {
  // Update Twisting Slash ghost weapon effect
  UpdateTwistingSlash(deltaTime);

  // Tick global cooldown (persists after cancel to prevent exploit)
  if (m_globalAttackCooldown > 0.0f) {
    m_globalAttackCooldown -= deltaTime;
    if (m_globalAttackCooldown <= 0.0f) {
      m_globalAttackCooldown = 0.0f;
      m_gcdFromSkill = false;
      m_gcdTargetMonster = -1;

      // GCD expired — execute queued action if any
      if (m_queuedTarget >= 0) {
        int qt = m_queuedTarget;
        glm::vec3 qp = m_queuedPos;
        uint8_t qs = m_queuedSkill;
        m_queuedTarget = -1;
        m_queuedSkill = 0;
        if (qs > 0)
          SkillAttackMonster(qt, qp, qs);
        else
          AttackMonster(qt, qp);
      }
    }
  }

  if (m_attackState == AttackState::NONE)
    return;

  switch (m_attackState) {
  case AttackState::APPROACHING: {
    // Check if we've arrived in range
    glm::vec3 dir = m_attackTargetPos - m_pos;
    dir.y = 0.0f;
    float dist = glm::length(dir);

    if (dist <= getAttackRange()) {
      // Arrived — start swing
      m_moving = false;
      m_attackState = AttackState::SWINGING;
      m_attackAnimTimer = 0.0f;
      m_attackHitRegistered = false;
      m_forceHitThisFrame = false;

      // Face the target — snap for directional VFX (Aqua Beam)
      m_targetFacing = atan2f(dir.z, -dir.x);
      m_facing = m_targetFacing;

      // Skill or weapon-type-specific attack animation
      if (m_activeSkillId > 0) {
        SetAction(GetSkillAction(m_activeSkillId));
        // Play skill sound on approach-to-swing transition (same as in-range path)
        switch (m_activeSkillId) {
        case 19: SoundManager::Play(SOUND_KNIGHT_SKILL1); break;
        case 20: SoundManager::Play(SOUND_KNIGHT_SKILL2); break;
        case 21: SoundManager::Play(SOUND_KNIGHT_SKILL3); break;
        case 22: SoundManager::Play(SOUND_KNIGHT_SKILL4); break;
        case 23: SoundManager::Play(SOUND_KNIGHT_SKILL4); break;
        case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
        case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;
        case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break;
        // DW cast-time sounds (Main 5.2: ZzzCharacter.cpp PlayBuffer)
        case 1:  SoundManager::Play(SOUND_HEART); break;        // Poison
        case 2:  SoundManager::Play(SOUND_METEORITE01); break;  // Meteorite
        case 3:  SoundManager::Play(SOUND_LIGHTNING_CAST); break; // Lightning (Lich electric sound)
        case 4:  SoundManager::Play(SOUND_METEORITE01); break;  // Fire Ball
        case 5:  SoundManager::Play(SOUND_FLAME); break;        // Flame
        case 6:  SoundManager::Play(SOUND_HELLFIRE); break;     // Inferno (Main 5.2: hell variant)
        case 7:  SoundManager::Play(SOUND_ICE); break;          // Ice
        case 8:  if (!SoundManager::IsPlaying(SOUND_STORM)) SoundManager::Play(SOUND_STORM); break; // Twister
        case 9:  if (!SoundManager::IsPlaying(SOUND_EVIL)) SoundManager::Play(SOUND_EVIL); break; // Evil Spirit
        case 10: SoundManager::Play(SOUND_HELLFIRE); break;     // Hellfire
        case 11: SoundManager::Play(SOUND_MAGIC); break;        // Power Wave
        case 12: SoundManager::Play(SOUND_FLASH); break;        // Aqua Beam
        case 13: SoundManager::Play(SOUND_METEORITE01); break;  // Meteor
        case 14: SoundManager::Play(SOUND_MAGIC); break;        // Teleport
        case 17: SoundManager::Play(SOUND_METEORITE01); break;  // Energy Ball
        default:
          if (HasWeapon())
            SoundManager::Play(SOUND_SWING1 + rand() % 2);
          break;
        }
        if (m_vfxManager) {
          m_vfxManager->SpawnSkillCast(m_activeSkillId, m_pos, m_facing, m_attackTargetPos);
          // Twisting Slash: spawn ghost weapon orbit on approach arrival
          if (m_activeSkillId == 41)
            StartTwistingSlash();
          // Spell VFX dispatch (same as SkillAttackMonster in-range path)
          switch (m_activeSkillId) {
          case 17: // Energy Ball
          case 4:  // Fire Ball
            m_vfxManager->SpawnSpellProjectile(m_activeSkillId,
                GetBoneWorldPosition(MuMath::BONE_L_HAND), m_attackTargetPos);
            break;
          case 11: { // Power Wave — MODEL_MAGIC2 ground wave
            float pwFacing = std::atan2(m_attackTargetPos.x - m_pos.x,
                                         m_attackTargetPos.z - m_pos.z);
            m_vfxManager->SpawnPowerWave(m_pos, pwFacing);
            break;
          }
          case 1: // Poison — Main 5.2: MODEL_POISON cloud + 10 smoke at target
            m_vfxManager->SpawnPoisonCloud(m_attackTargetPos);
            break;
          case 7: // Ice — MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
            m_vfxManager->SpawnIceStrike(m_attackTargetPos);
            break;
          case 2: // Meteorite — fireball falls from sky
            m_vfxManager->SpawnMeteorStrike(m_attackTargetPos);
            break;
          case 3: { // Lightning: AT_SKILL_THUNDER — ribbon beams from caster to target
            glm::vec3 castPos = m_pos + glm::vec3(0, 100, 0);
            glm::vec3 hitPos = m_attackTargetPos + glm::vec3(0, 50, 0);
            m_vfxManager->SpawnRibbon(castPos, hitPos, 50.0f,
                                      glm::vec3(0.4f, 0.6f, 1.0f), 0.5f);
            m_vfxManager->SpawnRibbon(castPos, hitPos, 10.0f,
                                      glm::vec3(0.6f, 0.8f, 1.0f), 0.5f);
            m_vfxManager->SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 15);
            SoundManager::Play(SOUND_THUNDER01); // Main 5.2: PlayBuffer(SOUND_THUNDER01)
            break;
          }
          case 13: // Cometfall: AT_SKILL_BLAST — sky-strike at target
            m_vfxManager->SpawnLightningStrike(m_attackTargetPos);
            break;
          case 5: // Flame — persistent ground fire at target
            m_vfxManager->SpawnFlameGround(m_attackTargetPos);
            break;
          case 8: // Twister — tornado travels toward target
            m_vfxManager->SpawnTwisterStorm(m_pos, m_attackTargetPos - m_pos);
            break;
          case 9: // Evil Spirit — 4-directional beams from caster
            m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
            break;
          case 10: // Hellfire — VFX delayed to landing frame
            m_pendingHellfire = true;
            m_hellfireSpawned = false;
            break;
          case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
            m_pendingAquaBeam = true;
            m_aquaBeamSpawned = false;
            m_aquaGatherTimer = 0.0f;
            break;
          }
        }
      } else {
        SetAction(nextAttackAction());
        // Normal attack swing sound on approach arrival
        if (HasWeapon()) {
          if (m_weaponInfo.category == 3 ||
              (m_weaponInfo.category == 0 && m_weaponInfo.itemIndex == 10))
            SoundManager::Play(SOUND_SWING_LIGHT);
          else
            SoundManager::Play(SOUND_SWING1 + rand() % 2);
        }
      }
    } else if (!m_moving) {
      // Stopped moving but not in range (blocked) — cancel
      CancelAttack();
    }
    break;
  }

  case AttackState::SWINGING: {
    // Check if swing animation is done
    int numKeys = 1;
    if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
      numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

    float spdMul = currentSpeedMultiplier();
    float atkAnimSpeed = ANIM_SPEED * spdMul;
    float animDuration = (numKeys > 1) ? (float)numKeys / atkAnimSpeed : 0.5f;
    m_attackAnimTimer += deltaTime;

    // Safety timeout: if stuck in SWINGING for >4 seconds, force cancel
    if (m_attackAnimTimer > 4.0f) {
      std::cout << "[Hero] SWINGING safety timeout (timer=" << m_attackAnimTimer
                << " animDur=" << animDuration << " action=" << m_action
                << " numKeys=" << numKeys << " spdMul=" << spdMul << ")"
                << std::endl;
      CancelAttack();
      break;
    }

    // Flash uses m_animFrame for swing-done check (animation slowdown desyncs timer)
    bool swingDone = (m_activeSkillId == 12)
        ? (m_animFrame >= (float)(numKeys - 1))
        : (m_attackAnimTimer >= animDuration);

    if (swingDone) {
      // If hit wasn't registered during the swing (frame spike skipped the hit
      // window), force-register it now so CheckAttackHit() returns true this frame
      if (!m_attackHitRegistered) {
        m_attackHitRegistered = true;
        m_forceHitThisFrame = true;
      }

      // Stop weapon blur trail on swing end
      if (m_weaponTrailActive && m_vfxManager) {
        m_weaponTrailActive = false;
        m_vfxManager->StopWeaponTrail();
      }
      // Swing finished — go to cooldown (also scaled by attack speed)
      m_attackState = AttackState::COOLDOWN;
      // Spells have shorter cooldown (0.2s base) for smoother casting flow
      float baseCooldown =
          (m_activeSkillId > 0) ? 0.2f : ATTACK_COOLDOWN_TIME;
      m_attackCooldown = baseCooldown / spdMul;

      // If beam never spawned (animation too short), force spawn now
      if (m_pendingAquaBeam && !m_aquaBeamSpawned && m_vfxManager) {
        m_aquaBeamSpawned = true;
        m_vfxManager->SpawnAquaBeam(m_pos, m_facing);
        m_pendingAquaBeam = false;
      }

      // Kill beam VFX when animation ends — beam syncs with hands
      if (m_activeSkillId == 12 && m_vfxManager) {
        m_vfxManager->KillAquaBeams();
      }

      // Return to combat idle (weapon/mount stance or unarmed)
      SetAction((isMountRiding() || m_weaponBmd) ? weaponIdleAction()
                                                 : ACTION_STOP_MALE);
    }
    break;
  }

  case AttackState::COOLDOWN: {
    m_attackCooldown -= deltaTime;
    // Safety timeout: cooldown should never exceed 3 seconds
    if (m_attackCooldown < -3.0f) {
      std::cout << "[Hero] COOLDOWN safety timeout (cooldown="
                << m_attackCooldown << ")" << std::endl;
      CancelAttack();
      break;
    }
    if (m_attackCooldown <= 0.0f) {
      // Auto-attack: if target is still valid, swing again
      if (m_attackTargetMonster >= 0) {
        // Will be re-evaluated from main.cpp which checks if target alive
        // and if monster is actually taking damage (unreachable detection)
        m_attackState = AttackState::NONE;
        m_activeSkillId = 0;
      } else {
        CancelAttack();
      }
    }
    break;
  }

  case AttackState::NONE:
    break;
  }

  // Smoothly rotate towards target facing in any attack state
  m_facing = smoothFacing(m_facing, m_targetFacing, deltaTime);
}

bool HeroCharacter::CheckAttackHit() {
  // Force-hit: swing completed without registering a hit (frame spike skipped
  // the hit window). The flag is set in UpdateAttack's SWINGING→COOLDOWN
  // transition and consumed here so main.cpp can process the hit.
  if (m_forceHitThisFrame) {
    m_forceHitThisFrame = false;
    return true;
  }

  if (m_attackState != AttackState::SWINGING || m_attackHitRegistered)
    return false;

  int numKeys = 1;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

  float atkAnimSpeed = ANIM_SPEED * currentSpeedMultiplier();
  float animDuration = (numKeys > 1) ? (float)numKeys / atkAnimSpeed : 0.5f;
  float hitTime = animDuration * ATTACK_HIT_FRACTION;

  if (m_attackAnimTimer >= hitTime) {
    m_attackHitRegistered = true;
    return true;
  }
  return false;
}

void HeroCharacter::CancelAttack() {
  // GCD already set when swing started — don't reduce it on cancel

  // Stop weapon blur trail if active
  if (m_weaponTrailActive && m_vfxManager) {
    m_weaponTrailActive = false;
    m_vfxManager->StopWeaponTrail();
  }

  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  m_activeSkillId = 0;
  m_swordSwingCount = 0;
  m_attackCycleCount = 0;
  m_forceHitThisFrame = false;
  m_moving = false; // Stop any approach movement
  m_pendingAquaBeam = false; // Clear pending beam on cancel
  m_aquaBeamSpawned = false;
  m_pendingHellfire = false;
  m_hellfireSpawned = false;
  m_aquaPacketReady = false; // Don't send damage if cancelled
  // Return to appropriate idle
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(defaultIdleAction());
  }
}

int HeroCharacter::GetSkillAction(uint8_t skillId) {
  switch (skillId) {
  // DK skills
  case 19:
    return ACTION_SKILL_SWORD1; // Falling Slash
  case 20:
    return ACTION_SKILL_SWORD2; // Lunge
  case 21:
    return ACTION_SKILL_SWORD3; // Uppercut
  case 22:
    return ACTION_SKILL_SWORD4; // Cyclone
  case 23:
    return ACTION_SKILL_SWORD5; // Slash
  case 41:
    return ACTION_SKILL_WHEEL; // Twisting Slash
  case 42:
    return ACTION_SKILL_FURY; // Rageful Blow
  case 43:
    return ACTION_SKILL_DEATH_STAB; // Death Stab
  // DW spells
  case 17:
    return ACTION_SKILL_HAND1; // Energy Ball
  case 4:
    // Fire Ball — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 1:
    // Poison — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 3:
    // Lightning — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 2:
    // Meteorite — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 7:
    // Ice — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 5:
    // Flame — Main 5.2: SetPlayerMagic() randomly picks HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 8:
    // Twister — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 6:
    return ACTION_SKILL_TELEPORT; // Teleport
  case 9:
    // Evil Spirit — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 12:
    return ACTION_SKILL_FLASH; // Aqua Beam
  case 10:
    return ACTION_SKILL_HELL; // Hellfire
  case 11:
    // Power Wave — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 13:
    // Cometfall — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 14:
    return ACTION_SKILL_INFERNO; // Inferno (self-centered AoE)
  // Elf summon skills (30-35) — simple magic hand cast
  case 30: case 31: case 32: case 33: case 34: case 35:
    return ACTION_SKILL_HAND1;
  default:
    return ACTION_SKILL_SWORD1; // Fallback
  }
}

void HeroCharacter::SkillAttackMonster(int monsterIndex,
                                       const glm::vec3 &monsterPos,
                                       uint8_t skillId) {
  if (IsDead())
    return;
  if (m_sittingOrPosing)
    CancelSitPose();
  // Skills can interrupt melee GCD (but not other skill GCDs)
  if (m_globalAttackCooldown > 0.0f) {
    if (m_gcdFromSkill) {
      // Skill GCD active — queue, don't interrupt
      m_queuedTarget = monsterIndex;
      m_queuedPos = monsterPos;
      m_queuedSkill = skillId;
      return;
    }
    // Melee GCD — interrupt it for the skill
    m_globalAttackCooldown = 0.0f;
    CancelAttack();
  }

  // Already swinging same target with same skill — just update position
  if (monsterIndex == m_attackTargetMonster && m_activeSkillId == skillId &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

  m_attackTargetMonster = monsterIndex;
  m_attackTargetPos = monsterPos;
  m_activeSkillId = skillId;

  glm::vec3 dir = monsterPos - m_pos;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  int skillAction = GetSkillAction(skillId);
  std::cout << "[Skill] SkillAttackMonster: monIdx=" << monsterIndex
            << " skillId=" << (int)skillId << " action=" << skillAction
            << " dist=" << dist << " range=" << getAttackRange() << std::endl;

  if (dist <= getAttackRange()) {
    m_attackState = AttackState::SWINGING;
    m_attackAnimTimer = 0.0f;
    m_attackHitRegistered = false;
    m_moving = false;
    m_targetFacing = atan2f(dir.z, -dir.x);
    m_facing = m_targetFacing; // Snap facing for directional VFX (Aqua Beam)

    SetAction(skillAction);
    // Skill-specific sounds (Main 5.2: PlaySkillSound)
    switch (skillId) {
    case 19: SoundManager::Play(SOUND_KNIGHT_SKILL1); break; // Falling Slash
    case 20: SoundManager::Play(SOUND_KNIGHT_SKILL2); break; // Lunge
    case 21: SoundManager::Play(SOUND_KNIGHT_SKILL3); break; // Uppercut
    case 22: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Cyclone
    case 23: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Slash (same as Cyclone)
    case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
    case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;    // Rageful Blow
    case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break; // Death Stab (same as Lunge)
    // DW cast-time sounds (Main 5.2: ZzzCharacter.cpp PlayBuffer)
    case 1:  SoundManager::Play(SOUND_HEART); break;        // Poison
    case 2:  SoundManager::Play(SOUND_METEORITE01); break;  // Meteorite
    case 3:  SoundManager::Play(SOUND_LIGHTNING_CAST); break; // Lightning (Lich electric sound)
    case 4:  SoundManager::Play(SOUND_METEORITE01); break;  // Fire Ball
    case 5:  SoundManager::Play(SOUND_FLAME); break;        // Flame
    case 7:  SoundManager::Play(SOUND_ICE); break;          // Ice
    case 8:  if (!SoundManager::IsPlaying(SOUND_STORM)) SoundManager::Play(SOUND_STORM); break; // Twister
    case 9:  if (!SoundManager::IsPlaying(SOUND_EVIL)) SoundManager::Play(SOUND_EVIL); break;   // Evil Spirit
    case 10: SoundManager::Play(SOUND_HELLFIRE); break;     // Hellfire
    case 12: SoundManager::Play(SOUND_FLASH); break;        // Aqua Beam
    case 17: SoundManager::Play(SOUND_METEORITE01); break;  // Energy Ball
    default:
      if (HasWeapon())
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
      break;
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
    float spdMul = currentSpeedMultiplier();
    float spd = ANIM_SPEED * spdMul;
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
    if (skillId == 12)
      animDur += 2.0f / spd;
    float cd = 0.2f / spdMul; // Spell cooldown = 0.2s base
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;
    m_gcdFromSkill = true; // Skill GCD — cannot be bypassed by move-cancel

    if (m_vfxManager) {
      m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing, monsterPos);

      // DK melee skills: weapon blur trail (Main 5.2: BlurType 1, BlurMapping 2)
      // BlurMapping 2 = motion_blur_r.OZJ texture, WHITE color (1,1,1)
      // Note: Twisting Slash (41) has NO weapon trail in Main 5.2
      if (skillId >= 19 && skillId <= 23) {
        if (HasWeapon()) {
          m_weaponTrailActive = true;
          m_vfxManager->StartWeaponTrail(glm::vec3(1.0f, 1.0f, 1.0f), true,
                                          m_weaponInfo.itemLevel);
        }
      }
      // Twisting Slash: spawn ghost weapon orbit effect
      if (skillId == 41)
        StartTwistingSlash();

      // Spell VFX: dispatch by skill ID (not class — server authorizes skills)
      switch (skillId) {
      case 17: // Energy Ball: traveling BITMAP_ENERGY projectile
      case 4:  // Fire Ball: traveling MODEL_FIRE projectile
        m_vfxManager->SpawnSpellProjectile(skillId,
            GetBoneWorldPosition(MuMath::BONE_L_HAND), monsterPos);
        break;
      case 11: { // Power Wave — MODEL_MAGIC2 ground wave toward target
        float pwFacing = std::atan2(monsterPos.x - m_pos.x,
                                     monsterPos.z - m_pos.z);
        m_vfxManager->SpawnPowerWave(m_pos, pwFacing);
        break;
      }
      case 1: // Poison — Main 5.2: MODEL_POISON cloud + 10 smoke at target
        m_vfxManager->SpawnPoisonCloud(monsterPos);
        break;
      case 7: // Ice — MODEL_ICE crystal + 5x MODEL_ICE_SMALL debris
        m_vfxManager->SpawnIceStrike(monsterPos);
        break;
      case 2: // Meteorite — fireball falls from sky
        m_vfxManager->SpawnMeteorStrike(monsterPos);
        break;
      case 3: { // Lightning: AT_SKILL_THUNDER — ribbon beams from caster to target
        glm::vec3 castPos = m_pos + glm::vec3(0, 100, 0);
        glm::vec3 hitPos = monsterPos + glm::vec3(0, 50, 0);
        m_vfxManager->SpawnRibbon(castPos, hitPos, 50.0f,
                                  glm::vec3(0.4f, 0.6f, 1.0f), 0.5f);
        m_vfxManager->SpawnRibbon(castPos, hitPos, 10.0f,
                                  glm::vec3(0.6f, 0.8f, 1.0f), 0.5f);
        m_vfxManager->SpawnBurst(ParticleType::SPELL_LIGHTNING, hitPos, 15);
        SoundManager::Play(SOUND_THUNDER01); // Main 5.2: PlayBuffer(SOUND_THUNDER01)
        break;
      }
      case 13: // Cometfall: AT_SKILL_BLAST — sky-strike at target
        m_vfxManager->SpawnLightningStrike(monsterPos);
        break;
      case 5: // Flame — persistent ground fire at target
        m_vfxManager->SpawnFlameGround(monsterPos);
        break;
      case 8: // Twister — tornado travels toward target
        m_vfxManager->SpawnTwisterStorm(m_pos, monsterPos - m_pos);
        break;
      case 9: // Evil Spirit — 4-directional beams from caster
        m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
        break;
      case 10: // Hellfire — ground fire ring (beams at blast phase)
        m_vfxManager->SpawnHellfire(m_pos);
        break;
      case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
        m_pendingAquaBeam = true;
        m_aquaBeamSpawned = false;
        m_aquaGatherTimer = 0.0f;
        break;
      case 14: // Inferno — ring of fire explosions around caster
        m_vfxManager->SpawnInferno(m_pos);
        break;
      }
    }
    std::cout << "[Skill] Started SWINGING with action " << skillAction
              << std::endl;
  } else {
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
    std::cout << "[Skill] APPROACHING target (too far)" << std::endl;
  }
}

void HeroCharacter::CastSelfAoE(uint8_t skillId, const glm::vec3 &targetPos) {
  if (IsDead())
    return;
  // Block if GCD is from a previous skill cast (persists after move-cancel)
  if (m_globalAttackCooldown > 0.0f && m_gcdFromSkill)
    return;
  // Cancel any in-progress melee attack and reset melee GCD
  m_globalAttackCooldown = 0.0f;
  CancelAttack();
  m_moving = false;
  m_activeSkillId = skillId;
  m_attackTargetMonster = -1; // No specific target

  // Face toward target — snap immediately for directional spells (Aqua Beam)
  glm::vec3 dir = targetPos - m_pos;
  dir.y = 0.0f;
  if (glm::length(dir) > 0.01f) {
    m_targetFacing = atan2f(dir.z, -dir.x);
    m_facing = m_targetFacing;
  }

  int skillAction = GetSkillAction(skillId);

  // Play cast animation
  m_attackState = AttackState::SWINGING;
  m_attackAnimTimer = 0.0f;
  m_attackHitRegistered = true; // No hit to register (AoE handled by server)
  SetAction(skillAction);

  // Skill-specific sounds (Main 5.2: AttackStage)
  switch (skillId) {
  case 41: SoundManager::Play(SOUND_KNIGHT_SKILL4); break; // Twisting Slash
  case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;    // Rageful Blow
  // DW cast-time sounds (Main 5.2: ZzzCharacter.cpp PlayBuffer)
  case 1:  SoundManager::Play(SOUND_HEART); break;        // Poison
  case 2:  SoundManager::Play(SOUND_METEORITE01); break;  // Meteorite
  case 3:  SoundManager::Play(SOUND_LIGHTNING_CAST); break; // Lightning (Lich electric sound)
  case 4:  SoundManager::Play(SOUND_METEORITE01); break;  // Fire Ball
  case 5:  SoundManager::Play(SOUND_FLAME); break;        // Flame
  case 7:  SoundManager::Play(SOUND_ICE); break;          // Ice
  case 8:  if (!SoundManager::IsPlaying(SOUND_STORM)) SoundManager::Play(SOUND_STORM); break; // Twister
  case 9:  if (!SoundManager::IsPlaying(SOUND_EVIL)) SoundManager::Play(SOUND_EVIL); break;   // Evil Spirit
  case 10: SoundManager::Play(SOUND_HELLFIRE); break;     // Hellfire
  case 12: SoundManager::Play(SOUND_FLASH); break;        // Aqua Beam
  case 17: SoundManager::Play(SOUND_METEORITE01); break;  // Energy Ball
  default: break;
  }

  // Set GCD
  int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
               ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
  float spdMul = currentSpeedMultiplier();
  float spd = ANIM_SPEED * spdMul;
  float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
  // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
  if (skillId == 12)
    animDur += 2.0f / spd;
  float cd = 0.2f / spdMul;
  m_globalAttackCooldown = animDur + cd;
  m_globalAttackCooldownMax = m_globalAttackCooldown;
  m_gcdFromSkill = true; // Skill GCD — cannot be bypassed by move-cancel

  // Spawn VFX
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing, targetPos);

    // Twisting Slash: ghost weapon orbit (Main 5.2: no weapon trail, just ghosts)
    if (skillId == 41 && HasWeapon())
      StartTwistingSlash();

    switch (skillId) {
    case 8: // Twister — tornado travels toward click direction
      m_vfxManager->SpawnTwisterStorm(m_pos, targetPos - m_pos);
      break;
    case 9: // Evil Spirit — 4-directional beams from caster
      m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
      break;
    case 10: // Hellfire — VFX delayed to landing frame
      m_pendingHellfire = true;
      m_hellfireSpawned = false;
      break;
    case 12: // Aqua Beam — delayed: beam spawns at anim frame 5.5
      m_pendingAquaBeam = true;
      m_aquaBeamSpawned = false;
      m_aquaGatherTimer = 0.0f;
      break;
    case 14: // Inferno — ring of fire explosions around caster
      m_vfxManager->SpawnInferno(m_pos);
      break;
    }
  }

  std::cout << "[Skill] CastSelfAoE: skillId=" << (int)skillId
            << " action=" << skillAction << std::endl;
}

void HeroCharacter::TeleportTo(const glm::vec3 &target) {
  if (IsDead())
    return;
  if (m_globalAttackCooldown > 0.0f)
    return;

  // Cancel any in-progress attack/movement
  CancelAttack();
  m_moving = false;

  // Main 5.2: teleport does NOT dismount. Mount persists, just teleports with player.

  // VFX: white rising sparks at origin and destination (Main 5.2)
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(6, m_pos, m_facing);
    m_vfxManager->SpawnSkillImpact(6, target);
  }
  SoundManager::Play(SOUND_MAGIC); // Main 5.2: PlayBuffer(SOUND_MAGIC)

  // No animation change — instant teleport, keep current pose
  // Fixed 1.5s GCD to prevent spam
  m_globalAttackCooldown = 1.5f;
  m_globalAttackCooldownMax = m_globalAttackCooldown;
  m_gcdFromSkill = true; // Skill GCD — cannot be bypassed by move-cancel

  // Instantly move to target position
  m_pos = target;
  m_target = target;
  SnapToTerrain();
}

void HeroCharacter::ApplyHitReaction() {
  // Only trigger if alive (don't interrupt dying/dead)
  if (m_heroState != HeroState::ALIVE && m_heroState != HeroState::HIT_STUN)
    return;

  m_heroState = HeroState::HIT_STUN;
  m_stateTimer = HIT_STUN_TIME;
  m_moving = false; // Stop sliding when playing hit reaction
  // Brief shock animation — don't interrupt attack swing
  if (m_attackState != AttackState::SWINGING) {
    SetAction(ACTION_SHOCK);
  }
}

void HeroCharacter::TakeDamage(int damage) {
  // Accept damage when ALIVE or HIT_STUN (so rapid hits can kill)
  if (m_heroState != HeroState::ALIVE && m_heroState != HeroState::HIT_STUN)
    return;

  m_hp -= damage;
  m_damageFlashTimer = 0.15f; // Brief white flash on hit (Main 5.2 visual feedback)
  if (m_hp <= 0) {
    ForceDie();
  } else {
    ApplyHitReaction();
  }
}

void HeroCharacter::ForceDie() {
  m_hp = 0;
  m_heroState = HeroState::DYING;
  m_stateTimer = 0.0f;
  CancelAttack();
  m_moving = false;
  SetAction(ACTION_DIE1);
  // Elf (class 32) uses female death scream
  if (m_class == 32)
    SoundManager::Play(SOUND_FEMALE_SCREAM1);
  else
    SoundManager::Play(SOUND_MALE_DIE);
  std::cout << "[Hero] Dying (Forced) — action=" << ACTION_DIE1
            << " numActions="
            << (m_skeleton ? (int)m_skeleton->Actions.size() : 0) << std::endl;
}

// ── Twisting Slash ghost weapon effect (Main 5.2: MODEL_SKILL_WHEEL) ──

void HeroCharacter::StartTwistingSlash() {
  if (m_ghostWeaponMeshBuffers.empty())
    return; // No weapon equipped
  m_twistingSlashActive = true;
  m_wheelSpawnTimer = 0.0f;
  m_wheelSpawnCount = 0;
  m_wheelSmokeTimer = 0.0f;
  for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i)
    m_wheelGhosts[i].active = false;
}

void HeroCharacter::UpdateTwistingSlash(float dt) {
  if (!m_twistingSlashActive)
    return;

  // Main 5.2: WHEEL1 lives 5 ticks, spawns 1 WHEEL2 per tick with SubType=4-LifeTime
  // SubType 0 has no alpha case (first spawn), SubType 1-4 = 0.6, 0.5, 0.4, 0.3
  m_wheelSpawnTimer += dt;
  while (m_wheelSpawnTimer >= 0.04f && m_wheelSpawnCount < MAX_WHEEL_GHOSTS) {
    m_wheelSpawnTimer -= 0.04f;
    auto &g = m_wheelGhosts[m_wheelSpawnCount];
    g.active = true;
    // Main 5.2: all WHEEL2 inherit same initial angle from WHEEL1 (= player facing).
    // Natural 18° stagger emerges from time-staggered spawning + 450°/sec rotation.
    g.orbitAngle = 0.0f;
    g.spinAngle = 0.0f;
    g.spinVelocity = 0.0f;
    // Main 5.2: SubType = 4-LifeTime → spawns -1,0,1,2,3
    // SubType -1,0 have no alpha case → default 1.0 (fully opaque)
    // SubType 1=0.6, 2=0.5, 3=0.4
    static constexpr float alphas[5] = {1.0f, 1.0f, 0.6f, 0.5f, 0.4f};
    g.alpha = alphas[m_wheelSpawnCount];
    g.lifetime = 1.0f; // 25 ticks at 25fps
    m_wheelSpawnCount++;
  }

  // Update each active ghost
  bool anyActive = false;
  m_wheelSmokeTimer += dt;
  bool spawnParticles = m_wheelSmokeTimer >= 0.04f;
  for (int i = 0; i < MAX_WHEEL_GHOSTS; ++i) {
    auto &g = m_wheelGhosts[i];
    if (!g.active)
      continue;
    g.lifetime -= dt;
    if (g.lifetime <= 0.0f) {
      g.active = false;
      continue;
    }
    anyActive = true;
    // Main 5.2: orbital rotation = Angle[2] -= 18 per tick = 450°/sec
    g.orbitAngle -= 450.0f * dt;
    // Main 5.2: RenderWheelWeapon does Direction[2] -= 30 per render frame (cumulative)
    // This is acceleration: 30°/frame * 25fps = 750°/sec² acceleration
    g.spinVelocity -= 750.0f * dt;
    g.spinAngle += g.spinVelocity * dt;

    if (m_vfxManager && spawnParticles) {
      float orbitRad = glm::radians(g.orbitAngle);
      glm::vec3 ghostPos = m_pos + glm::vec3(sinf(orbitRad) * 150.0f, 100.0f,
                                               cosf(orbitRad) * 150.0f);
      // Main 5.2: CreateParticle(BITMAP_SMOKE) + 4x JOINT_SPARK + 1x SPARK
      // + CreateSprite(BITMAP_LIGHT) warm glow per tick per ghost
      m_vfxManager->SpawnBurst(ParticleType::SMOKE, ghostPos, 1);
      m_vfxManager->SpawnBurst(ParticleType::HIT_SPARK, ghostPos, 3);
      m_vfxManager->SpawnBurst(ParticleType::FLARE, ghostPos, 1);
    }
  }
  if (spawnParticles)
    m_wheelSmokeTimer -= 0.04f;

  if (!anyActive && m_wheelSpawnCount >= MAX_WHEEL_GHOSTS)
    m_twistingSlashActive = false;
}

void HeroCharacter::UpdateState(float deltaTime) {
  switch (m_heroState) {
  case HeroState::ALIVE:
    // HP Regeneration in Safe Zone (~2% of Max HP per second)
    if (m_inSafeZone && m_hp < m_maxHp) {
      m_hpRemainder += 0.02f * (float)m_maxHp * deltaTime;
      float threshold = std::max(1.0f, 0.02f * (float)m_maxHp);
      if (m_hpRemainder >= threshold) {
        int gain = (int)m_hpRemainder;
        m_hp = std::min(m_hp + gain, m_maxHp);
        m_hpRemainder -= (float)gain;
        std::cout << "[Regen] Hero healed +" << gain
                  << " HP in SafeZone (Local). New HP: " << m_hp << "/"
                  << m_maxHp << std::endl;
      }
    } else {
      m_hpRemainder = 0.0f;
    }
    break; // Normal operation
  case HeroState::HIT_STUN:
    m_stateTimer -= deltaTime;
    if (m_stateTimer <= 0.0f) {
      m_heroState = HeroState::ALIVE;
      // Return to appropriate idle if not attacking/moving/sitting
      if (m_attackState == AttackState::NONE && !m_moving &&
          !m_sittingOrPosing) {
        if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
          SetAction(weaponIdleAction());
        } else {
          SetAction(defaultIdleAction());
        }
      }
    }
    break;
  case HeroState::DYING: {
    // Play die animation to completion, then transition to DEAD
    m_stateTimer += deltaTime; // Count up as safety timeout
    int numKeys = 1;
    if (ACTION_DIE1 < (int)m_skeleton->Actions.size())
      numKeys = m_skeleton->Actions[ACTION_DIE1].NumAnimationKeys;
    if (m_animFrame >= (float)(numKeys - 1) || m_stateTimer > 3.0f) {
      m_animFrame = (float)(numKeys - 1); // Freeze on last frame
      m_heroState = HeroState::DEAD;
      m_stateTimer = DEAD_WAIT_TIME;
      std::cout << "[Hero] Now DEAD, respawn in " << DEAD_WAIT_TIME << "s"
                << std::endl;
    }
    break;
  }
  case HeroState::DEAD:
    m_stateTimer -= deltaTime;
    // Respawn is triggered externally by main.cpp after timer expires
    break;
  case HeroState::RESPAWNING:
    // Brief invuln after respawn — return to ALIVE after timer
    m_stateTimer -= deltaTime;
    if (m_stateTimer <= 0.0f)
      m_heroState = HeroState::ALIVE;
    break;
  }

  // Final step: ensure we are always snapped to the ground heights
  if (m_heroState != HeroState::DYING && m_heroState != HeroState::DEAD) {
    SnapToTerrain();
  }
}

void HeroCharacter::Respawn(const glm::vec3 &spawnPos) {
  m_pos = spawnPos;
  SnapToTerrain();
  m_hp = m_maxHp;
  m_heroState = HeroState::RESPAWNING;
  m_stateTimer = 2.0f; // 2 seconds invulnerability
  m_moving = false;
  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  m_gcdTargetMonster = -1;
  m_globalAttackCooldown = 0.0f;
  m_activeSkillId = 0;
  m_attackHitRegistered = false;
  m_swordSwingCount = 0;
  // Return to idle (Elf uses female idle)
  if (isMountRiding() || (!m_inSafeZone && m_weaponBmd)) {
    SetAction(weaponIdleAction());
  } else {
    SetAction((m_class == 32) ? ACTION_STOP_FEMALE : ACTION_STOP_MALE);
  }
}

void HeroCharacter::SnapToTerrain() {
  m_pos.y = TerrainUtils::GetHeight(m_terrainData, m_pos.x, m_pos.z);
}

void HeroCharacter::SetAction(int newAction) {
  if (m_action == newAction)
    return;

  // Cross-fade blending for smooth transitions:
  // - Fist attack transitions
  // - Walk -> idle (stopping)
  // - Attack/skill -> combat idle (weapon attacks / spells finishing)
  // - Idle -> skill (entering spell cast animation)
  bool involvesFists =
      (m_action == ACTION_ATTACK_FIST || newAction == ACTION_ATTACK_FIST);

  // Detect walk actions (15-23) and stop/idle actions (0-10)
  bool isWalkingAction = (m_action >= 15 && m_action <= 23);
  bool isNewStop = (newAction >= 0 && newAction <= 10);
  bool isStopping = (isWalkingAction && isNewStop);

  // Attack/skill -> idle blend (weapon attacks 38-51, DK skills 60-74, magic 146-154)
  bool isAttackAction =
      (m_action >= 38 && m_action <= 51) || (m_action >= 60 && m_action <= 74) ||
      (m_action >= 146 && m_action <= 154);
  bool isAttackToIdle = (isAttackAction && isNewStop);

  // Idle -> skill/attack blend (smooth entry into spell cast animations)
  bool isCurrentStop = (m_action >= 0 && m_action <= 10);
  bool isNewSkill = (newAction >= 60 && newAction <= 74) ||
                    (newAction >= 146 && newAction <= 154);
  bool isIdleToSkill = (isCurrentStop && isNewSkill);

  if (involvesFists || isStopping || isAttackToIdle || isIdleToSkill) {
    m_priorAction = m_action;
    m_priorAnimFrame = m_animFrame;
    m_isBlending = true;
    m_blendAlpha = 0.0f;
  } else {
    m_isBlending = false;
    m_blendAlpha = 1.0f;
  }

  m_action = newAction;
  m_animFrame = 0.0f;
  m_idleReversing = false; // New idle always starts playing forward

}


void HeroCharacter::Cleanup() {
  auto cleanupShadows = [](std::vector<ShadowMesh> &shadowMeshes) {
    for (auto &sm : shadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    shadowMeshes.clear();
  };

  for (int p = 0; p < PART_COUNT; ++p) {
    CleanupMeshBuffers(m_parts[p].meshBuffers);
    cleanupShadows(m_parts[p].shadowMeshes);
  }
  CleanupMeshBuffers(m_baseHead.meshBuffers);
  cleanupShadows(m_baseHead.shadowMeshes);
  m_baseHead.bmd.reset();

  CleanupMeshBuffers(m_weaponMeshBuffers);
  CleanupMeshBuffers(m_ghostWeaponMeshBuffers);
  cleanupShadows(m_weaponShadowMeshes);
  m_weaponBmd.reset();

  CleanupMeshBuffers(m_shieldMeshBuffers);
  cleanupShadows(m_shieldShadowMeshes);
  m_shieldBmd.reset();

  // Wings
  CleanupMeshBuffers(m_wingMeshBuffers);
  cleanupShadows(m_wingShadowMeshes);
  m_wingBmd.reset();

  // Mount
  CleanupMeshBuffers(m_mount.meshBuffers);
  cleanupShadows(m_mount.shadowMeshes);
  m_mount.cachedBones.clear();
  m_mount.bmd.reset();
  m_mount.active = false;

  // Pet companion
  CleanupMeshBuffers(m_pet.meshBuffers);
  m_pet.bmd.reset();
  m_pet.active = false;

  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
