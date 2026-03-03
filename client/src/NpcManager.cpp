#include "NpcManager.hpp"
#include "HeroCharacter.hpp" // For PointLight struct
#include "SoundManager.hpp"
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
    {247, "Guard"},
    {249, "Guard"}};

glm::vec3 NpcManager::sampleTerrainLightAt(const glm::vec3 &worldPos) const {
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

float NpcManager::snapToTerrain(float worldX, float worldZ) {
  if (!m_terrainData)
    return 0.0f;
  const int S = TerrainParser::TERRAIN_SIZE;
  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  gz = std::clamp(gz, 0.0f, (float)(S - 2));
  gx = std::clamp(gx, 0.0f, (float)(S - 2));
  int xi = (int)gx, zi = (int)gz;
  float xd = gx - (float)xi, zd = gz - (float)zi;
  float h00 = m_terrainData->heightmap[zi * S + xi];
  float h10 = m_terrainData->heightmap[zi * S + (xi + 1)];
  float h01 = m_terrainData->heightmap[(zi + 1) * S + xi];
  float h11 = m_terrainData->heightmap[(zi + 1) * S + (xi + 1)];
  return h00 * (1 - xd) * (1 - zd) + h10 * xd * (1 - zd) + h01 * (1 - xd) * zd +
         h11 * xd * zd;
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
  std::ifstream shaderTest("shaders/model.vert");
  m_shader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/model.vert" : "../shaders/model.vert",
      shaderTest.good() ? "shaders/model.frag" : "../shaders/model.frag");

  m_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");

  m_outlineShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/outline.vert" : "../shaders/outline.vert",
      shaderTest.good() ? "shaders/outline.frag" : "../shaders/outline.frag");

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
    m_typeToModel[249] = berdyshIdx;
  }

  // Type 247: Crossbow Guard (bow, left hand bone 42)
  // Plate Armor = item index 9 → Male10 BMD files (Main 5.2: fileNum = index+1)
  int crossbowIdx =
      loadModel(playerPath, "player.bmd",
                {"HelmMale10.bmd", "ArmorMale10.bmd", "PantMale10.bmd",
                 "GloveMale10.bmd", "BootMale10.bmd"},
                "CrossbowGuard");
  if (crossbowIdx >= 0) {
    auto bowBmd = BMDParser::Parse(itemPath + "Bow07.bmd");
    if (bowBmd) {
      m_models[crossbowIdx].weaponBmd = bowBmd.get();
      m_models[crossbowIdx].weaponAttachBone = 42; // Left hand
      m_models[crossbowIdx].defaultAction = 1;     // PLAYER_STOP_MALE (weapon on back)
      m_ownedBmds.push_back(std::move(bowBmd));
    }
    m_typeToModel[247] = crossbowIdx;
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
  if (npcType == 249 || npcType == 247)
    added.walkAction = 15;

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

  m_shader->use();
  m_shader->setMat4("projection", proj);
  m_shader->setMat4("view", view);

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

  // Point lights
  int plCount = std::min((int)m_pointLights.size(), MAX_POINT_LIGHTS);
  m_shader->setInt("numPointLights", plCount);
  for (int i = 0; i < plCount; ++i) {
    std::string idx = std::to_string(i);
    m_shader->setVec3("pointLightPos[" + idx + "]", m_pointLights[i].position);
    m_shader->setVec3("pointLightColor[" + idx + "]", m_pointLights[i].color);
    m_shader->setFloat("pointLightRange[" + idx + "]", m_pointLights[i].range);
  }

  for (auto &npc : m_npcs) {
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
    // Only blacksmith has an audible sound in Lorencia (hammer strike)
    if (npc.npcType == 251 && npc.action == 0) {
      float dx = npc.position.x - camPos.x;
      float dz = npc.position.z - camPos.z;
      if (dx * dx + dz * dz < 400.0f * 400.0f) {
        if (prevAnimFrame < 5.0f && npc.animFrame >= 5.0f)
          SoundManager::Play3D(SOUND_NPC_BLACKSMITH, npc.position.x,
                               npc.position.y, npc.position.z);
      }
    }

    // Guard patrol movement: interpolate toward move target
    if (npc.isMoving) {
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
    auto bones = ComputeBoneMatricesInterpolated(mdl.skeleton, npc.action,
                                                 npc.animFrame);

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

    m_shader->setMat4("model", model);

    // Terrain lightmap at NPC position
    glm::vec3 tLight = sampleTerrainLightAt(npc.position);
    m_shader->setVec3("terrainLight", tLight);

    // Blacksmith forge glow: BlendMesh=4, Luminosity=0.8 constant
    // Main 5.2: o->BlendMesh = 4; o->BlendMeshLight = Luminosity;
    // Luminosity for Lorencia is a constant 0.8f (ZzzCharacter.cpp:5500)
    bool isBlacksmith = (npc.npcType == 251);
    float forgeLum = 1.0f;
    if (isBlacksmith) {
      forgeLum = 0.8f;
      m_shader->setFloat("blendMeshLight", forgeLum);
    }

    // Draw all body part meshes
    int meshDrawIdx = 0;
    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden) {
          meshDrawIdx++;
          continue;
        }
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);

        // Blacksmith mesh 4 = forge glow (additive blend)
        bool forgeGlow = isBlacksmith && (mb.bmdTextureId == 4);

        if (forgeGlow || mb.bright) {
          glBlendFunc(GL_ONE, GL_ONE);
          glDepthMask(GL_FALSE);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
          glDepthMask(GL_TRUE);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (mb.noneBlend) {
          glDisable(GL_BLEND);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
          glEnable(GL_BLEND);
        } else {
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
        }
        meshDrawIdx++;
      }
    }

    // Reset blendMeshLight after blacksmith
    if (isBlacksmith) {
      m_shader->setFloat("blendMeshLight", 1.0f);
    }

    // ── Guard weapon rendering (Main 5.2: ZzzCharacter.cpp:13859-13890) ──
    if (mdl.weaponBmd && mdl.weaponAttachBone >= 0 &&
        !npc.weaponMeshBuffers.empty() &&
        mdl.weaponAttachBone < (int)bones.size()) {
      // Guard weapon on back (bone 47) — guards are always in safe zones
      // Same pattern as HeroCharacter::Draw back-carry (line 625-649)
      static constexpr int BONE_BACK = 47;
      int attachBone = (BONE_BACK < (int)bones.size())
                           ? BONE_BACK
                           : mdl.weaponAttachBone;

      // Back rotation (70,0,90) + offset (-20,5,55) for back carry
      BoneWorldMatrix offsetMat = (attachBone == BONE_BACK)
          ? MuMath::BuildWeaponOffsetMatrix(glm::vec3(70.f, 0.f, 90.f),
                                            glm::vec3(-20.f, 5.f, 55.f))
          : MuMath::BuildWeaponOffsetMatrix(glm::vec3(0.0f), glm::vec3(0.0f));

      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms(
          (const float(*)[4])bones[attachBone].data(),
          (const float(*)[4])offsetMat.data(), (float(*)[4])parentMat.data());

      // Use cached weapon local bones (static bind-pose, computed once at load)
      const auto &wLocalBones = mdl.cachedWeaponBones;
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                                 (const float(*)[4])wLocalBones[bi].data(),
                                 (float(*)[4])wFinalBones[bi].data());
      }

      // Re-skin weapon meshes
      for (int mi = 0; mi < (int)npc.weaponMeshBuffers.size() &&
                       mi < (int)mdl.weaponBmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(mdl.weaponBmd->Meshes[mi], wFinalBones,
                                 npc.weaponMeshBuffers[mi]);
      }

      // Draw weapon meshes
      for (auto &mb : npc.weaponMeshBuffers) {
        if (mb.indexCount == 0 || mb.hidden)
          continue;
        glBindTexture(GL_TEXTURE_2D, mb.texture);
        glBindVertexArray(mb.vao);
        if (mb.bright) {
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
  }
}

void NpcManager::RenderShadows(const glm::mat4 &view, const glm::mat4 &proj) {
  if (!m_shadowShader || m_npcs.empty())
    return;

  m_shadowShader->use();
  m_shadowShader->setMat4("projection", proj);
  m_shadowShader->setMat4("view", view);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);
  glDisable(GL_CULL_FACE);

  // Stencil per NPC to avoid overlap
  glEnable(GL_STENCIL_TEST);

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

    m_shadowShader->setMat4("model", model);

    // Clear stencil for this NPC
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    float cosF = cosf(npc.facing);
    float sinF = sinf(npc.facing);

    int smIdx = 0;
    for (auto &bp : npc.bodyParts) {
      BMDData *bmd = (bp.bmdIdx < 0) ? mdl.skeleton : mdl.parts[bp.bmdIdx];
      for (int mi = 0;
           mi < (int)bmd->Meshes.size() && smIdx < (int)npc.shadowMeshes.size();
           ++mi, ++smIdx) {
        auto &sm = npc.shadowMeshes[smIdx];
        if (sm.vertexCount == 0 || sm.vao == 0)
          continue;

        auto &mesh = bmd->Meshes[mi];
        std::vector<glm::vec3> shadowVerts;
        shadowVerts.reserve(sm.vertexCount);

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
            // Apply scale
            pos *= npc.scale;
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

        glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        shadowVerts.size() * sizeof(glm::vec3),
                        shadowVerts.data());
        glBindVertexArray(sm.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
      }
    }
  }

  glBindVertexArray(0);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
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

void NpcManager::RenderSilhouetteOutline(int npcIndex, const glm::mat4 &view,
                                          const glm::mat4 &proj) {
  if (!m_outlineShader || npcIndex < 0 || npcIndex >= (int)m_npcs.size())
    return;

  auto &npc = m_npcs[npcIndex];

  // Build model matrices: normal scale for stencil, slightly larger for outline
  glm::mat4 baseModel = glm::translate(glm::mat4(1.0f), npc.position);
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  baseModel = glm::rotate(baseModel, glm::radians(-90.0f), glm::vec3(0, 1, 0));
  baseModel = glm::rotate(baseModel, npc.facing, glm::vec3(0, 0, 1));

  glm::mat4 stencilModel = glm::scale(baseModel, glm::vec3(npc.scale));

  m_outlineShader->use();
  m_outlineShader->setMat4("projection", proj);
  m_outlineShader->setMat4("view", view);

  glDisable(GL_CULL_FACE);

  // === Pass 1: Write COMPLETE silhouette to stencil ===
  // Depth test OFF so ALL mesh pixels get stenciled (no gaps between parts)
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  glStencilMask(0xFF);
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);

  m_outlineShader->setMat4("model", stencilModel);
  m_outlineShader->setFloat("outlineThickness", 0.0f);

  for (auto &bp : npc.bodyParts) {
    for (auto &mb : bp.meshBuffers) {
      if (mb.indexCount == 0 || mb.hidden)
        continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }
  for (auto &mb : npc.weaponMeshBuffers) {
    if (mb.indexCount == 0)
      continue;
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
  }

  // === Pass 2: Multi-layer soft glow where stencil != 1 ===
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
  glStencilMask(0x00);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  m_outlineShader->setVec3("outlineColor", 0.8f, 0.4f, 0.15f);

  // Render multiple layers from outermost (faint) to innermost (bright)
  // Normal extrusion for uniform width
  constexpr float thicknesses[] = {5.0f, 3.5f, 2.0f};
  constexpr float alphas[] = {0.08f, 0.18f, 0.35f};

  m_outlineShader->setMat4("model", stencilModel);

  for (int layer = 0; layer < 3; ++layer) {
    m_outlineShader->setFloat("outlineThickness", thicknesses[layer]);
    m_outlineShader->setFloat("outlineAlpha", alphas[layer]);

    for (auto &bp : npc.bodyParts) {
      for (auto &mb : bp.meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden)
          continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
      }
    }
    for (auto &mb : npc.weaponMeshBuffers) {
      if (mb.indexCount == 0)
        continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    }
  }

  // Restore state
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(0);
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
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
    }
    // Weapon meshes (guards)
    CleanupMeshBuffers(npc.weaponMeshBuffers);
    for (auto &sm : npc.weaponShadowMeshes) {
      if (sm.vao)
        glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo)
        glDeleteBuffers(1, &sm.vbo);
    }
  }
  m_npcs.clear();
  m_models.clear();
  m_ownedBmds.clear();
  m_shader.reset();
  m_shadowShader.reset();
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
