#include "MonsterManager.hpp"
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

// Monster type → display name
static const std::unordered_map<uint16_t, std::string> s_monsterNames = {
    {0, "Bull Fighter"},
    {1, "Hound"},
    {2, "Budge Dragon"},
    {3, "Spider"},
    {4, "Elite Bull Fighter"},
    {5, "Hell Hound"},
    {6, "Lich"},
    {7, "Giant"},
    {8, "Poison Bull"},
    {9, "Thunder Lich"},
    {10, "Dark Knight"},
    {11, "Ghost"},
    {12, "Larva"},
    {13, "Hell Spider"},
    {14, "Skeleton Warrior"},
    {15, "Skeleton Archer"},
    {16, "Skeleton Captain"},
    {17, "Cyclops"},
    {18, "Gorgon"},
    {19, "Yeti"},
    {20, "Elite Yeti"},
    {21, "Assassin"},
    {22, "Ice Monster"},
    {23, "Hommerd"},
    {24, "Worm"},
    {25, "Ice Queen"},
    // Noria monsters (types 26-33)
    {26, "Goblin"},
    {27, "Chain Scorpion"},
    {28, "Beetle Monster"},
    {29, "Hunter"},
    {30, "Forest Monster"},
    {31, "Agon"},
    {32, "Stone Golem"},
    {33, "Elite Goblin"},
    // Elf summon
    {150, "Bali"}};

glm::vec3
MonsterManager::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
  return TerrainUtils::SampleLightAt(m_terrainLightmap, worldPos);
}

float MonsterManager::snapToTerrain(float worldX, float worldZ) {
  return TerrainUtils::GetHeight(m_terrainData, worldX, worldZ);
}


// ─── Catmull-Rom spline evaluation ───────────────────────────────────────────

static glm::vec3 evalCatmullRom(const std::vector<glm::vec3> &pts, float t) {
  if (pts.empty())
    return glm::vec3(0);
  if (pts.size() == 1)
    return pts[0];

  int n = (int)pts.size();
  int i = std::max(0, std::min((int)t, n - 2));
  float f = std::clamp(t - (float)i, 0.0f, 1.0f);

  const glm::vec3 &p0 = pts[std::max(0, i - 1)];
  const glm::vec3 &p1 = pts[i];
  const glm::vec3 &p2 = pts[std::min(n - 1, i + 1)];
  const glm::vec3 &p3 = pts[std::min(n - 1, i + 2)];

  float f2 = f * f, f3 = f2 * f;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * f +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f3);
}

static glm::vec3 evalCatmullRomTangent(const std::vector<glm::vec3> &pts,
                                       float t) {
  if (pts.size() < 2)
    return glm::vec3(0, 0, 1);

  int n = (int)pts.size();
  int i = std::max(0, std::min((int)t, n - 2));
  float f = std::clamp(t - (float)i, 0.0f, 1.0f);

  const glm::vec3 &p0 = pts[std::max(0, i - 1)];
  const glm::vec3 &p1 = pts[i];
  const glm::vec3 &p2 = pts[std::min(n - 1, i + 1)];
  const glm::vec3 &p3 = pts[std::min(n - 1, i + 2)];

  float f2 = f * f;
  return 0.5f * ((-p0 + p2) +
                 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f +
                 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f2);
}

int MonsterManager::loadMonsterModel(const std::string &bmdFile,
                                     const std::string &name, float scale,
                                     float radius, float height,
                                     float bodyOffset,
                                     const std::string &texDirOverride) {
  // Check if already loaded
  for (int i = 0; i < (int)m_models.size(); ++i) {
    if (m_models[i].name == name)
      return i;
  }

  std::string fullPath = m_monsterTexPath + bmdFile;
  auto bmd = BMDParser::Parse(fullPath);
  if (!bmd) {
    std::cerr << "[Monster] Failed to load BMD: " << fullPath << std::endl;
    return -1;
  }

  MonsterModel model;
  model.name = name;
  model.texDir = texDirOverride.empty() ? m_monsterTexPath : texDirOverride;
  model.bmd = bmd.get();
  model.scale = scale;
  model.collisionRadius = radius;
  model.collisionHeight = height;
  model.bodyOffset = bodyOffset;

  // Find root bone (Parent == -1) for LockPositions handling
  for (int i = 0; i < (int)bmd->Bones.size(); ++i) {
    if (!bmd->Bones[i].Dummy && bmd->Bones[i].Parent == -1) {
      model.rootBone = i;
      break;
    }
  }

  m_ownedBmds.push_back(std::move(bmd));

  int idx = (int)m_models.size();
  m_models.push_back(std::move(model));
  auto *loadedBmd = m_models[idx].bmd;
  std::cout << "[Monster] Loaded model '" << name << "' ("
            << loadedBmd->Bones.size() << " bones, " << loadedBmd->Meshes.size()
            << " meshes, " << loadedBmd->Actions.size()
            << " actions, rootBone=" << m_models[idx].rootBone << ")"
            << std::endl;

  // Pre-upload mesh buffers using identity bones (for debris and shared use)
  auto identityBones = ComputeBoneMatrices(loadedBmd);
  for (auto &mesh : loadedBmd->Meshes) {
    UploadMeshWithBones(mesh, m_models[idx].texDir, identityBones,
                        m_models[idx].meshBuffers, m_models[idx].meshBounds, true);
  }

  // Log LockPositions for walk action (ACTION_WALK=2)
  if (ACTION_WALK < (int)loadedBmd->Actions.size()) {
    std::cout << "[Monster]   Walk action " << ACTION_WALK
              << ": keys=" << loadedBmd->Actions[ACTION_WALK].NumAnimationKeys
              << " LockPositions="
              << loadedBmd->Actions[ACTION_WALK].LockPositions << std::endl;
  }
  return idx;
}

void MonsterManager::InitModels(const std::string &dataPath) {
  if (m_modelsLoaded)
    return;

  m_monsterTexPath = dataPath + "/Monster/";

  // Create shaders (same as NPC — model.vert/frag, shadow.vert/frag)
  m_shader = Shader::Load("vs_model.bin", "fs_model.bin");
  m_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");
  m_outlineShader = Shader::Load("vs_outline.bin", "fs_outline.bin");

  // Bull Fighter: server type 0, Monster01.bmd (CreateMonsterClient: scale 0.8)
  // BBox: (-60,-60,0) to (50,50,150) — default
  int bullIdx =
      loadMonsterModel("Monster01.bmd", "Bull Fighter", 0.8f, 80.0f, 150.0f);
  if (bullIdx >= 0) {
    auto &bull = m_models[bullIdx];
    bull.level = 6;       // OpenMU: Level=6
    bull.defense = 6;     // OpenMU: Defense=6
    bull.defenseRate = 6; // OpenMU: DefRate=6
    bull.attackRate = 28; // OpenMU: AtkRate=28
  }
  m_typeToModel[0] = bullIdx;

  // Hound: server type 1, Monster02.bmd (CreateMonsterClient: scale 0.85)
  // BBox: (-60,-60,0) to (50,50,150) — default
  int houndIdx =
      loadMonsterModel("Monster02.bmd", "Hound", 0.85f, 80.0f, 150.0f);
  if (houndIdx >= 0) {
    auto &hound = m_models[houndIdx];
    hound.level = 9;       // OpenMU: Level=9
    hound.defense = 9;     // OpenMU: Defense=9
    hound.defenseRate = 9; // OpenMU: DefRate=9
    hound.attackRate = 39; // OpenMU: AtkRate=39
  }
  m_typeToModel[1] = houndIdx;

  // Budge Dragon: server type 2, Monster03.bmd (CreateMonsterClient: scale 0.5)
  // BBox: (-60,-60,0) to (50,50,80) — flying type, NO bodyOffset (hover handles
  // height)
  int budgeIdx =
      loadMonsterModel("Monster03.bmd", "Budge Dragon", 0.5f, 70.0f, 80.0f);
  if (budgeIdx >= 0) {
    auto &budge = m_models[budgeIdx];
    budge.level = 4;       // OpenMU: Level=4
    budge.defense = 3;     // OpenMU: Defense=3
    budge.defenseRate = 3; // OpenMU: DefRate=3
    budge.attackRate = 18; // OpenMU: AtkRate=18
  }
  m_typeToModel[2] = budgeIdx;

  // Spider: server type 3, Monster10.bmd (CreateMonsterClient: scale 0.4,
  // OpenMonsterModel(9)) BBox: (-60,-60,0) to (50,50,80) — NO bodyOffset
  // (BodyHeight=0 in original)
  int spiderIdx =
      loadMonsterModel("Monster10.bmd", "Spider", 0.4f, 70.0f, 80.0f);
  if (spiderIdx >= 0) {
    auto &spider = m_models[spiderIdx];
    spider.level = 2;       // OpenMU: Level=2
    spider.defense = 1;     // OpenMU: Defense=1
    spider.defenseRate = 1; // OpenMU: DefRate=1
    spider.attackRate = 8;  // OpenMU: AtkRate=8
  }
  m_typeToModel[3] = spiderIdx;

  // Elite Bull Fighter: server type 4, Monster01.bmd (Scale 1.15)
  // Separate model entry so it can have different weapons than Bull Fighter
  int eliteBullIdx = -1;
  if (bullIdx >= 0) {
    auto &bullModel = m_models[bullIdx];
    MonsterModel eliteBull;
    eliteBull.name = "Elite Bull Fighter";
    eliteBull.texDir = bullModel.texDir;
    eliteBull.bmd = bullModel.bmd; // Share BMD pointer (non-owning)
    eliteBull.scale = 1.15f;
    eliteBull.collisionRadius = bullModel.collisionRadius;
    eliteBull.collisionHeight = bullModel.collisionHeight;
    eliteBull.bodyOffset = bullModel.bodyOffset;
    eliteBull.rootBone = bullModel.rootBone;
    eliteBull.level = 12;       // OpenMU: Level=12
    eliteBull.defense = 12;     // OpenMU: Defense=12
    eliteBull.defenseRate = 12; // OpenMU: DefRate=12
    eliteBull.attackRate = 50;  // OpenMU: AtkRate=50
    // Pre-upload mesh buffers (separate GL objects from Bull Fighter)
    auto identBones = ComputeBoneMatrices(eliteBull.bmd);
    for (auto &mesh : eliteBull.bmd->Meshes) {
      UploadMeshWithBones(mesh, eliteBull.texDir, identBones,
                          eliteBull.meshBuffers, eliteBull.meshBounds, true);
    }
    eliteBullIdx = (int)m_models.size();
    m_models.push_back(std::move(eliteBull));
    std::cout
        << "[Monster] Created Elite Bull Fighter model (separate from Bull "
           "Fighter for weapon support)"
        << std::endl;
  }
  m_typeToModel[4] = eliteBullIdx;

  // Lich: server type 6, Monster05.bmd (scale 0.85, ranged caster)
  int lichIdx = loadMonsterModel("Monster05.bmd", "Lich", 0.85f, 80.0f, 150.0f);
  if (lichIdx >= 0) {
    auto &lich = m_models[lichIdx];
    lich.level = 14;       // OpenMU: Level=14
    lich.defense = 14;     // OpenMU: Defense=14
    lich.defenseRate = 14; // OpenMU: DefRate=14
    lich.attackRate = 62;  // OpenMU: AtkRate=62
    lich.blendMesh = -1;   // Disable additive gloves — fire VFX on staff tip
  }
  m_typeToModel[6] = lichIdx;

  // Giant: server type 7, Monster06.bmd (scale 1.6, large and slow)
  int giantIdx =
      loadMonsterModel("Monster06.bmd", "Giant", 1.6f, 120.0f, 200.0f);
  if (giantIdx >= 0) {
    auto &giant = m_models[giantIdx];
    giant.level = 17;       // OpenMU: Level=17
    giant.defense = 18;     // OpenMU: Defense=18
    giant.defenseRate = 18; // OpenMU: DefRate=18
    giant.attackRate = 80;  // OpenMU: AtkRate=80
  }
  m_typeToModel[7] = giantIdx;

  // ── Dungeon monsters (map 1) ──
  // Main 5.2 EMonsterModelType enum → BMD filename mapping:
  // Model 3=DarkKnight→Monster04, 6=Larva→Monster07, 7=Ghost→Monster08,
  // 8=HellSpider→Monster09, 10=Cyclops→Monster11, 11=Gorgon→Monster12

  // Hell Hound (type 5): reuse Hound model (Monster02) at larger scale
  {
    int idx = loadMonsterModel("Monster02.bmd", "Hell Hound", 1.1f, 90.0f, 160.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 55;
      m_models[idx].defenseRate = 55;
      m_models[idx].attackRate = 165;
      m_models[idx].hiddenMesh = 1; // Main 5.2: HiddenMesh=1 (hide body skin mesh)
    }
    m_typeToModel[5] = idx;
  }

  // Poison Bull (type 8): Monster01.bmd (Bull Fighter body, same as type 0/4)
  // Main 5.2: OpenMonsterModel(MONSTER_MODEL_BULL_FIGHTER), Scale=1.0f
  {
    int idx = loadMonsterModel("Monster01.bmd", "Poison Bull", 1.0f, 90.0f, 130.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 75;
      m_models[idx].defenseRate = 75;
      m_models[idx].attackRate = 190;
    }
    m_typeToModel[8] = idx;
  }

  // Thunder Lich (type 9): reuse Lich model (Monster05) — lightning caster variant
  {
    int idx = loadMonsterModel("Monster05.bmd", "Thunder Lich", 1.1f, 80.0f, 150.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 70;
      m_models[idx].defenseRate = 70;
      m_models[idx].attackRate = 180;
      m_models[idx].blendMesh = -1;
    }
    m_typeToModel[9] = idx;
  }

  // Dark Knight (type 10): Monster04.bmd (Main 5.2: MONSTER_MODEL_DARK_KNIGHT)
  // Main 5.2: Scale=0.8f, Level=1
  {
    int idx = loadMonsterModel("Monster04.bmd", "Dark Knight", 0.8f, 90.0f, 170.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 80;
      m_models[idx].defenseRate = 80;
      m_models[idx].attackRate = 200;
    }
    m_typeToModel[10] = idx;
  }

  // Ghost (type 11): Monster08.bmd (Main 5.2: MONSTER_MODEL_GHOST)
  // Main 5.2: AlphaTarget=0.4f, no BlendMesh, no weapon, no Scale override
  {
    int idx = loadMonsterModel("Monster08.bmd", "Ghost", 1.0f, 70.0f, 140.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 40;
      m_models[idx].defenseRate = 40;
      m_models[idx].attackRate = 145;
      m_models[idx].typeAlpha = 0.4f; // Main 5.2: AlphaTarget=0.4f
    }
    m_typeToModel[11] = idx;
  }

  // Larva (type 12): Monster07.bmd (Main 5.2: MONSTER_MODEL_LARVA)
  {
    int idx = loadMonsterModel("Monster07.bmd", "Larva", 0.6f, 60.0f, 100.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 31;
      m_models[idx].defenseRate = 31;
      m_models[idx].attackRate = 120;
    }
    m_typeToModel[12] = idx;
  }

  // Hell Spider (type 13): Monster09.bmd (Main 5.2: MONSTER_MODEL_HELL_SPIDER)
  // Main 5.2: Scale=1.1f, Weapon=MODEL_SERPENT_STAFF
  {
    int idx = loadMonsterModel("Monster09.bmd", "Hell Spider", 1.1f, 80.0f, 100.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 60;
      m_models[idx].defenseRate = 60;
      m_models[idx].attackRate = 170;
    }
    m_typeToModel[13] = idx;
  }

  // Cyclops (type 17): Monster11.bmd (Main 5.2: MONSTER_MODEL_CYCLOPS)
  {
    int idx = loadMonsterModel("Monster11.bmd", "Cyclops", 0.9f, 110.0f, 190.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 35;
      m_models[idx].defenseRate = 35;
      m_models[idx].attackRate = 130;
    }
    m_typeToModel[17] = idx;
  }

  // Gorgon (type 18): Monster12.bmd (Main 5.2: MONSTER_MODEL_GORGON)
  // Main 5.2: Scale=1.5f, BlendMesh=1, BlendMeshLight=1.0f, Weapon=MODEL_GORGON_STAFF
  {
    int idx = loadMonsterModel("Monster12.bmd", "Gorgon", 1.5f, 120.0f, 200.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 100;
      m_models[idx].defenseRate = 100;
      m_models[idx].attackRate = 220;
      m_models[idx].blendMesh = 1;
    }
    m_typeToModel[18] = idx;
  }

  // ── Devias monsters (map 2) ──
  // Main 5.2 EMonsterModelType enum → BMD filename mapping:
  // Model 12=Yeti→Monster13, 13=EliteYeti→Monster14, 14=Assassin→Monster15,
  // 15=IceMonster→Monster16, 16=Hommerd→Monster17, 17=Worm→Monster18,
  // 18=IceQueen→Monster19

  // Yeti (type 19): Monster13.bmd (Main 5.2: Scale=1.1)
  {
    int idx = loadMonsterModel("Monster13.bmd", "Yeti", 1.1f, 90.0f, 170.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 37;
      m_models[idx].defenseRate = 37;
      m_models[idx].attackRate = 150;
    }
    m_typeToModel[19] = idx;
  }

  // Elite Yeti (type 20): Monster14.bmd (Main 5.2: Scale=1.4)
  {
    int idx = loadMonsterModel("Monster14.bmd", "Elite Yeti", 1.4f, 100.0f, 190.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 50;
      m_models[idx].defenseRate = 50;
      m_models[idx].attackRate = 180;
    }
    m_typeToModel[20] = idx;
  }

  // Assassin (type 21): Monster15.bmd (Main 5.2: Scale=0.95)
  {
    int idx = loadMonsterModel("Monster15.bmd", "Assassin", 0.95f, 80.0f, 150.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 33;
      m_models[idx].defenseRate = 33;
      m_models[idx].attackRate = 130;
    }
    m_typeToModel[21] = idx;
  }

  // Ice Monster (type 22): Monster16.bmd (Main 5.2: Scale=1.0, BlendMesh=0)
  {
    int idx = loadMonsterModel("Monster16.bmd", "Ice Monster", 1.0f, 80.0f, 140.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 27;
      m_models[idx].defenseRate = 27;
      m_models[idx].attackRate = 110;
      m_models[idx].blendMesh = 0; // Icy glow on mesh 0
    }
    m_typeToModel[22] = idx;
  }

  // Hommerd (type 23): Monster17.bmd (Main 5.2: Scale=1.15)
  {
    int idx = loadMonsterModel("Monster17.bmd", "Hommerd", 1.15f, 90.0f, 160.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 29;
      m_models[idx].defenseRate = 29;
      m_models[idx].attackRate = 120;
    }
    m_typeToModel[23] = idx;
  }

  // Worm (type 24): Monster18.bmd (Main 5.2: Scale=1.0, low profile)
  {
    int idx = loadMonsterModel("Monster18.bmd", "Worm", 1.0f, 70.0f, 100.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 25;
      m_models[idx].defenseRate = 25;
      m_models[idx].attackRate = 100;
    }
    m_typeToModel[24] = idx;
  }

  // Ice Queen (type 25): Monster19.bmd (Main 5.2: Scale=1.1, boss)
  {
    int idx = loadMonsterModel("Monster19.bmd", "Ice Queen", 1.1f, 100.0f, 180.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 90;
      m_models[idx].defenseRate = 90;
      m_models[idx].attackRate = 260;
      m_models[idx].blendMesh = 2; // Boss glow effect
    }
    m_typeToModel[25] = idx;
  }

  // ── Noria monsters (OpenMU Version075) ──
  // Main 5.2 enum+1 = BMD number: GOBLIN=19→Monster20, CHAIN_SCORPION=20→Monster21, etc.

  // Goblin (type 26): Monster20.bmd (Main 5.2: Scale=0.8, weapon=AXE)
  {
    int idx = loadMonsterModel("Monster20.bmd", "Goblin", 0.8f, 70.0f, 100.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 2;
      m_models[idx].defenseRate = 2;
      m_models[idx].attackRate = 13;
    }
    m_typeToModel[26] = idx;
  }

  // Chain Scorpion (type 27): Monster21.bmd (Main 5.2: Scale=1.1)
  {
    int idx = loadMonsterModel("Monster21.bmd", "Chain Scorpion", 1.1f, 80.0f, 120.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 4;
      m_models[idx].defenseRate = 4;
      m_models[idx].attackRate = 23;
    }
    m_typeToModel[27] = idx;
  }

  // Beetle Monster (type 28): Monster22.bmd (Main 5.2: Scale=0.8, BlendMesh=1)
  {
    int idx = loadMonsterModel("Monster22.bmd", "Beetle Monster", 0.8f, 80.0f, 130.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 10;
      m_models[idx].defenseRate = 10;
      m_models[idx].attackRate = 44;
      m_models[idx].blendMesh = 1;
    }
    m_typeToModel[28] = idx;
  }

  // Hunter (type 29): Monster23.bmd (Main 5.2: Scale=0.95, ranged)
  {
    int idx = loadMonsterModel("Monster23.bmd", "Hunter", 0.95f, 80.0f, 150.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 13;
      m_models[idx].defenseRate = 13;
      m_models[idx].attackRate = 56;
    }
    m_typeToModel[29] = idx;
  }

  // Forest Monster (type 30): Monster24.bmd (Main 5.2: Scale=0.75)
  {
    int idx = loadMonsterModel("Monster24.bmd", "Forest Monster", 0.75f, 80.0f, 140.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 15;
      m_models[idx].defenseRate = 15;
      m_models[idx].attackRate = 68;
    }
    m_typeToModel[30] = idx;
  }

  // Agon (type 31): Monster25.bmd (Main 5.2: Scale=1.3)
  {
    int idx = loadMonsterModel("Monster25.bmd", "Agon", 1.3f, 90.0f, 160.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 16;
      m_models[idx].defenseRate = 16;
      m_models[idx].attackRate = 74;
    }
    m_typeToModel[31] = idx;
  }

  // Stone Golem (type 32): Monster26.bmd (Main 5.2: default scale ~0.9)
  {
    int idx = loadMonsterModel("Monster26.bmd", "Stone Golem", 0.9f, 100.0f, 180.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 20;
      m_models[idx].defenseRate = 20;
      m_models[idx].attackRate = 86;
    }
    m_typeToModel[32] = idx;
  }

  // Elite Goblin (type 33): Monster20.bmd (same as Goblin, Main 5.2: Scale=1.2)
  {
    int idx = loadMonsterModel("Monster20.bmd", "Elite Goblin", 1.2f, 80.0f, 130.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 8;
      m_models[idx].defenseRate = 8;
      m_models[idx].attackRate = 33;
    }
    m_typeToModel[33] = idx;
  }

  // ── Skeleton monsters: Player.bmd animation rig + Skeleton0x.bmd mesh skins
  // ── Main 5.2: types 14,15,16 use MODEL_PLAYER bones + Skeleton01/02/03.bmd
  // meshes
  m_dataPath = dataPath;
  m_playerBmd = BMDParser::Parse(dataPath + "/Player/Player.bmd");
  if (m_playerBmd) {
    std::cout << "[Monster] Loaded Player.bmd for skeleton animations ("
              << m_playerBmd->Bones.size() << " bones, "
              << m_playerBmd->Actions.size() << " actions)" << std::endl;

    // Find Player.bmd root bone for LockPositions
    int playerRootBone = -1;
    for (int i = 0; i < (int)m_playerBmd->Bones.size(); ++i) {
      if (!m_playerBmd->Bones[i].Dummy && m_playerBmd->Bones[i].Parent == -1) {
        playerRootBone = i;
        break;
      }
    }

    std::string skillPath = dataPath + "/Skill/";

    // Action maps: monster actions (0-6) → Player.bmd action indices
    // Warrior/Captain: sword idle/walk/attack
    int swordActionMap[7] = {4, 4, 17, 39, 40, 230, 231};
    // Archer: bow idle/walk/attack
    int archerActionMap[7] = {8, 8, 21, 50, 50, 230, 231};

    struct SkelDef {
      uint16_t type;
      const char *bmdFile;
      const char *name;
      float scale;
      int *actionMap;
      int level, defense, defenseRate, attackRate;
    };
    SkelDef skelDefs[] = {
        {14, "Skeleton01.bmd", "Skeleton Warrior", 0.95f, swordActionMap, 19,
         22, 22, 93}, // OpenMU: Def=22, DefRate=22, AtkRate=93
        {15, "Skeleton02.bmd", "Skeleton Archer", 1.1f, archerActionMap, 22, 36,
         36, 120},
        {16, "Skeleton03.bmd", "Skeleton Captain", 1.2f, swordActionMap, 25, 45,
         45, 140},
    };

    for (auto &sd : skelDefs) {
      auto skelBmd = BMDParser::Parse(skillPath + sd.bmdFile);
      if (!skelBmd) {
        std::cerr << "[Monster] Failed to load " << sd.bmdFile << std::endl;
        m_typeToModel[sd.type] = -1;
        continue;
      }

      MonsterModel model;
      model.name = sd.name;
      model.texDir = skillPath;
      model.bmd = skelBmd.get();
      model.animBmd = m_playerBmd.get();
      model.scale = sd.scale;
      model.collisionRadius = 80.0f;
      model.collisionHeight = 150.0f;
      model.rootBone = playerRootBone;
      model.level = sd.level;
      model.defense = sd.defense;
      model.defenseRate = sd.defenseRate;
      model.attackRate = sd.attackRate;
      for (int i = 0; i < 7; ++i)
        model.actionMap[i] = sd.actionMap[i];

      // Pre-upload mesh buffers using Player.bmd identity bones
      auto identBones = ComputeBoneMatrices(m_playerBmd.get());
      for (auto &mesh : skelBmd->Meshes) {
        UploadMeshWithBones(mesh, skillPath, identBones, model.meshBuffers,
                            model.meshBounds, true);
      }

      m_ownedBmds.push_back(std::move(skelBmd));
      int idx = (int)m_models.size();
      m_models.push_back(std::move(model));
      m_typeToModel[sd.type] = idx;

      std::cout << "[Monster] Loaded skeleton '" << sd.name << "' (type "
                << sd.type << ", mesh=" << sd.bmdFile << ")" << std::endl;
    }

    // Load weapons for skeleton types (Main 5.2: c->Weapon[n].Type)
    std::string itemPath = dataPath + "/Item/";
    auto loadWeapon = [&](uint16_t type, const char *bmdFile, int bone,
                          glm::vec3 rot, glm::vec3 off) {
      auto it = m_typeToModel.find(type);
      if (it == m_typeToModel.end() || it->second < 0)
        return;
      auto wpnBmd = BMDParser::Parse(itemPath + bmdFile);
      if (!wpnBmd) {
        std::cerr << "[Monster] Failed to load weapon " << bmdFile << std::endl;
        return;
      }
      WeaponDef wd;
      wd.bmd = wpnBmd.get();
      wd.texDir = itemPath;
      wd.attachBone = bone;
      wd.rot = rot;
      wd.offset = off;
      wd.cachedLocalBones = ComputeBoneMatrices(wd.bmd);
      m_models[it->second].weaponDefs.push_back(wd);
      m_ownedBmds.push_back(std::move(wpnBmd));
      std::cout << "[Monster] Loaded weapon " << bmdFile << " for type " << type
                << " (bone " << bone << ")" << std::endl;
    };
    // Identity offset: same as HeroCharacter combat mode.
    // Our skeletons use Player.bmd bones (33/42) and weapon BMDs are designed
    // to work with those bones without additional rotation/offset.
    glm::vec3 noRot(0), noOff(0);

    // Skeleton Warrior (type 14): Sword07.bmd R-Hand(33) + Shield05.bmd
    // L-Hand(42)
    loadWeapon(14, "Sword07.bmd", 33, noRot, noOff);
    loadWeapon(14, "Shield05.bmd", 42, noRot, noOff);
    // Skeleton Archer (type 15): Bow03.bmd L-Hand(42)
    loadWeapon(15, "Bow03.bmd", 42, noRot, noOff);
    // Skeleton Captain (type 16): Axe04.bmd R-Hand(33) + Shield07.bmd
    // L-Hand(42)
    loadWeapon(16, "Axe04.bmd", 33, noRot, noOff);
    loadWeapon(16, "Shield07.bmd", 42, noRot, noOff);
  } else {
    std::cerr << "[Monster] Failed to load Player.bmd — skeleton types "
                 "disabled"
              << std::endl;
    m_typeToModel[14] = -1;
    m_typeToModel[15] = -1;
    m_typeToModel[16] = -1;
  }

  // ── Non-skeleton monster weapons (Main 5.2 ZzzCharacter.cpp) ──
  // These monsters use their own BMD skeletons, not Player.bmd.
  // LinkBone values from Main 5.2: SetMonsterLinkBone()
  {
    std::string itemPath = dataPath + "/Item/";
    glm::vec3 noRot(0), noOff(0);

    auto loadMonsterWeapon = [&](uint16_t type, const char *bmdFile, int bone,
                                 glm::vec3 rot, glm::vec3 off) {
      auto it = m_typeToModel.find(type);
      if (it == m_typeToModel.end() || it->second < 0)
        return;
      auto wpnBmd = BMDParser::Parse(itemPath + bmdFile);
      if (!wpnBmd) {
        std::cerr << "[Monster] Failed to load weapon " << bmdFile << std::endl;
        return;
      }
      WeaponDef wd;
      wd.bmd = wpnBmd.get();
      wd.texDir = itemPath;
      wd.attachBone = bone;
      wd.rot = rot;
      wd.offset = off;
      wd.cachedLocalBones = ComputeBoneMatrices(wd.bmd);
      m_models[it->second].weaponDefs.push_back(wd);
      m_ownedBmds.push_back(std::move(wpnBmd));
      std::cout << "[Monster] Loaded weapon " << bmdFile << " for type " << type
                << " (bone " << bone << ")" << std::endl;
    };

    // Bull Fighter (type 0): MODEL_AXE+6 = Axe07.bmd, R-Hand bone 42
    // Main 5.2: c->Weapon[0].LinkBone = 42 (Monster01.bmd "left_bone")
    loadMonsterWeapon(0, "Axe07.bmd", 42, noRot, noOff);

    // Elite Bull Fighter (type 4): MODEL_SPEAR+7 = Spear08.bmd, R-Hand bone 42
    // Main 5.2: shares Monster01.bmd, same bone layout as Bull Fighter
    loadMonsterWeapon(4, "Spear08.bmd", 42, noRot, noOff);

    // Lich (type 6): MODEL_STAFF+2 = Staff03.bmd, R-Hand bone 41
    // Main 5.2: c->Weapon[0].LinkBone = 41 (Monster05.bmd "knife_gdf")
    loadMonsterWeapon(6, "Staff03.bmd", 41, noRot, noOff);

    // Thunder Lich (type 9): MODEL_THUNDER_STAFF = Staff04.bmd
    // Main 5.2: shares Monster05.bmd with Lich, LinkBone = 41
    loadMonsterWeapon(9, "Staff04.bmd", 41, noRot, noOff);

    // Hell Hound (type 5): MODEL_FALCHION = Sword08.bmd + MODEL_PLATE_SHIELD = Shield02.bmd
    // Main 5.2: MODEL_HOUND LinkBone[0]=19, LinkBone[1]=14
    loadMonsterWeapon(5, "Sword08.bmd", 19, noRot, noOff);
    loadMonsterWeapon(5, "Shield02.bmd", 14, noRot, noOff);

    // Giant (type 7): MODEL_AXE+2 = Axe03.bmd, DUAL WIELD (both hands)
    // Main 5.2: c->Weapon[0].LinkBone = 41, c->Weapon[1].LinkBone = 32
    // Monster06.bmd: bone 41 = "knife_bone" (R), bone 32 = "hand_bone01" (L)
    loadMonsterWeapon(7, "Axe03.bmd", 41, noRot, noOff);
    loadMonsterWeapon(7, "Axe03.bmd", 32, noRot, noOff);

    // Poison Bull (type 8): MODEL_GREAT_SCYTHE = MODEL_SPEAR+8 = Spear09.bmd
    // Main 5.2: c->Weapon[0].LinkBone = 42 (Monster01.bmd same bone as Bull Fighter)
    loadMonsterWeapon(8, "Spear09.bmd", 42, noRot, noOff);

    // Dark Knight (type 10): MODEL_DOUBLE_BLADE = MODEL_SWORD+13 = Sword14.bmd
    // Main 5.2: c->Weapon[0].LinkBone = 26
    loadMonsterWeapon(10, "Sword14.bmd", 26, noRot, noOff);

    // Hell Spider (type 13): MODEL_SERPENT_STAFF = MODEL_STAFF+2 = Staff03.bmd
    // Main 5.2: c->Weapon[0].LinkBone = 29
    loadMonsterWeapon(13, "Staff03.bmd", 29, noRot, noOff);

    // Cyclops (type 17): MODEL_CRESCENT_AXE = MODEL_AXE+8 = Axe09.bmd
    // Main 5.2: c->Weapon[0].LinkBone = 41 (shares with Lich/Giant)
    loadMonsterWeapon(17, "Axe09.bmd", 41, noRot, noOff);

    // Gorgon (type 18): MODEL_GORGON_STAFF = MODEL_STAFF+4 = Staff05.bmd
    // Main 5.2: c->Weapon[0].LinkBone = 30
    loadMonsterWeapon(18, "Staff05.bmd", 30, noRot, noOff);
  }

  // Bali (type 150) — Elf summon (MONSTER_MODEL_BALI=32 → Monster33.bmd)
  // Kayito: Scale=0.12, melee (attackRange=1), no weapons
  {
    int idx = loadMonsterModel("Monster33.bmd", "Bali", 0.12f, 80.0f, 150.0f);
    if (idx >= 0) {
      // level comes from server viewport packet
      m_models[idx].defense = 100;
      m_models[idx].defenseRate = 75;
      m_models[idx].attackRate = 260;
    }
    m_typeToModel[150] = idx;
  }

  // Load Debris models (not mapped to server types)
  std::string skillPath = dataPath + "/Skill/";
  m_boneModelIdx =
      loadMonsterModel("../Skill/Bone01.bmd", "Bone Debris", 0.5f, 0, 0);
  m_stoneModelIdx =
      loadMonsterModel("../Skill/BigStone01.bmd", "Stone Debris", 0.6f, 0, 0);

  // Arrow projectile model (Main 5.2: MODEL_ARROW → Arrow01.bmd)
  m_arrowModelIdx = loadMonsterModel("../Skill/Arrow01.bmd", "Arrow", 0.8f, 0,
                                     0, 0.0f, skillPath);

  m_modelsLoaded = true;
  std::cout << "[Monster] Models loaded: " << m_models.size() << " types"
            << std::endl;
}

void MonsterManager::AddMonster(uint16_t monsterType, uint8_t gridX,
                                uint8_t gridY, uint8_t dir,
                                uint16_t serverIndex, int hp, int maxHp,
                                uint8_t state, int level) {
  // Dedup: if monster with this serverIndex already exists, update instead
  int existing = FindByServerIndex(serverIndex);
  if (existing >= 0) {
    auto &em = m_monsters[existing];
    em.hp = hp;
    em.maxHp = maxHp;
    return;
  }

  auto it = m_typeToModel.find(monsterType);
  if (it == m_typeToModel.end()) {
    std::cerr << "[Monster] Unknown monster type " << monsterType << " at ("
              << (int)gridX << "," << (int)gridY << "), skipping" << std::endl;
    return;
  }
  int modelIdx = it->second;
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  auto &mdl = m_models[modelIdx];
  MonsterInstance mon;
  mon.modelIdx = modelIdx;
  mon.scale = mdl.scale;
  mon.monsterType = monsterType;
  mon.serverIndex = serverIndex;

  // Name
  auto nameIt = s_monsterNames.find(monsterType);
  if (nameIt != s_monsterNames.end())
    mon.name = nameIt->second;

  // Grid to world: WorldX = gridY * 100, WorldZ = gridX * 100
  // Small random offset to prevent stacking
  float randX = ((float)(rand() % 60) - 30.0f);
  float randZ = ((float)(rand() % 60) - 30.0f);
  float worldX = (float)gridY * 100.0f + randX;
  float worldZ = (float)gridX * 100.0f + randZ;
  float worldY = snapToTerrain(worldX, worldZ) + mdl.bodyOffset;
  mon.position = glm::vec3(worldX, worldY, worldZ);
  mon.spawnPosition = mon.position;

  // Direction to facing angle (same as NPC: dir-1 * 45°)
  mon.facing = (float)(dir - 1) * (float)M_PI / 4.0f;

  // Random bob timer offset so monsters don't bob in sync
  mon.bobTimer = (float)(m_monsters.size() * 1.7f);

  // Random animation offset so monsters don't sync
  mon.animFrame = (float)(m_monsters.size() * 2.3f);

  // Compute initial bone matrices (use animBmd for skeleton types)
  auto bones = ComputeBoneMatrices(mdl.getAnimBmd());

  // Upload meshes (mesh data from bmd, bones from animBmd)
  AABB aabb{};
  for (auto &mesh : mdl.bmd->Meshes) {
    UploadMeshWithBones(mesh, mdl.texDir, bones, mon.meshBuffers, aabb, true);
  }

  // Create shadow mesh buffers — sized for triangle-expanded vertices
  for (int mi = 0;
       mi < (int)mdl.bmd->Meshes.size() && mi < (int)mon.meshBuffers.size();
       ++mi) {
    auto &mesh = mdl.bmd->Meshes[mi];
    MonsterInstance::ShadowMesh sm;
    // Count actual shadow vertices: 3 per tri, 6 per quad
    int shadowVertCount = 0;
    for (int t = 0; t < mesh.NumTriangles; ++t) {
      shadowVertCount += (mesh.Triangles[t].Polygon == 4) ? 6 : 3;
    }
    sm.vertexCount = shadowVertCount;
    if (sm.vertexCount == 0) {
      mon.shadowMeshes.push_back(sm);
      continue;
    }
    bgfx::VertexLayout shadowLayout;
    shadowLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    sm.vbo = bgfx::createDynamicVertexBuffer(
        sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
    mon.shadowMeshes.push_back(sm);
  }

  mon.hp = hp;
  mon.maxHp = maxHp > 0 ? maxHp : hp;
  mon.level = level;
  mon.state = static_cast<MonsterState>(state);
  // Fade in all monsters (including initial sync) so they don't pop in
  mon.spawnAlpha = 0.0f;
  if (mon.state == MonsterState::DEAD || mon.state == MonsterState::DYING) {
    mon.corpseAlpha = 0.0f;
  }

  // Create per-instance weapon mesh buffers (skeleton types)
  for (auto &wd : mdl.weaponDefs) {
    MonsterInstance::WeaponMeshSet wms;
    if (wd.bmd) {
      AABB wpnAABB{};
      for (auto &mesh : wd.bmd->Meshes) {
        UploadMeshWithBones(mesh, wd.texDir, {}, wms.meshBuffers, wpnAABB,
                            true);
      }
    }
    mon.weaponMeshes.push_back(std::move(wms));

    // Create shadow meshes for this weapon
    MonsterInstance::WeaponShadowSet wss;
    if (wd.bmd) {
      for (auto &mesh : wd.bmd->Meshes) {
        MonsterInstance::ShadowMesh sm;
        int shadowVertCount = 0;
        for (int t = 0; t < mesh.NumTriangles; ++t)
          shadowVertCount += (mesh.Triangles[t].Polygon == 4) ? 6 : 3;
        sm.vertexCount = shadowVertCount;
        if (sm.vertexCount > 0) {
          bgfx::VertexLayout shadowLayout;
          shadowLayout.begin()
              .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
              .end();
          sm.vbo = bgfx::createDynamicVertexBuffer(
              sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
        }
        wss.meshes.push_back(sm);
      }
    }
    mon.weaponShadowMeshes.push_back(std::move(wss));
  }

  m_monsters.push_back(std::move(mon));
}

void MonsterManager::setAction(MonsterInstance &mon, int action) {
  // Always restart attack animations so repeated attacks play properly
  bool isAttack = (action == ACTION_ATTACK1 || action == ACTION_ATTACK2);
  if (mon.action == action && !isAttack)
    return;

  // Trigger blending for ALL animation changes
  mon.priorAction = mon.action;
  mon.priorAnimFrame = mon.animFrame;
  mon.isBlending = true;
  mon.blendAlpha = 0.0f;

  mon.action = action;
  mon.animFrame = 0.0f;
}

// Per-action animation speed with per-type overrides (ZzzOpenData.cpp
// OpenMonsterModel)
float MonsterManager::getAnimSpeed(uint16_t monsterType, int action) const {
  float speed;
  switch (action) {
  case ACTION_STOP1:
    speed = 0.25f;
    break;
  case ACTION_STOP2:
    speed = 0.20f;
    break;
  case ACTION_WALK:
    speed = 0.34f;
    break;
  case ACTION_ATTACK1:
  case ACTION_ATTACK2:
    speed = 0.33f;
    break;
  case ACTION_SHOCK:
    speed = 0.50f;
    break;
  case ACTION_DIE:
    speed = 0.55f;
    break;
  default:
    speed = 0.25f;
    break;
  }

  // Global per-type multipliers (ZzzOpenData.cpp:2370-2376)
  // NOTE: These use monster MODEL indices (not type IDs):
  //   Model 3 = Dark Knight (type 10), Model 5 = Giant (type 7)
  if (monsterType == 10) { // Dark Knight (model 3)
    speed *= 1.2f;
  } else if (monsterType == 7) { // Giant (model 5)
    speed *= 0.7f;
  } else if (monsterType == 32) { // Stone Golem (model 25)
    speed *= 0.7f;
  }

  // Specific walk speed overrides (ZzzOpenData.cpp:2378-2392)
  // NOTE: These use MONSTER_MODEL_* enum → our type mapping
  if (action == ACTION_WALK) {
    if (monsterType == 2)
      speed = 0.7f; // Budge Dragon (MONSTER_MODEL_BUDGE_DRAGON=2)
    else if (monsterType == 12)
      speed = 0.6f; // Larva (MONSTER_MODEL_LARVA=6)
    else if (monsterType == 13)
      speed = 0.7f; // Hell Spider (MONSTER_MODEL_HELL_SPIDER=8)
    else if (monsterType == 3)
      speed = 1.2f; // Spider (MONSTER_MODEL_SPIDER=9)
    else if (monsterType == 17)
      speed = 0.28f; // Cyclops (MONSTER_MODEL_CYCLOPS=10)
    else if (monsterType == 19)
      speed = 0.3f; // Yeti (MONSTER_MODEL_YETI=12)
    else if (monsterType == 20)
      speed = 0.28f; // Elite Yeti (MONSTER_MODEL_ELITE_YETI=13)
    else if (monsterType == 24)
      speed = 0.5f; // Worm (MONSTER_MODEL_WORM=17)
    else if (monsterType == 26)
      speed = 0.6f; // Goblin (MONSTER_MODEL_GOBLIN=19)
    else if (monsterType == 27)
      speed = 0.4f; // Chain Scorpion (MONSTER_MODEL_CHAIN_SCORPION=20)
    else if (monsterType == 28)
      speed = 0.5f; // Beetle Monster (MONSTER_MODEL_BEETLE_MONSTER=21)
    else if (monsterType == 33)
      speed = 0.6f; // Elite Goblin (shares MODEL_GOBLIN=19 with Goblin)
  }

  return speed * 25.0f; // Scale to 25fps base
}

// Smooth facing interpolation matching original MU TurnAngle2:
// - If angular error >= 45° (pi/4): snap to target (large correction)
// - Otherwise: exponential decay at 0.5^(dt*25) rate (half remaining error per
// 25fps frame)
static float smoothFacing(float current, float target, float dt) {
  float diff = target - current;
  // Normalize to [-PI, PI]
  while (diff > (float)M_PI)
    diff -= 2.0f * (float)M_PI;
  while (diff < -(float)M_PI)
    diff += 2.0f * (float)M_PI;

  if (std::abs(diff) >= (float)M_PI / 4.0f) {
    return target; // Snap for large turns (original: >= 45°)
  }
  // Exponential decay: 0.5^(dt*25) matches original half-error-per-frame at
  // 25fps
  float factor = 1.0f - std::pow(0.5f, dt * 25.0f);
  float result = current + diff * factor;
  // Normalize result
  while (result > (float)M_PI)
    result -= 2.0f * (float)M_PI;
  while (result < -(float)M_PI)
    result += 2.0f * (float)M_PI;
  return result;
}

// Compute facing angle from movement direction (OpenGL coords)
static float facingFromDir(const glm::vec3 &dir) {
  return atan2f(dir.z, -dir.x);
}

// Play idle sound for a monster transitioning to idle (with cooldown + range check)
void MonsterManager::playIdleSound(MonsterInstance &mon) {
  constexpr float COOLDOWN = 8.0f; // Min seconds between idle sounds per monster
  constexpr float MAX_DIST_SQ = 1200.0f * 1200.0f;
  if (m_worldTime - mon.lastIdleSoundTime < COOLDOWN) return;
  float dx = mon.position.x - m_playerPos.x;
  float dz = mon.position.z - m_playerPos.z;
  if (dx * dx + dz * dz > MAX_DIST_SQ) return;
  mon.lastIdleSoundTime = m_worldTime;
  float px = mon.position.x, py = mon.position.y, pz = mon.position.z;
  switch (mon.monsterType) {
  case 0: SoundManager::Play3D(SOUND_MONSTER_BULL1 + rand() % 2, px, py, pz); break;
  case 1: SoundManager::Play3D(SOUND_MONSTER_HOUND1 + rand() % 2, px, py, pz); break;
  case 2: SoundManager::Play3D(SOUND_MONSTER_BUDGE1, px, py, pz); break;
  case 3: SoundManager::Play3D(SOUND_MONSTER_SPIDER1, px, py, pz); break;
  case 4: SoundManager::Play3D(SOUND_MONSTER_WIZARD1 + rand() % 2, px, py, pz); break;
  case 5: SoundManager::Play3D(SOUND_MONSTER_HOUND1 + rand() % 2, px, py, pz); break;
  case 6: SoundManager::Play3D(SOUND_MONSTER_LARVA1 + rand() % 2, px, py, pz); break;
  case 7: SoundManager::Play3D(SOUND_MONSTER_GIANT1 + rand() % 2, px, py, pz); break;
  case 8: SoundManager::Play3D(SOUND_MONSTER_BULL1 + rand() % 2, px, py, pz); break;
  case 9: SoundManager::Play3D(SOUND_MONSTER_WIZARD1 + rand() % 2, px, py, pz); break;
  case 10: SoundManager::Play3D(SOUND_MONSTER_DARKKNIGHT1 + rand() % 2, px, py, pz); break;
  case 11: SoundManager::Play3D(SOUND_MONSTER_GHOST1 + rand() % 2, px, py, pz); break;
  case 12: SoundManager::Play3D(SOUND_MONSTER_LARVA1 + rand() % 2, px, py, pz); break;
  case 13: SoundManager::Play3D(SOUND_MONSTER_SHADOW1 + rand() % 2, px, py, pz); break;
  case 14: case 15: case 16: SoundManager::Play3D(SOUND_BONE1, px, py, pz); break;
  case 17: SoundManager::Play3D(SOUND_MONSTER_OGRE1 + rand() % 2, px, py, pz); break;
  case 18: SoundManager::Play3D(SOUND_MONSTER_GORGON1 + rand() % 2, px, py, pz); break;
  case 19: SoundManager::Play3D(SOUND_MONSTER_YETI1, px, py, pz); break;
  case 20: SoundManager::Play3D(SOUND_MONSTER_YETI1 + rand() % 2, px, py, pz); break;
  case 22: SoundManager::Play3D(SOUND_MONSTER_ICEMONSTER1 + rand() % 2, px, py, pz); break;
  case 23: SoundManager::Play3D(SOUND_MONSTER_HOMMERD1 + rand() % 2, px, py, pz); break;
  case 24: SoundManager::Play3D(SOUND_MONSTER_WORM1, px, py, pz); break;
  case 25: SoundManager::Play3D(SOUND_MONSTER_ICEQUEEN1 + rand() % 2, px, py, pz); break;
  case 26: case 33: SoundManager::Play3D(SOUND_MONSTER_GOBLIN1 + rand() % 2, px, py, pz); break;
  case 27: SoundManager::Play3D(SOUND_MONSTER_SCORPION1 + rand() % 2, px, py, pz); break;
  case 28: SoundManager::Play3D(SOUND_MONSTER_BEETLE1, px, py, pz); break;
  case 29: SoundManager::Play3D(SOUND_MONSTER_HUNTER1 + rand() % 2, px, py, pz); break;
  case 30: case 31: case 32: SoundManager::Play3D(SOUND_MONSTER_GOLEM1 + rand() % 2, px, py, pz); break;
  case 150: SoundManager::Play3D(SOUND_MONSTER_BALI1 + rand() % 2, px, py, pz); break;
  default: break;
  }
}

void MonsterManager::updateStateMachine(MonsterInstance &mon, float dt) {
  auto &mdl = m_models[mon.modelIdx];
  // Compute local index for TriggerAttackAnimation (deferred attack)
  int monIdx = (int)(&mon - m_monsters.data());

  switch (mon.state) {
  case MonsterState::IDLE: {
    // Own summon: spring-damper follow when idle (not in combat)
    // Reacts instantly to player movement with smooth acceleration/deceleration
    if (mon.serverIndex == m_ownSummonIndex && m_ownSummonIndex != 0) {
      // Estimate player velocity from position delta
      glm::vec3 playerVel = (m_playerPos - m_prevPlayerPos) / std::max(dt, 0.001f);
      playerVel.y = 0.0f;
      float playerSpeed = glm::length(playerVel);
      bool playerMoving = playerSpeed > 20.0f;

      // Follow distance: behind player, far enough to avoid model overlap
      // Bigger summons (Bali) need more space than small ones
      float followDist = playerMoving ? 160.0f : 190.0f;

      // Determine follow direction: use click-to-move path for predictive pathing
      // When player is walking, summon trails behind the travel direction
      // When stopped, summon stays behind player's facing
      glm::vec3 target;
      if (m_playerIsMoving && playerMoving) {
        // Player actively pathing — use travel direction (from velocity)
        // so summon follows behind the direction of travel, not behind facing
        glm::vec3 travelDir = glm::normalize(playerVel);
        target.x = m_playerPos.x - travelDir.x * followDist;
        target.z = m_playerPos.z - travelDir.z * followDist;
        target.y = 0.0f;

        // Lead toward the click-to-move destination so summon anticipates turns
        glm::vec3 toGoal = m_playerMoveTarget - m_playerPos;
        toGoal.y = 0.0f;
        float goalDist = glm::length(toGoal);
        if (goalDist > 50.0f) {
          glm::vec3 goalDir = toGoal / goalDist;
          // Blend 20% toward the destination direction for turn anticipation
          target.x += goalDir.x * playerSpeed * 0.2f;
          target.z += goalDir.z * playerSpeed * 0.2f;
        }
      } else {
        // Player standing — offset behind facing direction
        target.x = m_playerPos.x - cosf(m_playerFacing) * followDist;
        target.z = m_playerPos.z - sinf(m_playerFacing) * followDist;
        target.y = 0.0f;
      }

      glm::vec3 toTarget = target - mon.position;
      toTarget.y = 0.0f;
      float dist = glm::length(toTarget);

      // Spring-damper: F = -k*displacement - c*velocity
      // High stiffness for instant response, strong damping to prevent overshoot
      const float SPRING_K = 12.0f;  // Spring stiffness
      const float DAMPING = 5.5f;    // Damping coefficient
      const float MAX_SPEED = 500.0f;

      if (dist > 15.0f) {
        glm::vec3 springForce = toTarget * SPRING_K;
        glm::vec3 dampForce = -m_summonVelocity * DAMPING;
        glm::vec3 accel = springForce + dampForce;

        m_summonVelocity.x += accel.x * dt;
        m_summonVelocity.z += accel.z * dt;

        // Clamp max speed
        float vel = glm::length(glm::vec2(m_summonVelocity.x, m_summonVelocity.z));
        if (vel > MAX_SPEED) {
          m_summonVelocity.x *= MAX_SPEED / vel;
          m_summonVelocity.z *= MAX_SPEED / vel;
        }

        mon.position.x += m_summonVelocity.x * dt;
        mon.position.z += m_summonVelocity.z * dt;

        // Face movement direction
        if (vel > 30.0f) {
          glm::vec3 moveDir = glm::normalize(glm::vec3(m_summonVelocity.x, 0, m_summonVelocity.z));
          mon.facing = smoothFacing(mon.facing, facingFromDir(moveDir), dt * 8.0f);
        }

        // Walk animation when moving visibly
        if (vel > 60.0f)
          setAction(mon, ACTION_WALK);
        else
          setAction(mon, ACTION_STOP1);
      } else {
        // Close enough — decelerate and face player
        m_summonVelocity *= std::max(0.0f, 1.0f - 8.0f * dt);
        glm::vec3 toPlayer = m_playerPos - mon.position;
        toPlayer.y = 0.0f;
        if (glm::length(toPlayer) > 1.0f) {
          glm::vec3 fpDir = glm::normalize(toPlayer);
          mon.facing = smoothFacing(mon.facing, facingFromDir(fpDir), dt * 3.0f);
        }
        setAction(mon, ACTION_STOP1);
      }

      float terrainY = snapToTerrain(mon.position.x, mon.position.z);
      mon.position.y = terrainY + mdl.bodyOffset;
      mon.stateTimer -= dt;
      break;
    }

    // If we just entered IDLE or finished an idle cycle, pick a new action and
    // duration
    if (mon.stateTimer <= 0.0f) {
      // 80% chance for STOP1, 20% for STOP2 (matches original MU feel)
      int nextIdle = (rand() % 100 < 80) ? ACTION_STOP1 : ACTION_STOP2;
      setAction(mon, nextIdle);
      // Stay in this idle action for 2-5 seconds
      mon.stateTimer = 2.0f + static_cast<float>(rand() % 3000) / 1000.0f;
    }

    float terrainY = snapToTerrain(mon.position.x, mon.position.z);
    mon.position.y = terrainY + mdl.bodyOffset;
    // Budge Dragon hover (ZzzCharacter.cpp:6224): -abs(sin(Timer))*70+70
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }

    mon.stateTimer -= dt;
    break;
  }

  case MonsterState::WALKING: {
    float maxT = std::max(0.0f, (float)mon.splinePoints.size() - 1.0f);
    if (mon.splinePoints.size() < 2 || mon.splineT >= maxT) {
      // Path exhausted — check for deferred attack
      if (mon.pendingAttack) {
        mon.pendingAttack = false;
        TriggerAttackAnimation(monIdx);
        break;
      }
      // Idle
      mon.state = MonsterState::IDLE;
      playIdleSound(mon);
      mon.stateTimer = 0.0f;
      mon.splinePoints.clear();
      mon.splineT = 0.0f;
    } else {
      setAction(mon, ACTION_WALK);
      mon.splineT = std::min(mon.splineT + mon.splineRate * dt, maxT);
      glm::vec3 p = evalCatmullRom(mon.splinePoints, mon.splineT);
      mon.position.x = p.x;
      mon.position.z = p.z;
      // Face along spline tangent
      glm::vec3 tang = evalCatmullRomTangent(mon.splinePoints, mon.splineT);
      tang.y = 0.0f;
      if (glm::length(tang) > 0.01f)
        mon.facing =
            smoothFacing(mon.facing, facingFromDir(glm::normalize(tang)), dt);
    }
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    break;
  }

  case MonsterState::CHASING: {
    // Server-authoritative: follow spline, face player when path exhausted
    // Server APPROACHING state handles the melee gap delay
    float maxT = std::max(0.0f, (float)mon.splinePoints.size() - 1.0f);
    bool pathExhausted =
        mon.splinePoints.size() < 2 || mon.splineT >= maxT;

    if (!pathExhausted) {
      // Follow A* spline toward server target
      setAction(mon, ACTION_WALK);
      mon.splineT = std::min(mon.splineT + mon.splineRate * dt, maxT);
      glm::vec3 p = evalCatmullRom(mon.splinePoints, mon.splineT);
      mon.position.x = p.x;
      mon.position.z = p.z;
      // Face along tangent
      glm::vec3 tang = evalCatmullRomTangent(mon.splinePoints, mon.splineT);
      tang.y = 0.0f;
      if (glm::length(tang) > 0.01f)
        mon.facing =
            smoothFacing(mon.facing, facingFromDir(glm::normalize(tang)), dt);
    } else {
      // Path exhausted — check for deferred attack from server
      if (mon.pendingAttack) {
        mon.pendingAttack = false;
        TriggerAttackAnimation(monIdx);
        break;
      }
      // Idle, face target, wait for server attack packet
      setAction(mon, ACTION_STOP1);
      glm::vec3 facePos = m_playerPos;
      bool hasFace = !m_playerDead;
      // Monster with an attack target (summon or monster): face that target
      if (mon.attackTargetLocalIdx >= 0 &&
          mon.attackTargetLocalIdx < (int)m_monsters.size()) {
        auto &tgt = m_monsters[mon.attackTargetLocalIdx];
        if (tgt.state != MonsterState::DEAD) {
          facePos = tgt.position;
          hasFace = true;
        }
      }
      if (hasFace) {
        glm::vec3 toTarget = facePos - mon.position;
        toTarget.y = 0.0f;
        if (glm::length(toTarget) > 1.0f) {
          glm::vec3 fdir = glm::normalize(toTarget);
          mon.facing = smoothFacing(mon.facing, facingFromDir(fdir), dt);
        }
      }
    }
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    break;
  }

  case MonsterState::ATTACKING: {
    // Face the attack target during attack animation
    glm::vec3 facePos = m_playerPos; // Default: face player
    bool hasFaceTarget = !m_playerDead;

    // Monster with an attack target (summon or monster): face that target
    if (mon.attackTargetLocalIdx >= 0 &&
        mon.attackTargetLocalIdx < (int)m_monsters.size()) {
      auto &tgt = m_monsters[mon.attackTargetLocalIdx];
      if (tgt.state != MonsterState::DEAD) {
        facePos = tgt.position;
        hasFaceTarget = true;
      }
    }

    if (hasFaceTarget) {
      glm::vec3 toTarget = facePos - mon.position;
      toTarget.y = 0.0f;
      if (glm::length(toTarget) > 1.0f) {
        glm::vec3 dir = glm::normalize(toTarget);
        mon.facing = smoothFacing(mon.facing, facingFromDir(dir), dt);
      }
    }
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    if (mon.stateTimer <= 0.0f) {
      // After attack, check deferred server position
      glm::vec3 diff = mon.serverTargetPos - mon.position;
      diff.y = 0.0f;
      float dist = glm::length(diff);
      if (mon.serverChasing) {
        // In combat: stay in place for small moves, only walk for large ones
        // (prevents sliding/teleporting between attacks)
        if (dist > 300.0f) {
          // Very large reposition (player teleported/ran far): walk there
          mon.splinePoints.clear();
          mon.splinePoints.push_back(mon.position);
          mon.splinePoints.push_back(mon.serverTargetPos);
          mon.splineT = 0.0f;
          mon.splineRate = (dist > 1.0f) ? CHASE_SPEED / dist : 2.5f;
          mon.state = MonsterState::CHASING;
        } else {
          // Small/medium reposition: just stay put, face player, wait
          mon.state = MonsterState::CHASING;
          mon.splinePoints.clear();
        }
      } else {
        // Not in combat: normal idle/walk transition
        if (dist > 50.0f) {
          mon.splinePoints.clear();
          mon.splinePoints.push_back(mon.position);
          mon.splinePoints.push_back(mon.serverTargetPos);
          mon.splineT = 0.0f;
          mon.splineRate = (dist > 1.0f) ? WANDER_SPEED / dist : 2.5f;
          mon.state = MonsterState::WALKING;
        } else {
          mon.state = MonsterState::IDLE;
          if (mon.serverIndex == m_ownSummonIndex && m_ownSummonIndex != 0)
            m_summonVelocity = glm::vec3(0.0f);
          playIdleSound(mon);
        }
      }
    }
    break;
  }

  case MonsterState::HIT: {
    setAction(mon, ACTION_SHOCK);
    // Maintain Y position during hit stun
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    if (mon.monsterType == 2) {
      mon.bobTimer += dt * 3.75f;
      mon.position.y += (-std::abs(std::sin(mon.bobTimer)) * 30.0f + 30.0f);
    }
    mon.stateTimer -= dt;
    if (mon.stateTimer <= 0.0f) {
      // After hit stun, check deferred server position
      glm::vec3 diff = mon.serverTargetPos - mon.position;
      diff.y = 0.0f;
      float dist = glm::length(diff);
      if (mon.serverChasing) {
        // In combat: stay in place for small moves, only walk for large ones
        if (dist > 300.0f) {
          mon.splinePoints.clear();
          mon.splinePoints.push_back(mon.position);
          mon.splinePoints.push_back(mon.serverTargetPos);
          mon.splineT = 0.0f;
          mon.splineRate = (dist > 1.0f) ? CHASE_SPEED / dist : 2.5f;
          mon.state = MonsterState::CHASING;
        } else {
          mon.state = MonsterState::CHASING;
          mon.splinePoints.clear();
        }
      } else {
        if (dist > 50.0f) {
          mon.splinePoints.clear();
          mon.splinePoints.push_back(mon.position);
          mon.splinePoints.push_back(mon.serverTargetPos);
          mon.splineT = 0.0f;
          mon.splineRate = (dist > 1.0f) ? WANDER_SPEED / dist : 2.5f;
          mon.state = MonsterState::WALKING;
        } else {
          mon.state = MonsterState::IDLE;
          if (mon.serverIndex == m_ownSummonIndex && m_ownSummonIndex != 0)
            m_summonVelocity = glm::vec3(0.0f);
          playIdleSound(mon);
        }
      }
    }
    break;
  }

  case MonsterState::DYING: {
    setAction(mon, ACTION_DIE);
    // On death: snap to terrain + bodyOffset, no hover (ZzzCharacter.cpp:6285)
    mon.position.y =
        snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;

    // Death smoke effects (Main 5.2: MonsterDieSandSmoke / smoke particles)
    if (!mon.deathSmokeDone && m_vfxManager && mon.animFrame >= 8.0f) {
      int type = mon.monsterType;
      if (type == 5 || type == 7) {
        // Hell Hound (5) + Giant (7): MonsterDieSandSmoke — 20 particle burst
        m_vfxManager->SpawnBurst(ParticleType::SMOKE, mon.position, 20);
        mon.deathSmokeDone = true;
      } else if (type == 8 || type == 9 || type == 10 || type == 11 ||
                 type == 12 || type == 13 || type == 15 || type == 16 ||
                 type == 17 || type == 18) {
        // Dungeon monsters: smoke puffs on death (Main 5.2: ~25% chance, 1-2 particles)
        m_vfxManager->SpawnBurst(ParticleType::SMOKE, mon.position, 5);
        mon.deathSmokeDone = true;
      }
    }

    int numKeys = 1;
    BMDData *aBmd = mdl.getAnimBmd();
    int mappedDie = mdl.actionMap[ACTION_DIE];
    if (mappedDie < (int)aBmd->Actions.size())
      numKeys = aBmd->Actions[mappedDie].NumAnimationKeys;
    if (mon.animFrame >= (float)(numKeys - 1)) {
      mon.animFrame = (float)(numKeys - 1);
      mon.state = MonsterState::DEAD;
      mon.stateTimer = 0.0f;
    }
    break;
  }

  case MonsterState::DEAD: {
    mon.corpseTimer += dt;
    if (mon.corpseTimer < CORPSE_FADE_TIME) {
      mon.corpseAlpha = 1.0f - (mon.corpseTimer / CORPSE_FADE_TIME);
    } else {
      mon.corpseAlpha = 0.0f;
    }
    break;
  }

  default:
    mon.state = MonsterState::IDLE;
    playIdleSound(mon);
    break;
  }
}

void MonsterManager::Update(float deltaTime) {
  m_worldTime += deltaTime;
  int idx = 0;
  for (auto &mon : m_monsters) {
    // Safety: if HP is 0 but monster isn't dying/dead, force death
    // (catches missed 0x2A packets or race conditions)
    if (mon.hp <= 0 && mon.state != MonsterState::DYING &&
        mon.state != MonsterState::DEAD) {
      mon.state = MonsterState::DYING;
      mon.stateTimer = 0.0f;
      setAction(mon, ACTION_DIE);
    }

    // Main 5.2: PushingCharacter — StormTime spin (Twister stun)
    // Monster is stunned during spin: skip state machine, apply smooth rotation
    if (mon.stormTime > 0) {
      // Smooth rotation: current tick rate = stormTime * 10 degrees per 0.04s
      // = stormTime * 250 degrees/second, applied every frame
      float degPerSec = (float)(mon.stormTime) * 250.0f;
      mon.facing += degPerSec * (3.14159f / 180.0f) * deltaTime;

      // Decrement stormTime at tick boundaries
      mon.stormTickTimer += deltaTime;
      while (mon.stormTickTimer >= 0.04f && mon.stormTime > 0) {
        mon.stormTickTimer -= 0.04f;
        mon.stormTime--;
      }
    } else {
      updateStateMachine(mon, deltaTime);
    }

    idx++;
  }

  // Original MU: monsters ghost through each other — no separation

  updateDebris(deltaTime);
  updateArrows(deltaTime);
}

void MonsterManager::ClearMonsters() {
  for (auto &mon : m_monsters) {
    CleanupMeshBuffers(mon.meshBuffers);
    for (auto &wms : mon.weaponMeshes)
      CleanupMeshBuffers(wms.meshBuffers);
    for (auto &sm : mon.shadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    for (auto &wss : mon.weaponShadowMeshes) {
      for (auto &sm : wss.meshes) {
        if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
      }
    }
  }
  m_monsters.clear();
  m_arrows.clear();
  m_summonServerIndices.clear();
  m_ownSummonIndex = 0;
  m_summonVelocity = glm::vec3(0.0f);
  m_prevPlayerPos = m_playerPos;
}

MonsterInfo MonsterManager::GetMonsterInfo(int index) const {
  MonsterInfo info{};
  if (index < 0 || index >= (int)m_monsters.size())
    return info;
  const auto &mon = m_monsters[index];
  const auto &mdl = m_models[mon.modelIdx];
  info.position = mon.position;
  info.radius = mdl.collisionRadius;
  // Use AABB-based height if available, fallback to collisionHeight
  float aabbHeight = mdl.meshBounds.max.z * mdl.scale;
  info.height = (aabbHeight > 10.0f) ? aabbHeight : mdl.collisionHeight;
  info.bodyOffset = mdl.bodyOffset;
  info.name = mon.name;
  info.type = mon.monsterType;
  info.serverIndex = mon.serverIndex;
  info.level = (mon.levelOverride > 0) ? mon.levelOverride
             : (mon.level > 0)        ? mon.level
                                      : mdl.level;
  info.hp = mon.hp;
  info.maxHp = mon.maxHp;
  info.defense = mdl.defense;
  info.defenseRate = mdl.defenseRate;
  info.state = mon.state;
  return info;
}

uint16_t MonsterManager::GetServerIndex(int index) const {
  if (index < 0 || index >= (int)m_monsters.size())
    return 0;
  return m_monsters[index].serverIndex;
}

int MonsterManager::FindByServerIndex(uint16_t serverIndex) const {
  for (int i = 0; i < (int)m_monsters.size(); i++) {
    if (m_monsters[i].serverIndex == serverIndex)
      return i;
  }
  return -1;
}

void MonsterManager::ApplyStormTime(uint16_t serverIndex, int ticks) {
  int idx = FindByServerIndex(serverIndex);
  if (idx < 0)
    return;
  auto &mon = m_monsters[idx];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  // Only apply if not already spinning (don't reset mid-spin)
  if (mon.stormTime <= 0) {
    mon.stormTime = ticks;
    mon.stormTickTimer = 0.0f;
  }
}

// ─── Summon tracking ────────────────────────────────────────────────────────

void MonsterManager::MarkAsSummon(uint16_t serverIndex, bool isOwn, int level) {
  m_summonServerIndices.insert(serverIndex);
  if (isOwn)
    m_ownSummonIndex = serverIndex;
  if (level > 0) {
    int idx = FindByServerIndex(serverIndex);
    if (idx >= 0)
      m_monsters[idx].levelOverride = level;
  }
}

void MonsterManager::ClearSummon(uint16_t serverIndex) {
  m_summonServerIndices.erase(serverIndex);
  if (m_ownSummonIndex == serverIndex)
    m_ownSummonIndex = 0;

  // Force the monster to disappear immediately (skip death animation)
  int idx = FindByServerIndex(serverIndex);
  if (idx >= 0) {
    auto &mon = m_monsters[idx];
    mon.hp = 0;
    mon.state = MonsterState::DEAD;
    mon.corpseAlpha = 0.0f;
    mon.corpseTimer = 999.0f; // Already faded
  }
}

bool MonsterManager::IsSummon(uint16_t serverIndex) const {
  return m_summonServerIndices.count(serverIndex) > 0;
}

bool MonsterManager::IsOwnSummon(int monsterLocalIndex) const {
  if (monsterLocalIndex < 0 || monsterLocalIndex >= (int)m_monsters.size())
    return false;
  return m_monsters[monsterLocalIndex].serverIndex == m_ownSummonIndex &&
         m_ownSummonIndex != 0;
}

void MonsterManager::FaceTarget(int attackerIdx, int targetIdx) {
  if (attackerIdx < 0 || attackerIdx >= (int)m_monsters.size())
    return;
  if (targetIdx < 0 || targetIdx >= (int)m_monsters.size())
    return;
  auto &attacker = m_monsters[attackerIdx];
  auto &target = m_monsters[targetIdx];
  attacker.attackTargetLocalIdx = targetIdx;
  glm::vec3 dir = target.position - attacker.position;
  dir.y = 0.0f;
  float dist = glm::length(dir);

  // Own summon: don't snap position — let spline movement handle approach.
  // Only face the target.

  if (glm::length(dir) > 0.01f)
    attacker.facing = atan2f(dir.z, -dir.x);
}

void MonsterManager::SetMonsterHP(int index, int hp, int maxHp) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  m_monsters[index].hp = hp;
  m_monsters[index].maxHp = maxHp;
}

void MonsterManager::SetMonsterDying(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state != MonsterState::DYING && mon.state != MonsterState::DEAD) {
    mon.hp = 0;
    mon.state = MonsterState::DYING;
    mon.stateTimer = 0.0f;
    mon.pendingAttack = false;
    setAction(mon, ACTION_DIE);

    // Monster death sounds (Main 5.2: PlayMonsterSound) — distance gated
    {
      float dx = mon.position.x - m_playerPos.x;
      float dz = mon.position.z - m_playerPos.z;
      if (dx * dx + dz * dz < 1200.0f * 1200.0f) {
      float px = mon.position.x, py = mon.position.y, pz = mon.position.z;
      switch (mon.monsterType) {
      case 0: // Bull Fighter
        SoundManager::Play3D(SOUND_MONSTER_BULLDIE, px, py, pz);
        break;
      case 1: // Hound
        SoundManager::Play3D(SOUND_MONSTER_HOUNDDIE, px, py, pz);
        break;
      case 2: // Budge Dragon
        SoundManager::Play3D(SOUND_MONSTER_BUDGEDIE, px, py, pz);
        break;
      case 3: // Spider
        SoundManager::Play3D(SOUND_MONSTER_SPIDER1, px, py, pz);
        break;
      case 4: // Elite Bull Fighter (Wizard sounds)
        SoundManager::Play3D(SOUND_MONSTER_WIZARDDIE, px, py, pz);
        break;
      case 5: // Hell Hound — reuses Hound sounds
        SoundManager::Play3D(SOUND_MONSTER_HOUNDDIE, px, py, pz);
        break;
      case 6: // Lich — death uses mLarva2
      case 9: // Thunder Lich
      case 12: // Larva
        SoundManager::Play3D(SOUND_MONSTER_LARVA2, px, py, pz);
        break;
      case 7: // Giant
        SoundManager::Play3D(SOUND_MONSTER_GIANTDIE, px, py, pz);
        break;
      case 8: // Poison Bull — reuses Bull sounds
        SoundManager::Play3D(SOUND_MONSTER_BULLDIE, px, py, pz);
        break;
      case 10: // Dark Knight monster — reuses DarkKnight idle as death
        SoundManager::Play3D(SOUND_MONSTER_DARKKNIGHT2, px, py, pz);
        break;
      case 11: // Ghost — Main 5.2: mGhostDie
        SoundManager::Play3D(SOUND_MONSTER_GHOSTDIE, px, py, pz);
        break;
      case 13: // Hell Spider — Main 5.2: mShadowDie
        SoundManager::Play3D(SOUND_MONSTER_SHADOWDIE, px, py, pz);
        break;
      case 17: // Cyclops — Main 5.2: mOgreDie
        SoundManager::Play3D(SOUND_MONSTER_OGREDIE, px, py, pz);
        break;
      case 18: // Gorgon — Main 5.2: mGorgonDie
        SoundManager::Play3D(SOUND_MONSTER_GORGONDIE, px, py, pz);
        break;
      case 14: // Skeleton Warrior — Main 5.2: SOUND_BONE2 on death
      case 15: // Skeleton Archer
      case 16: // Skeleton Captain
        SoundManager::Play3D(SOUND_BONE2, px, py, pz);
        break;
      // Devias monsters (types 19-25)
      case 19: // Yeti
      case 20: // Elite Yeti
        SoundManager::Play3D(SOUND_MONSTER_YETIDIE, px, py, pz);
        break;
      case 21: // Assassin
        SoundManager::Play3D(SOUND_MONSTER_ASSASSINDIE, px, py, pz);
        break;
      case 22: // Ice Monster
        SoundManager::Play3D(SOUND_MONSTER_ICEMONSTERDIE, px, py, pz);
        break;
      case 23: // Hommerd
        SoundManager::Play3D(SOUND_MONSTER_HOMMERDDIE, px, py, pz);
        break;
      case 24: // Worm
        SoundManager::Play3D(SOUND_MONSTER_WORMDIE, px, py, pz);
        break;
      case 25: // Ice Queen
        SoundManager::Play3D(SOUND_MONSTER_ICEQUEENDIE, px, py, pz);
        break;
      // Noria monsters (types 26-33)
      case 26: // Goblin
      case 33: // Elite Goblin
        SoundManager::Play3D(SOUND_MONSTER_GOBLINDIE, px, py, pz);
        break;
      case 27: // Chain Scorpion
        SoundManager::Play3D(SOUND_MONSTER_SCORPIONDIE, px, py, pz);
        break;
      case 28: // Beetle Monster
        SoundManager::Play3D(SOUND_MONSTER_BEETLEDIE, px, py, pz);
        break;
      case 29: // Hunter
        SoundManager::Play3D(SOUND_MONSTER_HUNTERDIE, px, py, pz);
        break;
      case 30: // Forest Monster
      case 31: // Agon
      case 32: // Stone Golem
      case 150: // Bali (Elf summon) — no unique death sound, use Golem
        SoundManager::Play3D(SOUND_MONSTER_GOLEMDIE, px, py, pz);
        break;
      default: break;
      }
      }
    }

    // Spawn death debris (Main 5.2 ZzzCharacter.cpp:1386, 1401, 1412)
    if (mon.monsterType == 14 || mon.monsterType == 15 ||
        mon.monsterType == 16) { // All skeleton types
      spawnDebris(m_boneModelIdx, mon.position + glm::vec3(0, 50, 0), 6);
    } else if (mon.monsterType == 7) { // Giant
      spawnDebris(m_stoneModelIdx, mon.position + glm::vec3(0, 80, 0), 8);
    }
  }
}

void MonsterManager::TriggerHitAnimation(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  // Don't interrupt attack animation with hit flinch — the server keeps the
  // monster in ATTACKING state, so interrupting causes a visual glitch where
  // the monster flinches mid-swing then immediately attacks again.
  // Damage numbers still appear; only the flinch animation is suppressed.
  if (mon.state == MonsterState::ATTACKING)
    return;
  mon.state = MonsterState::HIT;
  mon.stateTimer = 0.5f;
}

void MonsterManager::TriggerAttackAnimation(int index) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;
  if (mon.stormTime > 0)
    return; // Don't attack while spinning from Twister stun

  // Server-authoritative: attack packet means monster is ready to attack.
  mon.serverChasing = true;

  // If still mid-spline (walking to target), defer the attack until the walk
  // finishes so the monster smoothly approaches before attacking — no teleport.
  if ((mon.state == MonsterState::CHASING || mon.state == MonsterState::WALKING) &&
      mon.splinePoints.size() >= 2 &&
      mon.splineT < (float)(mon.splinePoints.size() - 1) - 0.1f) {
    mon.pendingAttack = true;
    return;
  }

  mon.pendingAttack = false;
  mon.state = MonsterState::ATTACKING;
  // Attack animation duration based on action keys / speed
  auto &mdl = m_models[mon.modelIdx];
  // Main 5.2 pattern: SwordCount % 3 == 0 → ATTACK1, else ATTACK2
  int atk = (mon.swordCount % 3 == 0) ? ACTION_ATTACK1 : ACTION_ATTACK2;
  mon.swordCount++;
  int numKeys = 1;
  BMDData *aBmd = mdl.getAnimBmd();
  int mappedAtk = mdl.actionMap[atk];
  if (mappedAtk < (int)aBmd->Actions.size())
    numKeys = aBmd->Actions[mappedAtk].NumAnimationKeys;
  float speed = getAnimSpeed(mon.monsterType, atk);
  mon.stateTimer =
      (numKeys > 1 && speed > 0.0f) ? (float)numKeys / speed : 1.0f;
  setAction(mon, atk);

  // Monster attack sounds (Main 5.2: PlayMonsterSound) — distance gated
  {
    float dx = mon.position.x - m_playerPos.x;
    float dz = mon.position.z - m_playerPos.z;
    if (dx * dx + dz * dz < 1200.0f * 1200.0f) {
    float px = mon.position.x, py = mon.position.y, pz = mon.position.z;
    switch (mon.monsterType) {
    case 0: // Bull Fighter
      SoundManager::Play3D(SOUND_MONSTER_BULLATTACK1 + rand() % 2, px, py, pz);
      break;
    case 1: // Hound
      SoundManager::Play3D(SOUND_MONSTER_HOUNDATTACK1 + rand() % 2, px, py, pz);
      break;
    case 2: // Budge Dragon
      SoundManager::Play3D(SOUND_MONSTER_BUDGEATTACK1, px, py, pz);
      break;
    case 3: // Spider (all states use mSpider1.wav in Main 5.2)
      SoundManager::Play3D(SOUND_MONSTER_SPIDER1, px, py, pz);
      break;
    case 4: // Elite Bull Fighter (Wizard sounds)
      SoundManager::Play3D(SOUND_MONSTER_WIZARDATTACK1 + rand() % 2, px, py, pz);
      break;
    case 6: // Lich — idle sound + Main 5.2: SOUND_THUNDER01 on lightning cast
      SoundManager::Play3D(SOUND_MONSTER_LARVA1 + rand() % 2, px, py, pz);
      SoundManager::Play3D(SOUND_LICH_THUNDER, px, py, pz);
      break;
    case 5: // Hell Hound — reuses Hound attack sounds
      SoundManager::Play3D(SOUND_MONSTER_HOUNDATTACK1 + rand() % 2, px, py, pz);
      break;
    case 7: // Giant
      SoundManager::Play3D(SOUND_MONSTER_GIANTATTACK1 + rand() % 2, px, py, pz);
      break;
    case 8: // Poison Bull — reuses Bull attack sounds
      SoundManager::Play3D(SOUND_MONSTER_BULLATTACK1 + rand() % 2, px, py, pz);
      break;
    case 9: // Thunder Lich — Main 5.2: Wizard attack + thunder
      SoundManager::Play3D(SOUND_MONSTER_WIZARDATTACK1 + rand() % 2, px, py, pz);
      SoundManager::Play3D(SOUND_LICH_THUNDER, px, py, pz);
      break;
    case 10: // Dark Knight monster
      SoundManager::Play3D(SOUND_MONSTER_DARKKNIGHT1 + rand() % 2, px, py, pz);
      break;
    case 11: // Ghost — Main 5.2: mGhostAttack
      SoundManager::Play3D(SOUND_MONSTER_GHOSTATTACK1 + rand() % 2, px, py, pz);
      break;
    case 12: // Larva
      SoundManager::Play3D(SOUND_MONSTER_LARVA1 + rand() % 2, px, py, pz);
      break;
    case 13: // Hell Spider — Main 5.2: mShadowAttack
      SoundManager::Play3D(SOUND_MONSTER_SHADOWATTACK1 + rand() % 2, px, py, pz);
      break;
    case 17: // Cyclops — Main 5.2: mOgreAttack
      SoundManager::Play3D(SOUND_MONSTER_OGREATTACK1 + rand() % 2, px, py, pz);
      break;
    case 18: // Gorgon — Main 5.2: mGorgonAttack
      SoundManager::Play3D(SOUND_MONSTER_GORGONATTACK1 + rand() % 2, px, py, pz);
      break;
    case 14: // Skeleton Warrior — Main 5.2: SOUND_BONE1 on attack
    case 15: // Skeleton Archer
    case 16: // Skeleton Captain
      SoundManager::Play3D(SOUND_BONE1, px, py, pz);
      break;
    // Devias monsters (types 19-25) — Main 5.2 SetMonsterSound attack slots
    case 19: // Yeti — Sounds[2]=mYetiAttack1, Sounds[3]=mYetiAttack1
    case 20: // Elite Yeti — same attack sounds as Yeti
      // Main 5.2 only has one Yeti attack sound; pitch-vary to reduce monotony
      SoundManager::Play3DPitched(SOUND_MONSTER_YETIATTACK1, px, py, pz,
                                  0.85f, 1.15f);
      break;
    case 21: // Assassin — Sounds[2]=mAssassinAttack1, Sounds[3]=mAssassinAttack2
      SoundManager::Play3D(SOUND_MONSTER_ASSASSINATTACK1 + rand() % 2, px, py, pz);
      break;
    case 22: // Ice Monster — Sounds[2]=mIceMonster1, Sounds[3]=mIceMonster1 (reuse idle)
      SoundManager::Play3DPitched(SOUND_MONSTER_ICEMONSTER1, px, py, pz,
                                  0.9f, 1.1f);
      break;
    case 23: // Hommerd — Sounds[2]=mHomordAttack1, Sounds[3]=mHomordAttack1
      SoundManager::Play3DPitched(SOUND_MONSTER_HOMMERDATTACK1, px, py, pz,
                                  0.9f, 1.1f);
      break;
    case 24: // Worm — Sounds[2]=mWormDie, Sounds[3]=mWormDie (uses die sound!)
      SoundManager::Play3D(SOUND_MONSTER_WORMDIE, px, py, pz);
      break;
    case 25: // Ice Queen — Sounds[2]=mIceQueenAttack1, Sounds[3]=mIceQueenAttack2
      SoundManager::Play3D(SOUND_MONSTER_ICEQUEENATTACK1 + rand() % 2, px, py, pz);
      break;
    // Noria monsters (types 26-33)
    case 26: // Goblin
    case 33: // Elite Goblin
      SoundManager::Play3D(SOUND_MONSTER_GOBLINATTACK1 + rand() % 2, px, py, pz);
      break;
    case 27: // Chain Scorpion
      SoundManager::Play3D(SOUND_MONSTER_SCORPIONATTACK1 + rand() % 2, px, py, pz);
      break;
    case 28: // Beetle Monster
      SoundManager::Play3D(SOUND_MONSTER_BEETLEATTACK1, px, py, pz);
      break;
    case 29: // Hunter
      SoundManager::Play3D(SOUND_MONSTER_HUNTERATTACK1 + rand() % 2, px, py, pz);
      break;
    case 30: // Forest Monster
    case 31: // Agon
    case 32: // Stone Golem
      SoundManager::Play3D(SOUND_MONSTER_GOLEMATTACK1 + rand() % 2, px, py, pz);
      break;
    case 150: // Bali (Elf summon)
      SoundManager::Play3D(SOUND_MONSTER_BALIATTACK1 + rand() % 2, px, py, pz);
      break;
    default: break;
    }
    }
  }

  // Trigger Lich VFX (Monster Type 6 + Thunder Lich 9) — Main 5.2: two
  // BITMAP_JOINT_THUNDER ribbons (thick scale=50 + thin scale=10) from weapon
  // bone to target
  if ((mon.monsterType == 6 || mon.monsterType == 9) && m_vfxManager) {
    glm::vec3 startPos = mon.position;
    // Try to get weapon bone position (bone 41, Main 5.2 Lich LinkBone)
    // Bone matrices are in model-local space — must apply model rotation
    // (-90°Z, -90°Y, facing) to convert to world space
    if (41 < (int)mon.cachedBones.size()) {
      glm::mat4 modelRot = glm::mat4(1.0f);
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));
      const auto &m = mon.cachedBones[41];
      glm::vec3 boneLocal(m[0][3], m[1][3], m[2][3]);
      glm::vec3 boneWorld = glm::vec3(modelRot * glm::vec4(boneLocal, 1.0f));
      startPos = boneWorld * mon.scale + mon.position;
    } else {
      startPos.y += 100.0f * mon.scale; // Fallback: above head in world space
    }
    // Two-pass ribbon: thick outer + thin inner (Main 5.2 pattern)
    m_vfxManager->SpawnRibbon(startPos, m_playerPos, 50.0f,
                              glm::vec3(0.5f, 0.5f, 1.0f), 0.5f);
    m_vfxManager->SpawnRibbon(startPos, m_playerPos, 10.0f,
                              glm::vec3(0.7f, 0.8f, 1.0f), 0.5f);
    // Energy burst at hand (Main 5.2: CreateParticle(BITMAP_ENERGY))
    m_vfxManager->SpawnBurst(ParticleType::ENERGY, startPos, 3);
  }

  // Skeleton Archer (type 15): fire arrow toward player
  // Main 5.2: CreateArrows at AttackTime==8
  if (mon.monsterType == 15) {
    glm::vec3 arrowStart = mon.position + glm::vec3(0, 80.0f * mon.scale, 0);
    // Get left hand bone (42) for arrow origin if available
    if (42 < (int)mon.cachedBones.size()) {
      glm::mat4 modelRot = glm::mat4(1.0f);
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));
      const auto &bm = mon.cachedBones[42];
      glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
      glm::vec3 boneWorld = glm::vec3(modelRot * glm::vec4(boneLocal, 1.0f));
      arrowStart = boneWorld * mon.scale + mon.position;
    }
    SpawnArrow(arrowStart, m_playerPos + glm::vec3(0, 50, 0), 1800.0f);
  }

  // Hunter (type 29): ranged monster, fire arrow toward player
  if (mon.monsterType == 29) {
    glm::vec3 arrowStart = mon.position + glm::vec3(0, 80.0f * mon.scale, 0);
    if (42 < (int)mon.cachedBones.size()) {
      glm::mat4 modelRot = glm::mat4(1.0f);
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      modelRot =
          glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      modelRot = glm::rotate(modelRot, mon.facing, glm::vec3(0, 0, 1));
      const auto &bm = mon.cachedBones[42];
      glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
      glm::vec3 boneWorld = glm::vec3(modelRot * glm::vec4(boneLocal, 1.0f));
      arrowStart = boneWorld * mon.scale + mon.position;
    }
    SpawnArrow(arrowStart, m_playerPos + glm::vec3(0, 50, 0), 1600.0f);
  }
}

void MonsterManager::RespawnMonster(int index, uint8_t gridX, uint8_t gridY,
                                    int hp) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  auto &mdl = m_models[mon.modelIdx];

  float worldX = (float)gridY * 100.0f;
  float worldZ = (float)gridX * 100.0f;
  float worldY = snapToTerrain(worldX, worldZ) + mdl.bodyOffset;
  mon.position = glm::vec3(worldX, worldY, worldZ);
  mon.spawnPosition = mon.position;
  mon.hp = hp;
  mon.maxHp = hp;
  mon.corpseAlpha = 1.0f;
  mon.corpseTimer = 0.0f;
  mon.spawnAlpha = 0.0f; // Start invisible, fade in
  mon.state = MonsterState::IDLE;
  mon.serverChasing = false;
  mon.serverTargetPos = mon.position;
  mon.splinePoints.clear();
  mon.splineT = 0.0f;
  mon.splineRate = 0.0f;
  mon.deathSmokeDone = false;
  // Reset spring-damper velocity if this is our own summon
  if (mon.serverIndex == m_ownSummonIndex && m_ownSummonIndex != 0) {
    m_summonVelocity = glm::vec3(0.0f);
    m_prevPlayerPos = m_playerPos;
  }
  // Play APPEAR animation (action 7) if available, else STOP1
  // Skeleton types use Player.bmd — no monster APPEAR action, just idle
  if (!mdl.animBmd && 7 < (int)mdl.bmd->Actions.size() &&
      mdl.bmd->Actions[7].NumAnimationKeys > 1)
    setAction(mon, 7); // MONSTER01_APEAR (normal monsters only)
  else
    setAction(mon, ACTION_STOP1);
}

void MonsterManager::SetMonsterServerPosition(int index, float worldX,
                                              float worldZ, bool chasing) {
  if (index < 0 || index >= (int)m_monsters.size())
    return;
  auto &mon = m_monsters[index];
  if (mon.state == MonsterState::DYING || mon.state == MonsterState::DEAD)
    return;

  // Target world position (center of grid cell)
  glm::vec3 newTarget(worldX + 50.0f, 0.0f, worldZ + 50.0f);
  newTarget.y = snapToTerrain(newTarget.x, newTarget.z);
  mon.serverTargetPos = newTarget;
  mon.serverChasing = chasing;

  // Don't interrupt attack or hit stun animations
  if (mon.state == MonsterState::ATTACKING || mon.state == MonsterState::HIT)
    return;

  // If monster is still fading in from respawn, snap to position silently
  // and restart the fade so the monster is invisible at the new position
  if (mon.spawnAlpha < 1.0f) {
    auto &mdl = m_models[mon.modelIdx];
    mon.position = newTarget;
    mon.position.y = snapToTerrain(mon.position.x, mon.position.z) + mdl.bodyOffset;
    mon.splinePoints.clear();
    mon.splineT = 0.0f;
    mon.splineRate = 0.0f;
    mon.spawnAlpha = 0.0f; // Restart fade from invisible at new position
    mon.state = chasing ? MonsterState::CHASING : MonsterState::IDLE;
    return;
  }

  // Own summon: separate follow (smooth IDLE) from chase (direct spline)
  bool isOwnSummon = (mon.serverIndex == m_ownSummonIndex &&
                      m_ownSummonIndex != 0);
  if (isOwnSummon) {
    if (!chasing) {
      // Non-combat follow: let the IDLE Guardian Angel follow handle movement.
      // Only transition out of combat states so IDLE follow can take over.
      if (mon.state == MonsterState::CHASING ||
          mon.state == MonsterState::ATTACKING) {
        mon.state = MonsterState::IDLE;
        playIdleSound(mon);
        m_summonVelocity = glm::vec3(0.0f);
        mon.stateTimer = 0.0f;
        mon.splinePoints.clear();
        mon.splineT = 0.0f;
        setAction(mon, ACTION_STOP1); // Reset to idle animation
      }
      return;
    }
    // Combat chase: direct 2-point spline to target position
    mon.splinePoints.clear();
    mon.splineT = 0.0f;
    mon.splinePoints.push_back(mon.position);
    mon.splinePoints.push_back(newTarget);
    glm::vec3 d = newTarget - mon.position;
    d.y = 0.0f;
    float dist = glm::length(d);
    mon.splineRate = (dist > 1.0f) ? CHASE_SPEED / dist : 2.5f;
    mon.state = MonsterState::CHASING;
    return;
  }

  // Convert current position and target to grid coordinates
  // MU mapping: gridX = worldZ / 100, gridY = worldX / 100
  GridPoint curGrid;
  curGrid.x = (uint8_t)std::clamp((int)(mon.position.z / 100.0f), 0, 255);
  curGrid.y = (uint8_t)std::clamp((int)(mon.position.x / 100.0f), 0, 255);
  GridPoint tgtGrid;
  tgtGrid.x = (uint8_t)std::clamp((int)(worldZ / 100.0f), 0, 255);
  tgtGrid.y = (uint8_t)std::clamp((int)(worldX / 100.0f), 0, 255);

  // Dampening: if already walking/chasing to a target within 2 grid cells of
  // the new target, skip re-pathfind to avoid jitter from frequent updates
  if ((mon.state == MonsterState::WALKING ||
       mon.state == MonsterState::CHASING) &&
      !mon.splinePoints.empty()) {
    // Get grid cell of current spline endpoint
    const glm::vec3 &endPt = mon.splinePoints.back();
    GridPoint endGrid;
    endGrid.x = (uint8_t)std::clamp((int)(endPt.z / 100.0f), 0, 255);
    endGrid.y = (uint8_t)std::clamp((int)(endPt.x / 100.0f), 0, 255);
    if (PathFinder::ChebyshevDist(endGrid, tgtGrid) <= 2)
      return; // Target hasn't moved significantly
  }

  // Same cell — no movement needed
  if (curGrid == tgtGrid) {
    if (chasing)
      mon.state = MonsterState::CHASING;
    return;
  }

  // A* pathfind from current to target
  std::vector<GridPoint> path;
  if (m_terrainData && !m_terrainData->mapping.attributes.empty()) {
    path = m_pathFinder.FindPath(curGrid, tgtGrid,
                                 m_terrainData->mapping.attributes.data());
  }

  // Build Catmull-Rom spline control points from path
  mon.splinePoints.clear();
  mon.splineT = 0.0f;

  // First control point = current position (ensures smooth transition)
  mon.splinePoints.push_back(mon.position);

  if (!path.empty()) {
    // Convert each grid point to world position
    for (auto &gp : path) {
      float wx = (float)gp.y * 100.0f + 50.0f; // gridY → worldX
      float wz = (float)gp.x * 100.0f + 50.0f; // gridX → worldZ
      float wy = snapToTerrain(wx, wz);
      mon.splinePoints.push_back(glm::vec3(wx, wy, wz));
    }
  } else {
    // Pathfinding failed — fall back to direct line to target
    mon.splinePoints.push_back(newTarget);
  }

  // Pre-compute spline rate: constant world-unit speed regardless of segment
  // length. rate = speed * numSegments / totalXZDist
  float speed = chasing ? CHASE_SPEED : WANDER_SPEED;
  float numSegs = (float)(mon.splinePoints.size() - 1);
  float totalDist = 0.0f;
  for (size_t i = 1; i < mon.splinePoints.size(); ++i) {
    glm::vec3 d = mon.splinePoints[i] - mon.splinePoints[i - 1];
    d.y = 0.0f;
    totalDist += glm::length(d);
  }
  mon.splineRate =
      (numSegs > 0 && totalDist > 1.0f) ? speed * numSegs / totalDist : 2.5f;

  // Set state
  mon.state = chasing ? MonsterState::CHASING : MonsterState::WALKING;
}

int MonsterManager::CalcXPReward(int monsterIndex, int playerLevel) const {
  // CharacterCalcExperienceAlone (ObjectManager.cpp:813)
  if (monsterIndex < 0 || monsterIndex >= (int)m_monsters.size())
    return 0;
  const auto &mon = m_monsters[monsterIndex];
  const auto &mdl = m_models[mon.modelIdx];
  int monLvl = mdl.level;
  int lvlFactor = ((monLvl + 25) * monLvl) / 3;
  // Level penalty: monster 10+ levels below player
  if ((monLvl + 10) < playerLevel)
    lvlFactor = (lvlFactor * (monLvl + 10)) / std::max(1, playerLevel);
  int xp = lvlFactor + lvlFactor / 4; // * 1.25
  return std::max(1, xp);
}

void MonsterManager::Cleanup() {
  for (auto &mon : m_monsters) {
    CleanupMeshBuffers(mon.meshBuffers);
    for (auto &wms : mon.weaponMeshes)
      CleanupMeshBuffers(wms.meshBuffers);
    for (auto &sm : mon.shadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    for (auto &wss : mon.weaponShadowMeshes) {
      for (auto &sm : wss.meshes) {
        if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
      }
    }
  }
  m_monsters.clear();
  m_arrows.clear();
  m_models.clear();
  m_ownedBmds.clear();
  m_playerBmd.reset();
  m_shader.reset();
  m_shadowShader.reset();
}
