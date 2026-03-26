#include "NpcManager.hpp"
#include "ChromeGlow.hpp"
#include "HeroCharacter.hpp" // For PointLight struct
#include "SoundManager.hpp"
#include "TerrainUtils.hpp"
#include "TextureLoader.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// NPC type → display name mapping (matches Database::SeedNpcSpawns)
static const std::unordered_map<uint16_t, std::string> s_npcNames = {
    {253, "Potion Girl Amy"},
    {250, "Weapon Merchant"},
    {251, "Hanzo the Blacksmith"},
    {254, "Pasi the Mage"},
    {255, "Lumen the Barmaid"},
    {240, "Safety Guardian"},
    {245, "Warden Aldric"},
    {246, "Corporal Brynn"},
    {247, "Sergeant Dorian"},
    {248, "Lieutenant Kael"},
    {249, "Captain Marcus"},
    {300, "Baz the Vault Keeper"},
    {301, "Guild Master"},
    {302, "Caren the Barmaid"},
    {303, "Izabel the Wizard"},
    {304, "Zienna the Arms Dealer"},
    {310, "Ranger Elise"},
    {311, "Tracker Nolan"},
    {312, "Warden Hale"},
    {242, "Elf Lala"},
    {243, "Eo the Craftsman"},
    {238, "Chaos Goblin"},
    {256, "Sentinel Arwen"}};

glm::vec3 NpcManager::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
  return TerrainUtils::SampleLightAt(m_terrainLightmap, worldPos);
}

float NpcManager::snapToTerrain(float worldX, float worldZ) {
  return TerrainUtils::GetHeight(m_terrainData, worldX, worldZ);
}

int NpcManager::loadModel(const std::string &npcPath,
                          const std::string &skeletonFile,
                          const std::vector<std::string> &partFiles,
                          const std::string &modelName) {
  // Check if already loaded
  for (int i = 0; i < (int)m_models.size(); ++i) {
    if (m_models[i].name == modelName)
      return i;
  }

  auto skeleton = BMDParser::Parse(npcPath + skeletonFile);
  if (!skeleton) {
    std::cerr << "[NPC] Failed to load skeleton: " << skeletonFile << std::endl;
    return -1;
  }

  NpcModel model;
  model.name = modelName;
  model.skeleton = skeleton.get();
  m_ownedBmds.push_back(std::move(skeleton));

  // Load body part BMDs
  for (auto &partFile : partFiles) {
    auto part = BMDParser::Parse(npcPath + partFile);
    if (!part) {
      std::cerr << "[NPC] Failed to load part: " << partFile << std::endl;
      continue;
    }
    model.parts.push_back(part.get());
    m_ownedBmds.push_back(std::move(part));
  }

  // Find root bone (Parent == -1) for LockPositions handling
  for (int i = 0; i < (int)model.skeleton->Bones.size(); ++i) {
    if (!model.skeleton->Bones[i].Dummy && model.skeleton->Bones[i].Parent == -1) {
      model.rootBone = i;
      break;
    }
  }

  int idx = (int)m_models.size();
  m_models.push_back(std::move(model));
  std::cout << "[NPC] Loaded model '" << modelName << "' ("
            << m_models[idx].skeleton->Bones.size() << " bones, "
            << m_models[idx].parts.size() << " parts, rootBone="
            << m_models[idx].rootBone << ")" << std::endl;
  return idx;
}

void NpcManager::addNpc(int modelIdx, int gridX, int gridY, int dir,
                        float scale) {
  if (modelIdx < 0 || modelIdx >= (int)m_models.size())
    return;

  NpcInstance npc;
  npc.modelIdx = modelIdx;
  npc.scale = scale;
  npc.action = m_models[modelIdx].defaultAction; // 0 for NPCs, 4/12 for guards
  npc.npcType = 0; // Set by AddNpcByType or Init caller

  // Grid to world: center of cell (Main 5.2 WSclient.cpp:1790-1791)
  // Position = (gridPos + 0.5f) * TERRAIN_SCALE
  float worldX = ((float)gridY + 0.5f) * 100.0f;
  float worldZ = ((float)gridX + 0.5f) * 100.0f;
  float worldY = snapToTerrain(worldX, worldZ);
  npc.position = glm::vec3(worldX, worldY, worldZ);

  // Direction to facing angle (Main 5.2 WSclient.cpp:796):
  //   Angle[2] = ((float)Data->Angle - 1.f) * 45.f
  // DB stores OpenMU 1-8 = protocol 0-7 + 1, so subtract 2:
  npc.facing = (float)(dir - 2) * (float)M_PI / 4.0f;
  npc.spawnFacing = npc.facing;

  // Random animation offset so NPCs don't all sync
  npc.animFrame = (float)(m_npcs.size() * 3.7f);

  auto &mdl = m_models[modelIdx];
  auto bones = ComputeBoneMatrices(mdl.skeleton);

  // Check if skeleton itself has renderable meshes
  bool skeletonHasMeshes = false;
  for (auto &mesh : mdl.skeleton->Meshes) {
    if (mesh.NumTriangles > 0) {
      skeletonHasMeshes = true;
      break;
    }
  }

  // Determine texture directory: guards use Data/Player/, NPCs use Data/NPC/
  bool isGuard = (mdl.weaponAttachBone >= 0);
  std::string texDir = isGuard ? (m_dataPath + "/Player/") : m_npcTexPath;

  // Upload skeleton meshes (for single-model NPCs like Smith, Wizard, Storage)
  if (skeletonHasMeshes) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = -1; // skeleton
    for (auto &mesh : mdl.skeleton->Meshes) {
      UploadMeshWithBones(mesh, texDir, bones, bp.meshBuffers, aabb, true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Upload body part meshes (for multi-part NPCs like Man, Girl, Female,
  // Guards)
  for (int pi = 0; pi < (int)mdl.parts.size(); ++pi) {
    AABB aabb{};
    NpcInstance::BodyPart bp;
    bp.bmdIdx = pi;
    for (auto &mesh : mdl.parts[pi]->Meshes) {
      UploadMeshWithBones(mesh, texDir, bones, bp.meshBuffers, aabb, true);
    }
    npc.bodyParts.push_back(std::move(bp));
  }

  // Upload weapon meshes (guards only)
  if (mdl.weaponBmd) {
    std::string weaponTexDir = m_dataPath + "/Item/";
    if (mdl.cachedWeaponBones.empty())
      mdl.cachedWeaponBones = ComputeBoneMatrices(mdl.weaponBmd);
    const auto &wBones = mdl.cachedWeaponBones;
    AABB wAabb{};
    for (auto &mesh : mdl.weaponBmd->Meshes) {
      UploadMeshWithBones(mesh, weaponTexDir, wBones, npc.weaponMeshBuffers,
                          wAabb, true);
    }
    // Create weapon shadow mesh buffers
    for (auto &mb : npc.weaponMeshBuffers) {
      NpcInstance::ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      if (sm.vertexCount == 0) {
        npc.weaponShadowMeshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      npc.weaponShadowMeshes.push_back(sm);
    }
  }

  // Create shadow mesh buffers
  for (auto &bp : npc.bodyParts) {
    for (auto &mb : bp.meshBuffers) {
      NpcInstance::ShadowMesh sm;
      sm.vertexCount = mb.vertexCount;
      if (sm.vertexCount == 0) {
        npc.shadowMeshes.push_back(sm);
        continue;
      }
      bgfx::VertexLayout shadowLayout;
      shadowLayout.begin()
          .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
          .end();
      sm.vbo = bgfx::createDynamicVertexBuffer(
          sm.vertexCount, shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
      npc.shadowMeshes.push_back(sm);
    }
  }

  m_npcs.push_back(std::move(npc));
}

void NpcManager::InitModels(const std::string &dataPath) {
  if (m_modelsLoaded)
    return;

  std::string npcPath = dataPath + "/NPC/";
  m_npcTexPath = npcPath;
  m_dataPath = dataPath;

  // Create shaders
  m_shader = Shader::Load("vs_model.bin", "fs_model.bin");
  m_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");
  // m_outlineShader: not needed for BGFX (silhouette outline is GL-only)

  // Load NPC models for 0.97d Lorencia
  int smithIdx = loadModel(npcPath, "Smith01.bmd", {}, "Smith");
  int wizardIdx = loadModel(npcPath, "Wizard01.bmd", {}, "Wizard");
  int storageIdx = loadModel(npcPath, "Storage01.bmd", {}, "Storage");
  int manIdx = loadModel(
      npcPath, "Man01.bmd",
      {"ManHead01.bmd", "ManUpper01.bmd", "ManGloves01.bmd", "ManBoots01.bmd"},
      "MerchantMan");
  int girlIdx = loadModel(
      npcPath, "Girl01.bmd",
      {"GirlHead01.bmd", "GirlUpper01.bmd", "GirlLower01.bmd"}, "MerchantGirl");
  int femaleIdx = loadModel(npcPath, "Female01.bmd",
                            {"FemaleHead01.bmd", "FemaleUpper01.bmd",
                             "FemaleLower01.bmd", "FemaleBoots01.bmd"},
                            "MerchantFemale");

  // Map NPC type IDs to model indices
  m_typeToModel[251] = smithIdx;   // Hanzo the Blacksmith
  m_typeToModel[254] = wizardIdx;  // Pasi the Mage
  m_typeToModel[240] = storageIdx; // Safety Guardian (Vault)
  m_typeToModel[250] = manIdx;     // Weapon Merchant
  m_typeToModel[253] = girlIdx;    // Potion Girl Amy
  m_typeToModel[255] = femaleIdx;  // Lumen the Barmaid

  // Scale overrides
  m_typeScale[251] = 0.95f; // Blacksmith slightly smaller

  // ── Guard NPCs (Main 5.2: ZzzCharacter.cpp:13859-13890) ──
  // Guards use Player.bmd skeleton + armor set 9 (heavy plate)
  std::string playerPath = dataPath + "/Player/";
  std::string itemPath = dataPath + "/Item/";

  // Type 249: Berdysh Guard (spear, right hand bone 33)
  // Plate Armor = item index 9 → Male10 BMD files (Main 5.2: fileNum = index+1)
  int berdyshIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmMale10.bmd", "ArmorMale10.bmd", "PantMale10.bmd",
                 "GloveMale10.bmd", "BootMale10.bmd"},
                "BerdyshGuard");
  if (berdyshIdx >= 0) {
    auto spearBmd = BMDParser::Parse(itemPath + "Spear08.bmd");
    if (spearBmd) {
      m_models[berdyshIdx].weaponBmd = spearBmd.get();
      m_models[berdyshIdx].weaponAttachBone = 33; // Right hand
      m_models[berdyshIdx].defaultAction = 1;     // PLAYER_STOP_MALE (weapon on back)
      m_ownedBmds.push_back(std::move(spearBmd));
    }
    m_typeToModel[245] = berdyshIdx; // Warden Aldric
    m_typeToModel[246] = berdyshIdx; // Corporal Brynn
    m_typeToModel[248] = berdyshIdx; // Lieutenant Kael
    m_typeToModel[249] = berdyshIdx; // Captain Marcus
  }

  // Type 247: Poleaxe Guard (two-handed spear, right hand bone 33)
  // Plate Armor = item index 9 → Male10 BMD files (Main 5.2: fileNum = index+1)
  int poleaxeIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmMale10.bmd", "ArmorMale10.bmd", "PantMale10.bmd",
                 "GloveMale10.bmd", "BootMale10.bmd"},
                "PoleaxeGuard");
  if (poleaxeIdx >= 0) {
    auto spearBmd2 = BMDParser::Parse(itemPath + "Spear05.bmd");
    if (spearBmd2) {
      m_models[poleaxeIdx].weaponBmd = spearBmd2.get();
      m_models[poleaxeIdx].weaponAttachBone = 33; // Right hand
      m_models[poleaxeIdx].defaultAction = 1;     // PLAYER_STOP_MALE (weapon on back)
      m_ownedBmds.push_back(std::move(spearBmd2));
    }
    m_typeToModel[247] = poleaxeIdx;
  }

  // ── Noria NPC models ──
  // Elf Lala (type 242): Main 5.2 uses MODEL_ELF_WIZARD = ElfWizard01.bmd
  int elfWizardIdx = loadModel(npcPath, "ElfWizard01.bmd", {}, "ElfWizard");
  // Eo the Craftsman (type 243): Main 5.2 uses MODEL_ELF_MERCHANT = ElfMerchant01.bmd
  int elfMerchantIdx = loadModel(npcPath, "ElfMerchant01.bmd", {}, "ElfMerchant");
  // Chaos Goblin (type 238): Main 5.2 uses MODEL_MIX_NPC = MixNpc01.bmd
  int mixNpcIdx = loadModel(npcPath, "MixNpc01.bmd", {}, "MixNpc");

  m_typeToModel[242] = elfWizardIdx;    // Elf Lala
  m_typeToModel[243] = elfMerchantIdx;  // Eo the Craftsman
  m_typeToModel[238] = mixNpcIdx;       // Chaos Goblin

  // Type 256: Sentinel Arwen — Elf quest guard (Guardian Elf armor + Silver Bow)
  int elfGuardIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmElf05.bmd", "ArmorElf05.bmd", "PantElf05.bmd",
                 "GloveElf05.bmd", "BootElf05.bmd"},
                "ElfGuard");
  if (elfGuardIdx >= 0) {
    auto bowBmd = BMDParser::Parse(itemPath + "Bow06.bmd");
    if (bowBmd) {
      m_models[elfGuardIdx].weaponBmd = bowBmd.get();
      m_models[elfGuardIdx].weaponAttachBone = 33; // Right hand
      m_models[elfGuardIdx].defaultAction = 1;     // PLAYER_STOP_MALE (weapon on back)
      m_ownedBmds.push_back(std::move(bowBmd));
    }
    m_typeToModel[256] = elfGuardIdx;
  }

  // ── Devias NPC type mappings (reuse existing models) ──
  m_typeToModel[300] = storageIdx;  // Baz the Vault Keeper (same as Safety Guardian)
  m_typeToModel[301] = manIdx;      // Guild Master
  m_typeToModel[302] = femaleIdx;   // Caren the Barmaid
  m_typeToModel[303] = wizardIdx;   // Izabel the Wizard
  m_typeToModel[304] = manIdx;      // Zienna the Arms Dealer
  if (berdyshIdx >= 0) {
    m_typeToModel[310] = berdyshIdx;  // Ranger Elise (quest guard)
    m_typeToModel[311] = berdyshIdx;  // Tracker Nolan (quest guard)
  }
  if (poleaxeIdx >= 0) {
    m_typeToModel[312] = poleaxeIdx;  // Warden Hale (quest guard)
  }

  m_modelsLoaded = true;
  std::cout << "[NPC] Models loaded: " << m_models.size() << " types, "
            << m_typeToModel.size() << " type mappings" << std::endl;
}

void NpcManager::AddNpcByType(uint16_t npcType, uint8_t gridX, uint8_t gridY,
                              uint8_t dir, uint16_t serverIndex) {
  auto it = m_typeToModel.find(npcType);
  if (it == m_typeToModel.end()) {
    std::cerr << "[NPC] Unknown NPC type " << npcType << " at (" << (int)gridX
              << "," << (int)gridY << "), skipping" << std::endl;
    return;
  }
  float scale = 1.0f;
  auto scaleIt = m_typeScale.find(npcType);
  if (scaleIt != m_typeScale.end())
    scale = scaleIt->second;

  addNpc(it->second, gridX, gridY, dir, scale);

  // Set type, name, and server index on the just-added NPC
  auto &added = m_npcs.back();
  added.npcType = npcType;
  added.serverIndex = serverIndex;
  auto nameIt = s_npcNames.find(npcType);
  if (nameIt != s_npcNames.end())
    added.name = nameIt->second;

  // Guard walk actions — weapons on back, use non-weapon animations
  // PLAYER_WALK_MALE = 15 (neutral walk, no weapon in hand)
  if ((npcType >= 245 && npcType <= 249) || npcType == 256 ||
      (npcType >= 310 && npcType <= 312)) {
    added.walkAction = 15;
    added.isGuard = true;
  }

  // Random initial sound cooldown so NPCs don't all trigger at once
  if (npcType == 242 || npcType == 243)
    added.soundCooldown = 2.0f + (rand() % 8); // 2-10s initial delay
  else if (npcType == 238)
    added.soundCooldown = 1.0f + (rand() % 4); // 1-5s initial delay

  std::cout << "[NPC] Server-spawned NPC type=" << npcType
            << " idx=" << serverIndex << " at grid (" << (int)gridX << ","
            << (int)gridY << ") dir=" << (int)dir << std::endl;
}

void NpcManager::Init(const std::string &dataPath) {
  // Load models if not already loaded
  InitModels(dataPath);

  // NPCs are now spawned entirely by the server via AddNpcByType()
  std::cout << "[NPC] Models loaded, waiting for server NPC spawns" << std::endl;
}

void NpcManager::Render(const glm::mat4 &view, const glm::mat4 &proj,
                        const glm::vec3 &camPos, float deltaTime) {
  if (!m_shader || m_npcs.empty())
    return;

  // Extract frustum planes from VP matrix for culling
  glm::mat4 vp = proj * view;
  glm::vec4 frustum[6];
  frustum[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                          vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
  frustum[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                          vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
  frustum[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                          vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
  frustum[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                          vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
  frustum[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                          vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
  frustum[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                          vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far
  for (int i = 0; i < 6; ++i)
    frustum[i] /= glm::length(glm::vec3(frustum[i]));

  // BGFX: no shader->use() — program bound at submit time
  // View/proj set via bgfx::setViewTransform() in caller
  glm::vec3 eye = camPos;
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

  // Point lights (pre-cached locations)
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->uploadPointLights(plCount, m_pointLights.data());

  for (int ni = 0; ni < (int)m_npcs.size(); ++ni) {
    auto &npc = m_npcs[ni];
    auto &mdl = m_models[npc.modelIdx];

    // Advance idle animation
    int numKeys = 1;
    bool lockPos = false;
    if (npc.action >= 0 && npc.action < (int)mdl.skeleton->Actions.size()) {
      numKeys = mdl.skeleton->Actions[npc.action].NumAnimationKeys;
      lockPos = mdl.skeleton->Actions[npc.action].LockPositions;
    }
    float prevAnimFrame = npc.animFrame;
    if (numKeys > 1) {
      // Guard uses same Player.bmd as character — exact same walk animation speed
      static constexpr float CHAR_ANIM_SPEED = 8.25f;
      float speed = (npc.walkAction > 0 && npc.isMoving) ? CHAR_ANIM_SPEED
                    : (npc.walkAction > 0)                ? 7.5f
                                                          : ANIM_SPEED;
      npc.animFrame += speed * deltaTime;
      // LockPositions actions wrap at numKeys-1 (last frame == first frame)
      int wrapKeys = lockPos ? (numKeys - 1) : numKeys;
      if (wrapKeys < 1) wrapKeys = 1;
      if (npc.animFrame >= (float)wrapKeys) {
        npc.animFrame = std::fmod(npc.animFrame, (float)wrapKeys);
        // NPC action switching (Main 5.2: ZzzCharacter.cpp:3352-3358)
        // Blacksmith: 75% action 0 (hammering), 25% action 1-2
        if (npc.npcType == 251) {
          int numActions = (int)mdl.skeleton->Actions.size();
          if (rand() % 4 == 0 && numActions > 1) {
            npc.action = 1 + rand() % std::min(2, numActions - 1);
          } else {
            npc.action = 0;
          }
          npc.animFrame = 0.0f;
        }
      }
    }

    // NPC proximity sounds (Main 5.2: ZzzCharacter.cpp:5906-5926)
    // Distance-based: use Play3D for OpenAL 3D attenuation
    float dx = npc.position.x - camPos.x;
    float dz = npc.position.z - camPos.z;
    float distSq = dx * dx + dz * dz;
    if (npc.soundCooldown > 0.0f)
      npc.soundCooldown -= deltaTime;

    // Blacksmith (type 251): hammer strike synced to animation frame 5
    if (npc.npcType == 251 && npc.action == 0 && distSq < 800.0f * 800.0f) {
      if (prevAnimFrame < 5.0f && npc.animFrame >= 5.0f)
        SoundManager::Play3D(SOUND_NPC_BLACKSMITH, npc.position.x,
                             npc.position.y, npc.position.z);
    }
    // Elf Lala (242) / Eo the Craftsman (243): harp on cooldown
    if ((npc.npcType == 242 || npc.npcType == 243) &&
        distSq < 800.0f * 800.0f && npc.soundCooldown <= 0.0f) {
      SoundManager::Play3D(SOUND_NPC_HARP, npc.position.x,
                           npc.position.y, npc.position.z);
      npc.soundCooldown = 8.0f + (rand() % 5); // 8-12 seconds
    }
    // Chaos Goblin (238): mix/grinding on cooldown
    if (npc.npcType == 238 &&
        distSq < 800.0f * 800.0f && npc.soundCooldown <= 0.0f) {
      SoundManager::Play3D(SOUND_NPC_MIX, npc.position.x,
                           npc.position.y, npc.position.z);
      npc.soundCooldown = 3.0f + (rand() % 3); // 3-5 seconds
    }

    // Guard interaction: face player when quest dialog is open
    if (npc.isGuard && m_interactingNpc == ni) {
      // Stop movement immediately and face player
      if (npc.isMoving) {
        npc.isMoving = false;
        npc.action = mdl.defaultAction;
        npc.animFrame = 0.0f;
      }
      float dx = m_playerPos.x - npc.position.x;
      float dz = m_playerPos.z - npc.position.z;
      if (dx * dx + dz * dz > 1.0f)
        npc.facing = atan2f(dz, -dx);
    }
    // Guard was interacting last frame but no longer — reset to spawn facing
    if (npc.isGuard && m_prevInteractingNpc == ni && m_interactingNpc != ni) {
      npc.facing = npc.spawnFacing;
    }
    // Guard patrol is server-driven via 0x14 NPC_MOVE packets (SetNpcMoveTarget)

    // Guard patrol movement: interpolate toward move target (skip if interacting)
    if (npc.isMoving && !(npc.isGuard && m_interactingNpc == ni)) {
      glm::vec3 diff = npc.moveTarget - npc.position;
      diff.y = 0.0f; // Only move in XZ plane
      float dist = glm::length(diff);
      // Guard patrol speed: same as character (334 u/s)
      float step = 334.0f * deltaTime;
      if (dist <= step || dist < 1.0f) {
        // Arrived at target
        npc.position.x = npc.moveTarget.x;
        npc.position.z = npc.moveTarget.z;
        npc.position.y = snapToTerrain(npc.position.x, npc.position.z);
        npc.isMoving = false;
        // Switch back to idle action
        npc.action = mdl.defaultAction;
        npc.animFrame = 0.0f;
      } else {
        glm::vec3 dir = diff / dist;
        float nextX = npc.position.x + dir.x * step;
        float nextZ = npc.position.z + dir.z * step;

        bool blocked = false;
        if (m_terrainData) {
          int gy = (int)(nextX / 100.0f);
          int gx = (int)(nextZ / 100.0f);
          const int S = TerrainParser::TERRAIN_SIZE;
          if (gx >= 0 && gy >= 0 && gx < S && gy < S) {
            uint8_t attr = m_terrainData->mapping.attributes[gy * S + gx];
            if ((attr & 0x04) != 0) {
              blocked = true;
            }
          }
        }

        if (blocked) {
          npc.isMoving = false;
          npc.action = mdl.defaultAction;
          npc.animFrame = 0.0f;
        } else {
          npc.position.x = nextX;
          npc.position.z = nextZ;
          npc.position.y = snapToTerrain(npc.position.x, npc.position.z);
          // Update facing toward movement direction (same formula as monsters)
          npc.facing = atan2f(dir.z, -dir.x);
        }
      }
    }

    // Frustum culling: skip bone computation + rendering for off-screen NPCs
    {
      float cullRadius = 200.0f * npc.scale;
      glm::vec3 center = npc.position + glm::vec3(0, cullRadius * 0.5f, 0);
      bool outside = false;
      for (int p = 0; p < 6; ++p) {
        if (frustum[p].x * center.x + frustum[p].y * center.y +
                frustum[p].z * center.z + frustum[p].w <
            -cullRadius) {
          outside = true;
          break;
        }
      }
      if (outside)
        continue;
    }

    // Compute bone matrices
    ComputeBoneMatricesInterpolated(mdl.skeleton, npc.action,
                                    npc.animFrame, npc.cachedBones);
    auto &bones = npc.cachedBones;

    // LockPositions: cancel root bone X/Y displacement to prevent walk
    // animation from physically moving the model (same as MonsterManager)
    if (mdl.rootBone >= 0 && npc.action >= 0 &&
        npc.action < (int)mdl.skeleton->Actions.size() &&
        mdl.skeleton->Actions[npc.action].LockPositions) {
      int rb = mdl.rootBone;
      auto &bm = mdl.skeleton->Bones[rb].BoneMatrixes[npc.action];
      if (!bm.Position.empty()) {
        float dx = bones[rb][0][3] - bm.Position[0].x;
        float dy = bones[rb][1][3] - bm.Position[0].y;
        if (dx != 0.0f || dy != 0.0f) {
          for (int b = 0; b < (int)bones.size(); ++b) {
            bones[b][0][3] -= dx;
            bones[b][1][3] -= dy;
          }
        }
      }
    }

    npc.cachedBones = bones;

    // ── Blacksmith VFX (Main 5.2: ZzzCharacter.cpp:5917-5939) ──
    // MODEL_SMITH (NPC type 251): sparks from bone 17 during hammer frames 5-6
    // Sparks at weapon contact point (sword on anvil), not hammer grip
    if (npc.npcType == 251 && m_vfxManager && npc.action == 0 &&
        npc.animFrame >= 5.0f && npc.animFrame <= 6.0f) {
      const int HAMMER_BONE = 17;
      if (HAMMER_BONE < (int)bones.size()) {
        // Bone position in model-local space
        glm::vec3 boneLocal(bones[HAMMER_BONE][0][3], bones[HAMMER_BONE][1][3],
                            bones[HAMMER_BONE][2][3]);
        // Offset from hand grip to hammer tip / weapon contact point
        // In BMD-local space: extend along hammer axis toward the anvil strike
        boneLocal += glm::vec3(0.0f, -30.0f, -15.0f);

        // Transform from BMD-local to world space
        glm::vec3 r1(boneLocal.y, -boneLocal.x, boneLocal.z);
        glm::vec3 r2(r1.z, r1.y, -r1.x);
        float adjustedFacing = npc.facing;
        float cf = std::cos(adjustedFacing);
        float sf = std::sin(adjustedFacing);
        glm::vec3 r3(r2.x * cf - r2.y * sf, r2.x * sf + r2.y * cf, r2.z);
        glm::vec3 sparkPos = npc.position + r3 * npc.scale;

        m_vfxManager->SpawnBurst(ParticleType::HIT_SPARK, sparkPos, 4);
      }
    }

    // Re-skin meshes
    for (auto &bp : npc.bodyParts) {
      BMDData *bmd = (bp.bmdIdx < 0) ? mdl.skeleton : mdl.parts[bp.bmdIdx];
      for (int mi = 0;
           mi < (int)bp.meshBuffers.size() && mi < (int)bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(bmd->Meshes[mi], bones, bp.meshBuffers[mi]);
      }
    }

    // Build model matrix
    glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
    // Fix: Rotate -90 degrees around Z to align guards correctly
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, npc.facing, glm::vec3(0, 0, 1));
    if (npc.scale != 1.0f)
      model = glm::scale(model, glm::vec3(npc.scale));

    // Terrain lightmap at NPC position
    glm::vec3 tLight = sampleTerrainLightAt(npc.position);

    // Blacksmith forge glow: BlendMesh=4, Luminosity=0.8 constant
    // Main 5.2: o->BlendMesh = 4; o->BlendMeshLight = Luminosity;
    // Luminosity for Lorencia is a constant 0.8f (ZzzCharacter.cpp:5500)
    bool isBlacksmith = (npc.npcType == 251);
    bool isGuardNpc = (npc.npcType >= 245 && npc.npcType <= 249) ||
                      (npc.npcType >= 310 && npc.npcType <= 312);
    float blendMeshLight = 1.0f;
    if (isBlacksmith) blendMeshLight = 0.8f;
    if (isGuardNpc) blendMeshLight = 0.8f;

    // Helper lambda: set all per-submit uniforms (BGFX consumes per submit)
    auto setNpcUniforms = [&](float bml, float chromeMode, float chromeTime,
                              const glm::vec3 &glowColor) {
      m_shader->setVec4("u_params", glm::vec4(1.0f, bml, chromeMode, chromeTime));
      m_shader->setVec4("u_params2", glm::vec4(m_luminosity, 0.0f, 0.0f, 0.0f));
      m_shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
      m_shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 500, 0), 0.0f));
      m_shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
      m_shader->setVec4("u_terrainLight", glm::vec4(tLight, 0.0f));
      m_shader->setVec4("u_glowColor", glm::vec4(glowColor, 0.0f));
      m_shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
      m_shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
      m_shader->setVec4("u_fogParams", glm::vec4(fogNear, fogFar, useFog, 0.0f));
      m_shader->setVec4("u_fogColor", glm::vec4(fogColor, 0.0f));
      m_shader->uploadPointLights(plCount, m_pointLights.data());
      // Shadow map
      float shadowEnabled = bgfx::isValid(m_shadowMapTex) ? 1.0f : 0.0f;
      m_shader->setVec4("u_shadowParams", glm::vec4(shadowEnabled, 0.0f, 0.0f, 0.0f));
      if (shadowEnabled > 0.5f) {
        m_shader->setMat4("u_lightMtx", m_lightMtx);
        m_shader->setTexture(1, "s_shadowMap", m_shadowMapTex);
      }
    };

    // Draw all body part meshes
    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        bool forgeGlow = isBlacksmith && (mb.bmdTextureId == 4);

        bgfx::setTransform(glm::value_ptr(model));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        m_shader->setTexture(0, "s_texColor", mb.texture);
        setNpcUniforms(blendMeshLight, 0.0f, 0.0f, glm::vec3(0.0f));

        uint64_t state;
        if (forgeGlow || mb.bright) {
          state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD;
        } else if (mb.noneBlend) {
          state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        } else {
          state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
        bgfx::setState(state);
        bgfx::submit(0, m_shader->program);
      }
    }

    // ── +7 armor enhancement glow for guards (ChromeGlow module) ──
    bool isGuardType = (npc.npcType >= 245 && npc.npcType <= 249) ||
                       npc.npcType == 256 ||
                       (npc.npcType >= 310 && npc.npcType <= 312);
    if (isGuardType && TexValid(ChromeGlow::GetTextures().chrome1)) {
      static constexpr int GUARD_ARMOR_LEVEL = 7;
      // Plate guards use index 9 (muted blue), Elf guard uses index 5 (white)
      int guardItemIdx = (npc.npcType == 256) ? 5 : 9;
      float t = (float)glfwGetTime();
      uint64_t glowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                         | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD;
      for (int p = 0; p < (int)npc.bodyParts.size(); ++p) {
        auto &bp = npc.bodyParts[p];
        ChromeGlow::GlowPass passes[3];
        int n = ChromeGlow::GetGlowPasses(GUARD_ARMOR_LEVEL, 7 + p, guardItemIdx, passes);
        for (int gp = 0; gp < n; ++gp) {
          for (auto &mb : bp.meshBuffers) {
            if (mb.indexCount == 0 || mb.hidden) continue;
            bgfx::setTransform(glm::value_ptr(model));
            if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
            else bgfx::setVertexBuffer(0, mb.vbo);
            bgfx::setIndexBuffer(mb.ebo);
            m_shader->setTexture(0, "s_texColor", passes[gp].texture);
            setNpcUniforms(blendMeshLight, (float)passes[gp].chromeMode, t, passes[gp].color);
            bgfx::setState(glowState);
            bgfx::submit(0, m_shader->program);
          }
        }
      }
      // Reset uniforms after guard glow passes
      setNpcUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
    }

    // ── Guard weapon rendering (Main 5.2: ZzzCharacter.cpp:13859-13890) ──
    if (mdl.weaponBmd && mdl.weaponAttachBone >= 0 &&
        !npc.weaponMeshBuffers.empty() &&
        mdl.weaponAttachBone < (int)bones.size()) {
      static constexpr int BONE_BACK = 47;
      int attachBone = (BONE_BACK < (int)bones.size())
                           ? BONE_BACK
                           : mdl.weaponAttachBone;
      BoneWorldMatrix offsetMat = (attachBone == BONE_BACK)
          ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                            glm::vec3(-20.f, 5.f, 55.f))
          : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0.0f), glm::vec3(0.0f));
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms(
          (const float(*)[4])bones[attachBone].data(),
          (const float(*)[4])offsetMat.data(), (float(*)[4])parentMat.data());
      const auto &wLocalBones = mdl.cachedWeaponBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }
      for (int mi = 0; mi < (int)npc.weaponMeshBuffers.size() &&
                       mi < (int)mdl.weaponBmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(mdl.weaponBmd->Meshes[mi], wFinalBones,
                                 npc.weaponMeshBuffers[mi]);
      }
      for (auto &mb : npc.weaponMeshBuffers) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        bgfx::setTransform(glm::value_ptr(model));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        m_shader->setTexture(0, "s_texColor", mb.texture);
        setNpcUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
        uint64_t wState;
        if (mb.bright) {
          wState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                 | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD;
        } else {
          wState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS
                 | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
        bgfx::setState(wState);
        bgfx::submit(0, m_shader->program);
      }
    }
  }

  // Track previous interacting NPC for detecting dialog close transitions
  m_prevInteractingNpc = m_interactingNpc;
}

void NpcManager::RenderShadows(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_shadowShader || m_npcs.empty())
    return;

  uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint32_t shadowStencil = BGFX_STENCIL_TEST_EQUAL
                         | BGFX_STENCIL_FUNC_REF(0)
                         | BGFX_STENCIL_FUNC_RMASK(0xFF)
                         | BGFX_STENCIL_OP_FAIL_S_KEEP
                         | BGFX_STENCIL_OP_FAIL_Z_KEEP
                         | BGFX_STENCIL_OP_PASS_Z_INCR;

  const float sx = 2000.0f;
  const float sy = 4000.0f;

  for (auto &npc : m_npcs) {
    if (npc.cachedBones.empty())
      continue;

    auto &mdl = m_models[npc.modelIdx];

    // Shadow model matrix (facing baked into vertices)
    glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    if (npc.scale != 1.0f)
      model = glm::scale(model, glm::vec3(npc.scale));


    float cosF = cosf(npc.facing);
    float sinF = sinf(npc.facing);

    int smIdx = 0;
    for (auto &bp : npc.bodyParts) {
      BMDData *bmd = (bp.bmdIdx < 0) ? mdl.skeleton : mdl.parts[bp.bmdIdx];
      for (int mi = 0;
           mi < (int)bmd->Meshes.size() && smIdx < (int)npc.shadowMeshes.size();
           ++mi, ++smIdx) {
        auto &sm = npc.shadowMeshes[smIdx];
        if (sm.vertexCount == 0 || !bgfx::isValid(sm.vbo))
          continue;

        auto &mesh = bmd->Meshes[mi];
        static std::vector<glm::vec3> shadowVerts;
        shadowVerts.clear();

        for (int i = 0; i < mesh.NumTriangles; ++i) {
          auto &tri = mesh.Triangles[i];
          int steps = (tri.Polygon == 3) ? 3 : 4;
          for (int v = 0; v < 3; ++v) {
            auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
            glm::vec3 pos = srcVert.Position;
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)npc.cachedBones.size()) {
              pos = MuMath::TransformPoint(
                  (const float(*)[4])npc.cachedBones[boneIdx].data(), pos);
            }
            pos *= npc.scale;
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
          if (steps == 4) {
            int quadIndices[3] = {0, 2, 3};
            for (int v : quadIndices) {
              auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = srcVert.Position;
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)npc.cachedBones.size()) {
                pos = MuMath::TransformPoint(
                    (const float(*)[4])npc.cachedBones[boneIdx].data(), pos);
              }
              pos *= npc.scale;
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

}

void NpcManager::SetShadowMap(bgfx::TextureHandle tex, const glm::mat4 &lightMtx) {
  m_shadowMapTex = tex;
  m_lightMtx = lightMtx;
}

void NpcManager::RenderToShadowMap(uint8_t viewId, bgfx::ProgramHandle depthProgram) {
  if (m_npcs.empty()) return;

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW;

  for (auto &npc : m_npcs) {
    auto &mdl = m_models[npc.modelIdx];
    glm::mat4 model = glm::translate(glm::mat4(1.0f), npc.position);
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, npc.facing, glm::vec3(0, 0, 1));

    // Body part meshes
    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.hidden || mb.indexCount == 0) continue;
        bgfx::setTransform(glm::value_ptr(model));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        bgfx::setState(state);
        bgfx::submit(viewId, depthProgram);
      }
    }
    // Weapon meshes (guards)
    for (auto &mb : npc.weaponMeshBuffers) {
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

NpcInfo NpcManager::GetNpcInfo(int index) const {
  NpcInfo info{};
  if (index < 0 || index >= (int)m_npcs.size())
    return info;
  const auto &npc = m_npcs[index];
  info.position = npc.position;
  info.radius = 80.0f;
  info.height = 200.0f;
  info.name = npc.name;
  info.type = npc.npcType;
  return info;
}


void NpcManager::SetNpcMoveTarget(uint16_t serverIndex, float worldX,
                                  float worldZ) {
  for (auto &npc : m_npcs) {
    if (npc.serverIndex == serverIndex) {
      float worldY = snapToTerrain(worldX, worldZ);
      npc.moveTarget = glm::vec3(worldX, worldY, worldZ);
      npc.isMoving = true;
      // Switch to walk action
      if (npc.walkAction > 0)
        npc.action = npc.walkAction;
      return;
    }
  }
}

void NpcManager::Cleanup() {
  for (auto &npc : m_npcs) {
    for (auto &bp : npc.bodyParts)
      CleanupMeshBuffers(bp.meshBuffers);
    for (auto &sm : npc.shadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    // Weapon meshes (guards)
    CleanupMeshBuffers(npc.weaponMeshBuffers);
    for (auto &sm : npc.weaponShadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
  }
  m_npcs.clear();
  m_models.clear();
  m_ownedBmds.clear();
  m_shader.reset();
  m_shadowShader.reset();
}

void NpcManager::SetQuestMarkers(const std::vector<GuardMarker> &markers) {
  m_guardMarkers = markers;
}

void NpcManager::RenderLabels(ImDrawList *dl, const glm::mat4 &view,
                              const glm::mat4 &proj, int winW, int winH,
                              const glm::vec3 &camPos, int hoveredNpc) {
  const float padX = 4.0f, padY = 2.0f;

  for (int i = 0; i < GetNpcCount(); ++i) {
    NpcInfo info = GetNpcInfo(i);
    if (info.name.empty())
      continue;

    float dist = glm::distance(camPos, info.position);
    if (dist > 2000.0f)
      continue;

    glm::vec3 labelPos = info.position + glm::vec3(0, info.height + 30.0f, 0);
    glm::vec4 clip = proj * view * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0)
      continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;

    ImVec2 textSize = ImGui::CalcTextSize(info.name.c_str());
    float x0 = sx - textSize.x / 2 - padX;
    float y0 = sy - textSize.y / 2 - padY;
    float x1 = sx + textSize.x / 2 + padX;
    float y1 = sy + textSize.y / 2 + padY;

    ImU32 bgCol = IM_COL32(10, 10, 10, 150);
    ImU32 borderCol = IM_COL32(80, 80, 80, 150);
    ImU32 textCol = IM_COL32(200, 200, 200, 255);

    // Fill
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol, 2.0f);
    // Border
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), borderCol, 2.0f, 0, 1.0f);

    // Shadow
    dl->AddText(ImVec2(sx - textSize.x / 2 + 1, sy - textSize.y / 2 + 1),
                IM_COL32(0, 0, 0, 180), info.name.c_str());
    // Text
    dl->AddText(ImVec2(sx - textSize.x / 2, sy - textSize.y / 2), textCol,
                info.name.c_str());

    // Quest marker above nameplate (for guard NPCs)
    char marker = '\0';
    ImU32 markerColor = 0;
    for (auto &gm : m_guardMarkers) {
      if (gm.guardType == info.type && gm.marker != '\0') {
        marker = gm.marker;
        markerColor = gm.isGold ? IM_COL32(255, 210, 50, 255)
                                : IM_COL32(160, 160, 160, 255);
        break;
      }
    }
    if (marker != '\0') {
      char markerStr[2] = {marker, '\0'};
      // Use larger font scale for marker
      float baseFontSize = ImGui::GetFontSize();
      float markerFontSize = baseFontSize * 2.0f;
      ImVec2 markerSize = ImGui::CalcTextSize(markerStr);
      markerSize.x *= 2.0f;
      markerSize.y *= 2.0f;
      float mx = sx - markerSize.x * 0.5f;
      float my = y0 - markerSize.y - 4.0f;
      // Shadow
      ImFont *font = ImGui::GetFont();
      dl->AddText(font, markerFontSize,
                  ImVec2(mx + 1.5f, my + 1.5f), IM_COL32(0, 0, 0, 200),
                  markerStr);
      // Marker
      dl->AddText(font, markerFontSize, ImVec2(mx, my),
                  markerColor, markerStr);
    }
  }
}

int NpcManager::PickLabel(float screenX, float screenY, const glm::mat4 &view,
                          const glm::mat4 &proj, int winW, int winH,
                          const glm::vec3 &camPos) const {
  const float padX = 4.0f, padY = 2.0f;

  for (int i = 0; i < GetNpcCount(); ++i) {
    NpcInfo info = GetNpcInfo(i);
    if (info.name.empty())
      continue;

    float dist = glm::distance(camPos, info.position);
    if (dist > 2000.0f)
      continue;

    glm::vec3 labelPos = info.position + glm::vec3(0, info.height + 30.0f, 0);
    glm::vec4 clip = proj * view * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0)
      continue;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;

    ImVec2 textSize = ImGui::CalcTextSize(info.name.c_str());
    float x0 = sx - textSize.x / 2 - padX;
    float y0 = sy - textSize.y / 2 - padY;
    float x1 = sx + textSize.x / 2 + padX;
    float y1 = sy + textSize.y / 2 + padY;

    if (screenX >= x0 && screenX <= x1 && screenY >= y0 && screenY <= y1)
      return i;
  }
  return -1;
}
