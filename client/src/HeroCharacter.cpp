#include "HeroCharacter.hpp"
#include "ItemModelManager.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
  // MaxHP = 110 + 2.0*(Level-1) + (VIT-25)*3.0
  m_maxHp = (int)(DK_BASE_HP + DK_LEVEL_LIFE * (m_level - 1) +
                  (m_vitality - DK_BASE_VIT) * DK_VIT_TO_LIFE);
  if (m_maxHp < 1)
    m_maxHp = 1;

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

  // Damage = STR / 6 + weapon .. STR / 4 + weapon (OpenMU DK formula)
  m_damageMin = std::max(1, (int)m_strength / 6 + m_weaponDamageMin);
  m_damageMax = std::max(m_damageMin, (int)m_strength / 4 + m_weaponDamageMax);

  // Defense = DEX / 3 + equipped armor/shield defense
  m_defense = (int)m_dexterity / 3 + m_equipDefenseBonus;

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
  RecalcStats();
}

void HeroCharacter::SetDefenseBonus(int def) {
  m_equipDefenseBonus = def;
  RecalcStats();
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
  const int SIZE = 256;
  if (m_terrainLightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  float gz = worldPos.x / 100.0f;
  float gx = worldPos.z / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = m_terrainLightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = m_terrainLightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = m_terrainLightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = m_terrainLightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
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
  // DK with weapon in right hand AND weapon (not shield) in left hand
  if (m_weaponInfo.category == 0xFF || m_shieldInfo.category == 0xFF)
    return false;
  return m_shieldInfo.category != 6; // Left hand has a weapon, not a shield
}

int HeroCharacter::weaponIdleAction() const {
  if (!m_weaponBmd)
    return ACTION_STOP_MALE;

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
  if (!m_weaponBmd)
    return ACTION_WALK_MALE;

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
  case 5: // Staff — use fist attack for melee (magic is separate)
    return ACTION_ATTACK_FIST;
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
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
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
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

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
  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

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
    bool isAttacking = (m_action >= 38 && m_action <= 51) ||
                       (m_action >= 60 && m_action <= 71) ||
                       (m_action >= 146 && m_action <= 154);
    float speed;
    if (isHealAnim)
      speed = (float)numKeys / m_slowAnimDuration; // Stretch to fit duration
    else if (isAttacking)
      speed = ANIM_SPEED * attackSpeedMultiplier();
    else
      speed = ANIM_SPEED;
    // Main 5.2: Flash animation slowdown during gathering phase (frames 1.0-3.0)
    // PlaySpeed /= 2 creates dramatic wind-up before beam release
    if (m_activeSkillId == 12 && m_animFrame >= 1.0f && m_animFrame < 3.0f)
      speed *= 0.5f;

    m_animFrame += speed * deltaTime;
    if (clampAnim) {
      if (m_animFrame >= (float)(numKeys - 1))
        m_animFrame = (float)(numKeys - 1);
    } else {
      int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
      if (m_animFrame >= (float)wrapKeys)
        m_animFrame = std::fmod(m_animFrame, (float)wrapKeys);
    }

    // Main 5.2: footstep sounds at animation frames 1.5 and 4.5 during walk
    // City (safe zone) uses soil footsteps, fields use grass
    // Slight pitch variation (0.9-1.1) so steps don't sound identical
    if (m_moving) {
      int walkSound = m_inSafeZone ? SOUND_WALK_SOIL : SOUND_WALK_GRASS;
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

  // Handle cross-fade blending animation
  if (m_isBlending) {
    m_blendAlpha += deltaTime / BLEND_DURATION;
    if (m_blendAlpha >= 1.0f) {
      m_blendAlpha = 1.0f;
      m_isBlending = false;
    }
  }

  // Compute bones for current animation frame
  std::vector<BoneWorldMatrix> bones;
  if (m_isBlending && m_priorAction != -1) {
    bones = ComputeBoneMatricesBlended(m_skeleton.get(), m_priorAction,
                                       m_priorAnimFrame, m_action, m_animFrame,
                                       m_blendAlpha);
  } else {
    bones = ComputeBoneMatricesInterpolated(m_skeleton.get(), m_action,
                                            m_animFrame);
  }

  // LockPositions: root bone X/Y locked to frame 0
  if (m_rootBone >= 0) {
    int i = m_rootBone;

    float dx = 0.0f, dy = 0.0f;

    if (m_isBlending && m_priorAction != -1) {
      // Blend root offsets from both actions if they have lockPos
      bool lock1 = false, lock2 = false;
      if (m_priorAction < (int)m_skeleton->Actions.size())
        lock1 = m_skeleton->Actions[m_priorAction].LockPositions;
      if (m_action < (int)m_skeleton->Actions.size())
        lock2 = m_skeleton->Actions[m_action].LockPositions;

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
    } else if (lockPos) {
      // Standard single-action lock
      auto &bm = m_skeleton->Bones[i].BoneMatrixes[m_action];
      if (!bm.Position.empty()) {
        dx = bones[i][0][3] - bm.Position[0].x;
        dy = bones[i][1][3] - bm.Position[0].y;
      }
    }

    if (dx != 0.0f || dy != 0.0f) {
      for (int b = 0; b < (int)bones.size(); ++b) {
        bones[b][0][3] -= dx;
        bones[b][1][3] -= dy;
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
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  model = glm::rotate(model, m_facing, glm::vec3(0, 0, 1));

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);
  m_shader->setMat4("model", model);

  glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
  m_shader->setVec3("lightPos", eye + glm::vec3(0, 500, 0));
  m_shader->setVec3("lightColor", 1.0f, 1.0f, 1.0f);
  m_shader->setVec3("viewPos", eye);
  m_shader->setBool("useFog", true);
  m_shader->setVec3("uFogColor", glm::vec3(0.117f, 0.078f, 0.039f));
  m_shader->setFloat("uFogNear", 1500.0f);
  m_shader->setFloat("uFogFar", 3500.0f);
  m_shader->setFloat("blendMeshLight", 1.0f);
  m_shader->setFloat("objectAlpha", 1.0f);
  m_shader->setVec2("texCoordOffset", glm::vec2(0.0f));
  m_shader->setFloat("luminosity", m_luminosity);

  // Terrain lightmap at hero position
  glm::vec3 tLight = sampleTerrainLightAt(m_pos);
  m_shader->setVec3("terrainLight", tLight);

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  // Draw all body part meshes
  for (int p = 0; p < PART_COUNT; ++p) {
    for (auto &mb : m_parts[p].meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      if (mb.noneBlend) {
        glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glEnable(GL_BLEND);
      } else if (mb.bright) {
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      } else {
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
  }
  // Draw base head for accessory helms (face visible underneath helm)
  if (m_showBaseHead) {
    for (auto &mb : m_baseHead.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // Draw weapon (if equipped)
  // SafeZone: weapon renders on bone 47 (back) with rotation/offset
  // Combat: weapon renders on hand bone (33 or 42) with identity offset
  // Reference: ZzzCharacter.cpp RenderCharacterBackItem (line 14634)
  static constexpr int BONE_BACK = 47;
  auto &wCat = GetWeaponCategoryRender(m_weaponInfo.category);
  int attachBone = (m_inSafeZone && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : wCat.attachBone;
  if (m_weaponBmd && !m_weaponMeshBuffers.empty() &&
      attachBone < (int)bones.size()) {

    // SafeZone: back rotation (70,0,90) + offset (-20,5,40) (Main 5.2 line
    // 6693) Combat: identity (weapon BMD's own bone handles orientation)
    BoneWorldMatrix weaponOffsetMat =
        m_inSafeZone
            ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                              glm::vec3(-20.f, 5.f, 40.f))
            : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                              glm::vec3(0, 0, 0));

    // parentMat = CharBone[attachBone] * OffsetMatrix
    BoneWorldMatrix parentMat;
    MuMath::ConcatTransforms((const float(*)[4])bones[attachBone].data(),
                             (const float(*)[4])weaponOffsetMat.data(),
                             (float(*)[4])parentMat.data());

    // Use cached weapon local bones (static bind-pose, computed once at equip)
    const auto &wLocalBones = m_weaponLocalBones;
    std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
    for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
      MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                               (const float(*)[4])wLocalBones[bi].data(),
                               (float(*)[4])wFinalBones[bi].data());
    }

    // Re-skin weapon vertices using final bone matrices
    for (int mi = 0; mi < (int)m_weaponMeshBuffers.size() &&
                     mi < (int)m_weaponBmd->Meshes.size();
         ++mi) {
      auto &mesh = m_weaponBmd->Meshes[mi];
      auto &mb = m_weaponMeshBuffers[mi];
      if (mb.indexCount == 0)
        continue;

      std::vector<ViewerVertex> verts;
      verts.reserve(mesh.NumTriangles * 3);
      for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
        auto &tri = mesh.Triangles[ti];
        for (int v = 0; v < 3; ++v) {
          ViewerVertex vv;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 srcPos = srcVert.Position;
          glm::vec3 srcNorm = (tri.NormalIndex[v] < mesh.NumNormals)
                                  ? mesh.Normals[tri.NormalIndex[v]].Normal
                                  : glm::vec3(0, 0, 1);

          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)wFinalBones.size()) {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])wFinalBones[boneIdx].data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])wFinalBones[boneIdx].data(), srcNorm);
          } else {
            vv.pos = MuMath::TransformPoint((const float(*)[4])parentMat.data(),
                                            srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])parentMat.data(), srcNorm);
          }
          vv.tex =
              (tri.TexCoordIndex[v] < mesh.NumTexCoords)
                  ? glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                              mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV)
                  : glm::vec2(0);
          verts.push_back(vv);
        }
      }

      // Upload to GPU via glBufferSubData
      glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(ViewerVertex),
                      verts.data());

      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);

      // Main 5.2: ItemLight — glow mesh renders additively with pulsing brightness
      if (m_weaponBlendMesh >= 0 && mi == m_weaponBlendMesh) {
        float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
        m_shader->setFloat("blendMeshLight", pulseLight);
        glBlendFunc(GL_ONE, GL_ONE); // Additive
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->setFloat("blendMeshLight", 1.0f);
      } else {
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
  }

  // --- Render shield / left-hand item ---
  // SafeZone: renders on bone 47 (back) offset to not overlap weapon
  // Combat: renders on bone 42 (left hand) with identity offset
  auto &sCat = GetWeaponCategoryRender(6); // category 6 = shield
  int shieldBone = (m_inSafeZone && BONE_BACK < (int)bones.size())
                       ? BONE_BACK
                       : sCat.attachBone;
  if (m_shieldBmd && !m_shieldMeshBuffers.empty() &&
      shieldBone < (int)bones.size()) {

    // SafeZone back rendering (Main 5.2 RenderLinkObject line 6710-6731):
    // Shield: rotation (70,0,90) + offset (-10,0,0)
    // Dual-wield left weapon: rotation (-110,180,90) + offset (20,15,40)
    //   (mirrors to opposite side of back — Kayito WeaponView.cpp)
    bool dualWieldLeft = m_inSafeZone && isDualWielding();
    BoneWorldMatrix shieldOffsetMat =
        m_inSafeZone ? (dualWieldLeft ? MuMath::BuildWeaponOffsetMatrix(
                                            glm::vec3(-110.f, 180.f, 90.f),
                                            glm::vec3(20.f, 15.f, 40.f))
                                      : MuMath::BuildWeaponOffsetMatrix(
                                            glm::vec3(70.f, 0.f, 90.f),
                                            glm::vec3(-10.f, 0.f, 0.f)))
                     : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0, 0, 0),
                                                       glm::vec3(0, 0, 0));

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
      auto &mesh = m_shieldBmd->Meshes[mi];
      auto &mb = m_shieldMeshBuffers[mi];
      if (mb.indexCount == 0)
        continue;

      std::vector<ViewerVertex> verts;
      verts.reserve(mesh.NumTriangles * 3);
      for (int ti = 0; ti < mesh.NumTriangles; ++ti) {
        auto &tri = mesh.Triangles[ti];
        for (int v = 0; v < 3; ++v) {
          ViewerVertex vv;
          auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
          glm::vec3 srcPos = srcVert.Position;
          glm::vec3 srcNorm = (tri.NormalIndex[v] < mesh.NumNormals)
                                  ? mesh.Normals[tri.NormalIndex[v]].Normal
                                  : glm::vec3(0, 0, 1);

          int boneIdx = srcVert.Node;
          if (boneIdx >= 0 && boneIdx < (int)sFinalBones.size()) {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])sFinalBones[boneIdx].data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])sFinalBones[boneIdx].data(), srcNorm);
          } else {
            vv.pos = MuMath::TransformPoint(
                (const float(*)[4])shieldParentMat.data(), srcPos);
            vv.normal = MuMath::RotateVector(
                (const float(*)[4])shieldParentMat.data(), srcNorm);
          }
          vv.tex =
              (tri.TexCoordIndex[v] < mesh.NumTexCoords)
                  ? glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                              mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV)
                  : glm::vec2(0);
          verts.push_back(vv);
        }
      }

      glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      verts.size() * sizeof(ViewerVertex), verts.data());
    }
    // Draw shield meshes
    for (auto &mb : m_shieldMeshBuffers) {
      if (mb.indexCount == 0)
        continue;
      glBindTexture(GL_TEXTURE_2D, mb.texture);
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }
}

void HeroCharacter::RenderShadow(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_skeleton || !m_shadowShader || m_cachedBones.empty())
    return;

  // Shadow model matrix: NO facing rotation (facing is baked into vertices
  // before shadow projection so the shadow direction stays fixed in world
  // space)
  glm::mat4 model = glm::translate(glm::mat4(1.0f), m_pos);
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);
  m_shadowShader->setMat4("model", model);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Stencil: draw each shadow pixel exactly once — body + weapon + shield
  // merge into one unified shadow silhouette.
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glClear(GL_STENCIL_BUFFER_BIT);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_INCR, GL_INCR);

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 2000.0f;
  const float sy = 4000.0f;

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
      if (sm.vertexCount == 0 || sm.vao == 0)
        continue;

      auto &mesh = bmd->Meshes[mi];
      std::vector<glm::vec3> shadowVerts;
      shadowVerts.reserve(sm.vertexCount);

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

      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      shadowVerts.size() * sizeof(glm::vec3),
                      shadowVerts.data());
      glBindVertexArray(sm.vao);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
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
  static constexpr int SHADOW_BONE_BACK = 47;
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
      const auto &wLocalBones = m_weaponLocalBones;
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

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
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

    glm::vec3 step = dir * m_speed * deltaTime;
    glm::vec3 newPos = m_pos + step;

    const int S = TerrainParser::TERRAIN_SIZE;
    auto isWalkableAt = [&](float wx, float wz) -> bool {
      int tgz = (int)(wx / 100.0f);
      int tgx = (int)(wz / 100.0f);
      return (tgx >= 0 && tgz >= 0 && tgx < S && tgz < S) &&
             (m_terrainData->mapping.attributes[tgz * S + tgx] & 0x04) == 0;
    };

    // Wall sliding: try full move, then X-only, then Z-only
    // (Main 5.2 MapPath.cpp: direction fallback when diagonal is blocked)
    if (isWalkableAt(newPos.x, newPos.z)) {
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
  m_target = target;
  // Only reset walk animation if not already walking
  int walkAction =
      (!m_inSafeZone && m_weaponBmd) ? weaponWalkAction() : ACTION_WALK_MALE;
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
  // Use weapon-specific idle action when outside SafeZone with weapon
  if (!m_inSafeZone && m_weaponBmd) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
  m_animFrame = 0.0f;
}

void HeroCharacter::SetInSafeZone(bool safe) {
  if (m_inSafeZone == safe)
    return;
  m_inSafeZone = safe;
  // Original MU: weapon model is ALWAYS rendered when equipped.
  // SafeZone only changes animation stance (unarmed vs combat).

  // Switch animation to match new state
  if (m_moving) {
    SetAction((!safe && m_weaponBmd) ? weaponWalkAction() : ACTION_WALK_MALE);
  } else {
    SetAction((!safe && m_weaponBmd) ? weaponIdleAction() : ACTION_STOP_MALE);
  }

  std::cout << "[Hero] " << (safe ? "Entered SafeZone" : "Left SafeZone")
            << ", action=" << m_action << std::endl;
}

void HeroCharacter::EquipWeapon(const WeaponEquipInfo &weapon) {
  // Cleanup old weapon
  CleanupMeshBuffers(m_weaponMeshBuffers);
  for (auto &sm : m_weaponShadowMeshes) {
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
  }
  m_weaponShadowMeshes.clear();

  if (weapon.category == 0xFF) {
    m_weaponBmd.reset();
    m_weaponInfo = weapon;
    m_inSafeZone = true;
    SetAction(ACTION_STOP_MALE);
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
                        weaponAABB, false);
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
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
      meshes.push_back(sm);
    }
    return meshes;
  };
  m_weaponShadowMeshes = createShadowMeshes(bmd.get());

  m_weaponBmd = std::move(bmd);
  m_weaponLocalBones = ComputeBoneMatrices(m_weaponBmd.get());

  // Main 5.2: ItemLight — per-weapon BlendMesh glow assignment
  m_weaponBlendMesh = ItemModelManager::GetItemBlendMesh(
      weapon.category, weapon.itemIndex);

  auto &catRender = GetWeaponCategoryRender(weapon.category);
  std::cout << "[Hero] Loaded weapon " << weapon.modelFile << ": "
            << m_weaponBmd->Meshes.size() << " meshes, "
            << m_weaponBmd->Bones.size() << " bones"
            << " (bone=" << (int)catRender.attachBone
            << " idle=" << weaponIdleAction() << " walk=" << weaponWalkAction()
            << " 2H=" << weapon.twoHanded << ")" << std::endl;

  // Update animation to combat stance if outside SafeZone
  if (!m_inSafeZone) {
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
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
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
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
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

  std::cout << "[Hero] Shield equipped: " << shield.modelFile << " ("
            << m_shieldMeshBuffers.size() << " GPU meshes)" << std::endl;
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

void HeroCharacter::EquipBodyPart(int partIndex, const std::string &modelFile) {
  if (partIndex < 0 || partIndex >= PART_COUNT)
    return;

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
    if (sm.vao)
      glDeleteVertexArrays(1, &sm.vao);
    if (sm.vbo)
      glDeleteBuffers(1, &sm.vbo);
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
      glGenVertexArrays(1, &sm.vao);
      glGenBuffers(1, &sm.vbo);
      glBindVertexArray(sm.vao);
      glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
      glBufferData(GL_ARRAY_BUFFER, sm.vertexCount * sizeof(glm::vec3), nullptr,
                   GL_DYNAMIC_DRAW);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                            (void *)0);
      glEnableVertexAttribArray(0);
      glBindVertexArray(0);
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
      if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
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
}

void HeroCharacter::AttackMonster(int monsterIndex,
                                  const glm::vec3 &monsterPos) {
  if (IsDead())
    return;
  if (m_globalAttackCooldown > 0.0f)
    return; // Still on cooldown from cancelled attack

  // Already attacking same target — just update position, don't reset cycle
  if (monsterIndex == m_attackTargetMonster && m_activeSkillId == 0 &&
      (m_attackState == AttackState::SWINGING ||
       m_attackState == AttackState::COOLDOWN)) {
    m_attackTargetPos = monsterPos;
    return;
  }

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
    m_moving = false;

    // Face the target
    m_targetFacing = atan2f(dir.z, -dir.x);

    // Weapon-type-specific attack animation (Main 5.2 SwordCount cycle)
    int act = nextAttackAction();
    SetAction(act);
    // Main 5.2: weapon-type-specific swing sound (ZzzCharacter.cpp:1190-1204)
    // Spears (cat 3) → eSwingLightSword; all other melee → random eSwingWeapon1/2
    if (HasWeapon()) {
      if (m_weaponInfo.category == 3)
        SoundManager::Play(SOUND_SWING_LIGHT);
      else
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (act >= 0 && act < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[act].NumAnimationKeys : 1;
    float spd = ANIM_SPEED * attackSpeedMultiplier();
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    float cd = ATTACK_COOLDOWN_TIME / attackSpeedMultiplier();
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;
  } else {
    // Out of range — walk toward target
    m_attackState = AttackState::APPROACHING;
    MoveTo(monsterPos);
  }
}

void HeroCharacter::UpdateAttack(float deltaTime) {
  // Tick global cooldown (persists after cancel to prevent exploit)
  if (m_globalAttackCooldown > 0.0f) {
    m_globalAttackCooldown -= deltaTime;
    if (m_globalAttackCooldown < 0.0f)
      m_globalAttackCooldown = 0.0f;
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
        case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;
        case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break;
        default:
          if (HasWeapon())
            SoundManager::Play(SOUND_SWING1 + rand() % 2);
          break;
        }
        if (m_vfxManager) {
          m_vfxManager->SpawnSkillCast(m_activeSkillId, m_pos, m_facing);
          // Spell VFX dispatch (same as SkillAttackMonster in-range path)
          switch (m_activeSkillId) {
          case 17: // Energy Ball
          case 4:  // Fire Ball
            m_vfxManager->SpawnSpellProjectile(m_activeSkillId, m_pos,
                                               m_attackTargetPos);
            break;
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
          case 10: // Hellfire — ground fire ring (beams at blast phase)
            m_vfxManager->SpawnHellfire(m_pos);
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
          if (m_weaponInfo.category == 3)
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

    float atkAnimSpeed = ANIM_SPEED * attackSpeedMultiplier();
    float animDuration = (numKeys > 1) ? (float)numKeys / atkAnimSpeed : 0.5f;
    m_attackAnimTimer += deltaTime;

    // Flash uses m_animFrame for swing-done check (animation slowdown desyncs timer)
    bool swingDone = (m_activeSkillId == 12)
        ? (m_animFrame >= (float)(numKeys - 1))
        : (m_attackAnimTimer >= animDuration);

    if (swingDone) {
      // Swing finished — go to cooldown (also scaled by attack speed)
      m_attackState = AttackState::COOLDOWN;
      // Spells have shorter cooldown (0.2s base) for smoother casting flow
      float baseCooldown =
          (m_activeSkillId > 0) ? 0.2f : ATTACK_COOLDOWN_TIME;
      m_attackCooldown = baseCooldown / attackSpeedMultiplier();

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

      // Return to combat idle (weapon stance or unarmed)
      SetAction(m_weaponBmd ? weaponIdleAction() : ACTION_STOP_MALE);
    }
    break;
  }

  case AttackState::COOLDOWN: {
    m_attackCooldown -= deltaTime;
    if (m_attackCooldown <= 0.0f) {
      // Auto-attack: if target is still valid, swing again
      if (m_attackTargetMonster >= 0) {
        // Will be re-evaluated from main.cpp which checks if target alive
        m_attackState = AttackState::NONE;
        m_activeSkillId =
            0; // Reset so auto-attack re-engages with normal attacks
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
  if (m_attackState != AttackState::SWINGING || m_attackHitRegistered)
    return false;

  int numKeys = 1;
  if (m_action >= 0 && m_action < (int)m_skeleton->Actions.size())
    numKeys = m_skeleton->Actions[m_action].NumAnimationKeys;

  float atkAnimSpeed = ANIM_SPEED * attackSpeedMultiplier();
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

  m_attackState = AttackState::NONE;
  m_attackTargetMonster = -1;
  m_activeSkillId = 0;
  m_swordSwingCount = 0;
  m_moving = false; // Stop any approach movement
  m_pendingAquaBeam = false; // Clear pending beam on cancel
  m_aquaBeamSpawned = false;
  m_aquaPacketReady = false; // Don't send damage if cancelled
  // Return to appropriate idle
  if (!m_inSafeZone && m_weaponBmd) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
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
  case 13:
    // Cometfall — Main 5.2: SetPlayerMagic() → HAND1 or HAND2 (50/50)
    return (rand() % 2 == 0) ? ACTION_SKILL_HAND1 : ACTION_SKILL_HAND2;
  case 14:
    return ACTION_SKILL_INFERNO; // Inferno (self-centered AoE)
  default:
    return ACTION_SKILL_SWORD1; // Fallback
  }
}

void HeroCharacter::SkillAttackMonster(int monsterIndex,
                                       const glm::vec3 &monsterPos,
                                       uint8_t skillId) {
  if (IsDead())
    return;
  // Allow spell to interrupt normal melee attacks
  if (m_globalAttackCooldown > 0.0f) {
    if (m_activeSkillId > 0)
      return; // Block if GCD from a previous skill cast
    m_globalAttackCooldown = 0.0f; // Reset melee GCD
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
    case 42: SoundManager::Play(SOUND_RAGE_BLOW1); break;    // Rageful Blow
    case 43: SoundManager::Play(SOUND_KNIGHT_SKILL2); break; // Death Stab (same as Lunge)
    default:
      if (HasWeapon())
        SoundManager::Play(SOUND_SWING1 + rand() % 2);
      break;
    }

    // Set GCD = full attack cycle (animation + cooldown)
    int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
                 ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
    float spd = ANIM_SPEED * attackSpeedMultiplier();
    float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
    // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
    if (skillId == 12)
      animDur += 2.0f / spd;
    float cd = 0.2f / attackSpeedMultiplier(); // Spell cooldown = 0.2s base
    m_globalAttackCooldown = animDur + cd;
    m_globalAttackCooldownMax = m_globalAttackCooldown;

    if (m_vfxManager) {
      m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing);
      // Spell VFX: dispatch by skill ID (not class — server authorizes skills)
      switch (skillId) {
      case 17: // Energy Ball: traveling BITMAP_ENERGY projectile
      case 4:  // Fire Ball: traveling MODEL_FIRE projectile
        m_vfxManager->SpawnSpellProjectile(skillId, m_pos, monsterPos);
        break;
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
  // Allow spell to interrupt normal melee attacks (reset GCD from auto-attack)
  if (m_globalAttackCooldown > 0.0f && m_activeSkillId > 0)
    return; // Only block if GCD is from a previous SKILL cast
  // Cancel any in-progress attack and reset melee GCD
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
  default: break; // DW spells have no swing/cast sound
  }

  // Set GCD
  int nk = (skillAction >= 0 && skillAction < (int)m_skeleton->Actions.size())
               ? m_skeleton->Actions[skillAction].NumAnimationKeys : 1;
  float spd = ANIM_SPEED * attackSpeedMultiplier();
  float animDur = (nk > 1) ? (float)nk / spd : 0.5f;
  // Flash: animation slowdown during frames 1.0-3.0 adds ~2 frames at half speed
  if (skillId == 12)
    animDur += 2.0f / spd;
  float cd = 0.2f / attackSpeedMultiplier();
  m_globalAttackCooldown = animDur + cd;
  m_globalAttackCooldownMax = m_globalAttackCooldown;

  // Spawn VFX — tornado travels from caster toward target
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(skillId, m_pos, m_facing);
    switch (skillId) {
    case 8: // Twister — tornado travels toward click direction
      m_vfxManager->SpawnTwisterStorm(m_pos, targetPos - m_pos);
      break;
    case 9: // Evil Spirit — 4-directional beams from caster
      m_vfxManager->SpawnEvilSpirit(m_pos, m_facing);
      break;
    case 10: // Hellfire — ground fire ring (beams spawned at blast phase)
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

  // VFX: white rising sparks at origin and destination (Main 5.2)
  if (m_vfxManager) {
    m_vfxManager->SpawnSkillCast(6, m_pos, m_facing);
    m_vfxManager->SpawnSkillImpact(6, target);
  }

  // No animation change — instant teleport, keep current pose
  // Fixed 1.5s GCD to prevent spam
  m_globalAttackCooldown = 1.5f;
  m_globalAttackCooldownMax = m_globalAttackCooldown;

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
  SoundManager::Play(SOUND_MALE_DIE);
  std::cout << "[Hero] Dying (Forced) — action=" << ACTION_DIE1
            << " numActions="
            << (m_skeleton ? (int)m_skeleton->Actions.size() : 0) << std::endl;
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
      // Return to appropriate idle if not attacking/moving
      if (m_attackState == AttackState::NONE && !m_moving) {
        if (!m_inSafeZone && m_weaponBmd) {
          SetAction(weaponIdleAction());
        } else {
          SetAction(ACTION_STOP_MALE);
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
  // Return to idle
  if (!m_inSafeZone && m_weaponBmd) {
    SetAction(weaponIdleAction());
  } else {
    SetAction(ACTION_STOP_MALE);
  }
}

void HeroCharacter::SnapToTerrain() {
  if (!m_terrainData)
    return;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = m_pos.x / 100.0f;
  float gx = m_pos.z / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  m_pos.y = h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) +
            h01 * (1 - xd) * zd + h11 * xd * zd;
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

}

void HeroCharacter::Cleanup() {
  auto cleanupShadows = [](std::vector<ShadowMesh> &shadowMeshes) {
    for (auto &sm : shadowMeshes) {
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
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
  cleanupShadows(m_weaponShadowMeshes);
  m_weaponBmd.reset();

  CleanupMeshBuffers(m_shieldMeshBuffers);
  cleanupShadows(m_shieldShadowMeshes);
  m_shieldBmd.reset();

  m_shader.reset();
  m_shadowShader.reset();
  m_skeleton.reset();
}
