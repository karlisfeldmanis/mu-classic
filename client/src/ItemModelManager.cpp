#include "ItemModelManager.hpp"
#include "BMDUtils.hpp"
#include "ChromeGlow.hpp"
#include "ItemDatabase.hpp"
#include "Shader.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <iostream>

std::map<std::string, LoadedItemModel> ItemModelManager::s_cache;
Shader *ItemModelManager::s_shader = nullptr;
std::unique_ptr<Shader> ItemModelManager::s_shadowShader;
std::string ItemModelManager::s_dataPath;
std::shared_ptr<BMDData> ItemModelManager::s_playerBmd;
std::vector<BoneWorldMatrix> ItemModelManager::s_playerIdleBones;
int ItemModelManager::s_fbW = 1280;
int ItemModelManager::s_fbH = 720;

// Per-frame counter for assigning unique BGFX view IDs to each item slot.
// Reset in SetFramebufferSize (called once per frame before the render loop).
static int s_itemViewCounter = 0;

// BGFX view IDs for item UI slots start at 31 (after ImGui main at 30)
static constexpr bgfx::ViewId ITEM_UI_VIEW_BASE = 31;

void ItemModelManager::SetFramebufferSize(int w, int h) {
  s_fbW = w;
  s_fbH = h;
  s_itemViewCounter = 0;
}
// Main 5.2: ItemLight — returns BlendMesh index for weapons with glow
int ItemModelManager::GetItemBlendMesh(int category, int itemIndex) {
  // Main 5.2 ItemObjectAttribute (ZzzObject.cpp:5286-5435)
  // Returns: >=0 = additive mesh index, -1 = none, -2 = whole-object pulse
  switch (category) {
  case 0: // Swords
    if (itemIndex == 5 || itemIndex == 10 || itemIndex == 13 || itemIndex == 14)
      return 1;
    break;
  case 2: // Maces
    if (itemIndex == 4 || itemIndex == 6) return 1;
    if (itemIndex == 5) return 0;
    break;
  case 3: // Spears
    if (itemIndex == 0) return 1;
    break;
  case 4: // Bows — Main 5.2: BlendMesh=-2 (whole-object oscillating brightness)
    if (itemIndex == 6 || itemIndex == 13 || itemIndex == 14 || itemIndex == 16)
      return -2;
    break;
  case 5: // Staffs
    if (itemIndex == 0 || itemIndex == 5 || itemIndex == 11) return 2;
    if (itemIndex == 7 || itemIndex == 9) return 1;
    if (itemIndex == 6 || itemIndex == 8) return -2; // Whole-object pulse
    break;
  case 6: // Shields
    if (itemIndex == 11 || itemIndex == 12 || itemIndex == 13 || itemIndex == 14)
      return 1;
    break;
  case 12: // Wings — Main 5.2: only Wing+0 and Wing+3 have BlendMesh=0
    if (itemIndex == 0 || itemIndex == 3) return 0;
    break;
  case 13: // Helpers
    if (itemIndex == 0) return 1; // Guardian Angel
    break;
  }
  return -1;
}

// Main 5.2: HiddenMesh — hide meshes with matching Texture index
int ItemModelManager::GetItemHiddenMesh(int category, int itemIndex) {
  if (category == 2 && itemIndex == 7) return 2;  // Battle Scepter
  if (category == 3 && itemIndex == 10) return 1;  // Dragon Spear
  if (category == 6 && itemIndex == 16) return 2;  // Elemental Shield
  return -1; // No hidden mesh
}

// Per-category display poses from Main 5.2 RenderObjectScreen()
// Angles are MU Euler: (pitch, yaw, roll) in degrees
struct ItemDisplayPose {
  float pitch, yaw, roll;
};
static const ItemDisplayPose kItemPoses[] = {
    {180.f, 270.f, 15.f},  //  0 Swords
    {180.f, 270.f, 15.f},  //  1 Axes
    {180.f, 270.f, 15.f},  //  2 Maces/Flails
    {0.f, 90.f, 20.f},     //  3 Spears
    {0.f, 270.f, 15.f},    //  4 Bows
    {180.f, 270.f, 25.f},  //  5 Staffs
    {270.f, 270.f, 0.f},   //  6 Shields — Main 5.2: Vector(270,270,0)
    {-90.f, 0.f, 0.f},     //  7 Helms
    {-90.f, 0.f, 0.f},     //  8 Armor
    {-90.f, 0.f, 0.f},     //  9 Pants
    {-90.f, 0.f, 0.f},     // 10 Gloves
    {-90.f, 0.f, 0.f},     // 11 Boots
    {270.f, -10.f, 0.f},   // 12 Wings
    {270.f, -10.f, 0.f},   // 13 Accessories
    {270.f, -10.f, 0.f},   // 14 Potions
};
static constexpr int kItemPoseCount =
    sizeof(kItemPoses) / sizeof(kItemPoses[0]);

// BGFX vertex layout for static item meshes (same as ObjectRenderer)
static bgfx::VertexLayout s_itemLayout;
static bool s_layoutInit = false;

static void EnsureLayout() {
  if (!s_layoutInit) {
    s_itemLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    s_layoutInit = true;
  }
}

// Shadow vertex layout (position only)
static bgfx::VertexLayout s_shadowLayout;
static bool s_shadowLayoutInit = false;

static void EnsureShadowLayout() {
  if (!s_shadowLayoutInit) {
    s_shadowLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    s_shadowLayoutInit = true;
  }
}

void ItemModelManager::Init(Shader *shader, const std::string &dataPath) {
  s_shader = shader;
  s_dataPath = dataPath;

  // Load shadow shader (same as monsters/NPCs/hero)
  s_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");
}

static void UploadStaticMesh(const Mesh_t &mesh, const std::string &texPath,
                             const std::vector<BoneWorldMatrix> &bones,
                             const std::string &modelFile,
                             std::vector<MeshBuffers> &outBuffers) {
  MeshBuffers mb;
  mb.isDynamic = false;

  // Resolve texture — try primary dir, then fallback to Item/ and Skill/
  auto texInfo = TextureLoader::ResolveWithInfo(texPath, mesh.TextureName);
  if (!TexValid(texInfo.textureID) && texPath.find("/Player/") != std::string::npos) {
    std::string itemPath = texPath;
    auto p = itemPath.find("/Player/");
    itemPath.replace(p, 8, "/Item/");
    texInfo = TextureLoader::ResolveWithInfo(itemPath, mesh.TextureName);
    if (!TexValid(texInfo.textureID)) {
      std::string skillPath = texPath;
      p = skillPath.find("/Player/");
      skillPath.replace(p, 8, "/Skill/");
      texInfo = TextureLoader::ResolveWithInfo(skillPath, mesh.TextureName);
    }
  }
  mb.texture = texInfo.textureID;
  mb.hasAlpha = texInfo.hasAlpha;
  mb.textureName = mesh.TextureName;

  // Parse script flags from texture name
  auto flags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.bright = flags.bright;
  mb.hidden = flags.hidden;
  mb.noneBlend = flags.noneBlend;
  mb.bmdTextureId = mesh.Texture;

  // Force additive blending for Wings and specific pets
  {
    std::string texLower = mesh.TextureName;
    std::transform(texLower.begin(), texLower.end(), texLower.begin(), ::tolower);
    std::string modelLower = modelFile;
    std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(), ::tolower);

    if (texLower.find("wing") != std::string::npos ||
        modelLower.find("wing") != std::string::npos ||
        texLower.find("fairy2") != std::string::npos ||
        texLower.find("satan2") != std::string::npos ||
        texLower.find("unicon01") != std::string::npos ||
        texLower.find("flail00") != std::string::npos) {
      mb.bright = true;
    }
  }

  if (mb.hidden)
    return;

  // Expand vertices per-triangle-corner (same as ObjectRenderer::UploadMesh)
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();

    for (int v = 0; v < 3; ++v) {
      Vertex vert;
      auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
      auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        const auto &bm = bones[boneIdx];
        vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                          srcVert.Position);
        vert.normal =
            MuMath::RotateVector((const float(*)[4])bm.data(), srcNorm.Normal);
      } else {
        vert.pos = srcVert.Position;
        vert.normal = srcNorm.Normal;
      }

      vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                           mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
      vertices.push_back(vert);
      indices.push_back(startIdx + v);
    }

    if (steps == 4) {
      int quadIndices[3] = {0, 2, 3};
      for (int v : quadIndices) {
        Vertex vert;
        auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
        auto &srcNorm = mesh.Normals[tri.NormalIndex[v]];

        int boneIdx = srcVert.Node;
        if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
          const auto &bm = bones[boneIdx];
          vert.pos = MuMath::TransformPoint((const float(*)[4])bm.data(),
                                            srcVert.Position);
          vert.normal = MuMath::RotateVector((const float(*)[4])bm.data(),
                                             srcNorm.Normal);
        } else {
          vert.pos = srcVert.Position;
          vert.normal = srcNorm.Normal;
        }

        vert.tex = glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                             mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV);
        vertices.push_back(vert);
        indices.push_back((uint32_t)vertices.size() - 1);
      }
    }
  }

  mb.vertexCount = (int)vertices.size();
  mb.indexCount = (int)indices.size();

  if (mb.indexCount == 0) {
    outBuffers.push_back(mb);
    return;
  }

  EnsureLayout();

  uint32_t vbSize = (uint32_t)(vertices.size() * sizeof(Vertex));
  uint32_t ibSize = (uint32_t)(indices.size() * sizeof(uint32_t));

  mb.vbo = bgfx::createVertexBuffer(
      bgfx::copy(vertices.data(), vbSize), s_itemLayout);
  mb.ebo = bgfx::createIndexBuffer(
      bgfx::copy(indices.data(), ibSize), BGFX_BUFFER_INDEX32);

  outBuffers.push_back(mb);
}

LoadedItemModel *ItemModelManager::Get(const std::string &filename) {
  if (filename.empty())
    return nullptr;
  auto it = s_cache.find(filename);
  if (it != s_cache.end())
    return &it->second;

  // Load item model from BMD
  // Search Item/ first, then Player/ (armor models live there)
  LoadedItemModel model;
  std::string foundDir = "Item";
  const char *searchDirs[] = {"Item", "Player"};

  for (const char *dir : searchDirs) {
    std::string path = s_dataPath + "/" + dir + "/" + filename;
    model.bmd = BMDParser::Parse(path);
    if (model.bmd) {
      foundDir = dir;
      break;
    }
  }
  if (!model.bmd) {
    std::cerr << "[Item] Failed to load " << filename
              << " (searched Item/ and Player/)" << std::endl;
    s_cache[filename] = {};
    return nullptr;
  }

  // For body parts (found in Player/), use Player.bmd idle pose
  bool isPlayerBodyPart = false;
  if (foundDir == "Player") {
    std::string fLower = filename;
    std::transform(fLower.begin(), fLower.end(), fLower.begin(), ::tolower);
    isPlayerBodyPart = (fLower.find("helm") != std::string::npos ||
                        fLower.find("armor") != std::string::npos ||
                        fLower.find("pant") != std::string::npos ||
                        fLower.find("glove") != std::string::npos ||
                        fLower.find("boot") != std::string::npos);
  }

  // Lazily load Player.bmd skeleton for idle pose computation
  if (isPlayerBodyPart && !s_playerBmd) {
    s_playerBmd = BMDParser::Parse(s_dataPath + "/Player/Player.bmd");
    if (s_playerBmd) {
      s_playerIdleBones =
          ComputeBoneMatrices(s_playerBmd.get(), 1, 0); // Action 1 = idle
    }
  }

  auto bones = (isPlayerBodyPart && !s_playerIdleBones.empty())
                   ? s_playerIdleBones
                   : ComputeBoneMatrices(model.bmd.get(), 0, 0);
  std::string texPath = s_dataPath + "/" + foundDir + "/";

  // Compute transformed AABB from bone-transformed vertices
  glm::vec3 tMin(1e9f), tMax(-1e9f);
  for (const auto &mesh : model.bmd->Meshes) {
    UploadStaticMesh(mesh, texPath, bones, filename, model.meshes);
    for (int vi = 0; vi < (int)mesh.Vertices.size(); ++vi) {
      glm::vec3 pos = mesh.Vertices[vi].Position;
      int boneIdx = mesh.Vertices[vi].Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        pos = MuMath::TransformPoint((const float(*)[4])bones[boneIdx].data(),
                                     pos);
      }
      tMin = glm::min(tMin, pos);
      tMax = glm::max(tMax, pos);
    }
  }
  model.transformedMin = tMin;
  model.transformedMax = tMax;

  // Create shadow mesh buffers (dynamic, position-only) for each mesh
  EnsureShadowLayout();
  for (const auto &mesh : model.bmd->Meshes) {
    ItemShadowMesh sm;
    int shadowVertCount = 0;
    for (int t = 0; t < mesh.NumTriangles; ++t) {
      shadowVertCount += (mesh.Triangles[t].Polygon == 4) ? 6 : 3;
    }
    sm.vertexCount = shadowVertCount;
    if (sm.vertexCount == 0) {
      model.shadowMeshes.push_back(sm);
      continue;
    }
    sm.vbo = bgfx::createDynamicVertexBuffer(
        sm.vertexCount, s_shadowLayout, BGFX_BUFFER_ALLOW_RESIZE);
    model.shadowMeshes.push_back(sm);
  }

  s_cache[filename] = std::move(model);
  return &s_cache[filename];
}

void ItemModelManager::RenderItemUI(const std::string &modelFile,
                                    int16_t defIndex, int x, int y, int w,
                                    int h, bool hovered, uint8_t itemLevel,
                                    bool overlay) {
  LoadedItemModel *model = Get(modelFile);
  if (!model || !model->bmd)
    return;

  Shader *shader = s_shader;
  if (!shader)
    return;

  // Allocate a unique BGFX view for this item slot
  bgfx::ViewId viewId = ITEM_UI_VIEW_BASE + s_itemViewCounter++;
  if (viewId >= 200) return; // Safety: don't overlap with overlay views

  // Setup BGFX view: viewport to item rect
  // Overlay mode: no clear at all (accessory slots render on top of other items)
  bgfx::setViewRect(viewId, uint16_t(x), uint16_t(y), uint16_t(w), uint16_t(h));
  if (!overlay)
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
  bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

  // Auto-fit camera/model based on bone-transformed AABB
  glm::vec3 min = model->transformedMin;
  glm::vec3 max = model->transformedMax;
  glm::vec3 size = max - min;
  glm::vec3 center = (min + max) * 0.5f;
  float maxDim = std::max(std::max(size.x, size.y), size.z);
  if (maxDim < 1.0f)
    maxDim = 1.0f;

  // Orthographic projection for UI items
  float aspect = (float)w / (float)h;
  glm::mat4 proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -100.0f, 100.0f);
  glm::mat4 viewMat = glm::lookAt(glm::vec3(0, 0, 50.0f), glm::vec3(0, 0, 0),
                                   glm::vec3(0, 1, 0));

  bgfx::setViewTransform(viewId, glm::value_ptr(viewMat), glm::value_ptr(proj));
  bgfx::touch(viewId);

  // Model Transformation
  glm::mat4 mod = glm::mat4(1.0f);

  auto &itemDefs = ItemDatabase::GetItemDefs();
  int category = -1;
  if (defIndex != -1) {
    auto it = itemDefs.find(defIndex);
    if (it != itemDefs.end()) {
      category = it->second.category;
    } else {
      category = defIndex / 32;
    }

    // Apply per-category display pose from Main 5.2 RenderObjectScreen()
    const auto &pose = (category < kItemPoseCount)
                           ? kItemPoses[category]
                           : kItemPoses[7];
    mod = glm::rotate(mod, glm::radians(pose.roll), glm::vec3(0, 0, 1));
    mod = glm::rotate(mod, glm::radians(pose.yaw), glm::vec3(0, 1, 0));
    mod = glm::rotate(mod, glm::radians(pose.pitch), glm::vec3(1, 0, 0));
  } else {
    mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(1, 0, 0));
  }

  // Compute post-rotation AABB from static pose for stable scale
  {
    glm::vec3 rMin(1e9f), rMax(-1e9f);
    for (int ci = 0; ci < 8; ci++) {
      glm::vec3 corner(
          (ci & 1) ? max.x : min.x,
          (ci & 2) ? max.y : min.y,
          (ci & 4) ? max.z : min.z);
      glm::vec3 rc = glm::vec3(mod * glm::vec4(corner - center, 1.0f));
      rMin = glm::min(rMin, rc);
      rMax = glm::max(rMax, rc);
    }
    glm::vec3 rSize = rMax - rMin;
    float fitDim = std::max(rSize.x / aspect, rSize.y);
    if (fitDim < 1.0f) fitDim = 1.0f;
    // Scale to fit within slot with padding — per-category tuning
    float fillFactor = 1.55f;
    if (category >= 7 && category <= 11) fillFactor = 1.35f; // body parts
    else if (category == 6)              fillFactor = 1.40f; // shields
    else if (category == 13)             fillFactor = 1.40f; // rings/pendants/accessories
    else if (category == 14)             fillFactor = 1.45f; // potions/jewels
    else if (category == 12)             fillFactor = 1.45f; // orbs/wings
    // Only reduce for equipment accessory slots (pendant/ring), not bag grid
    float minSlotDim = (float)std::min(w, h);
    if (minSlotDim < 35.0f) fillFactor *= 0.85f; // tiny equip accessory slots only
    float scale = fillFactor / fitDim;
    mod = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * mod;
  }

  // Hover spin after scaling
  if (hovered) {
    float spin = (float)glfwGetTime() * 180.0f;
    mod = glm::rotate(glm::mat4(1.0f), glm::radians(spin), glm::vec3(0, 1, 0)) * mod;
  }

  // Center the model
  mod = mod * glm::translate(glm::mat4(1.0f), -center);

  // Determine body part and BlendMesh state
  bool isBodyPart = (category >= 7 && category <= 11);
  int itemIndex = (defIndex >= 0) ? (defIndex % 32) : -1;
  int blendMeshIdx = (category >= 0 && itemIndex >= 0)
                         ? GetItemBlendMesh(category, itemIndex)
                         : -1;
  int hiddenMeshIdx = (category >= 0 && itemIndex >= 0)
                          ? GetItemHiddenMesh(category, itemIndex)
                          : -1;

  // +3/+5 tint and +7+ dim (Main 5.2 ZzzObject.cpp:10183-10227)
  float tNow = (float)glfwGetTime();
  float gLum = 0.8f + 0.2f * sinf(tNow * 2.0f);
  glm::vec3 tint(1.0f);
  float baseDim = 1.0f;
  if (itemLevel >= 5 && itemLevel < 7) {
    tint = glm::vec3(gLum * 0.5f, gLum * 0.7f, gLum); // blue tint
  } else if (itemLevel >= 3 && itemLevel < 7) {
    tint = glm::vec3(gLum, gLum * 0.6f, gLum * 0.6f); // red tint
  }
  if (itemLevel >= 9) baseDim = 0.9f;
  else if (itemLevel >= 7) baseDim = 0.8f;

  // Render meshes — no face culling for double-sided meshes
  for (int mi = 0; mi < (int)model->meshes.size(); ++mi) {
    const auto &mb = model->meshes[mi];
    if (mb.hidden || mb.indexCount == 0)
      continue;
    if (!TexValid(mb.texture))
      continue;

    // Skip skin/body meshes for body part items in UI
    if (isBodyPart && mi < (int)model->bmd->Meshes.size()) {
      std::string texLower = model->bmd->Meshes[mi].TextureName;
      std::transform(texLower.begin(), texLower.end(), texLower.begin(), ::tolower);
      if (texLower.find("skin_") != std::string::npos ||
          texLower.find("hide") != std::string::npos)
        continue;
      if (category != 7 && texLower.find("head_") != std::string::npos)
        continue;
    }

    // Skip hidden meshes (Main 5.2: HiddenMesh)
    if (hiddenMeshIdx >= 0 && mi < (int)model->bmd->Meshes.size() &&
        model->bmd->Meshes[mi].Texture == hiddenMeshIdx)
      continue;

    bgfx::setTransform(glm::value_ptr(mod));
    bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    shader->setTexture(0, "s_texColor", mb.texture);

    // Determine blend state
    bool isGlowMesh = (blendMeshIdx >= 0 && mi < (int)model->bmd->Meshes.size() &&
                        model->bmd->Meshes[mi].Texture == blendMeshIdx);

    float blendLight = 1.0f;
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_MSAA;

    if (isGlowMesh) {
      blendLight = sinf(tNow * 4.0f) * 0.3f + 0.7f;
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
      state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
    } else if (blendMeshIdx == -2) {
      // Whole-object oscillating brightness (Main 5.2: BlendMesh=-2)
      blendLight = sinf(tNow * 4.0f) * 0.3f + 0.7f;
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
    } else if (mb.bright) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
      state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
    } else if (mb.hasAlpha) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
    } else {
      // Standard alpha blend so transparent texels merge with panel background
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }

    // Set uniforms: u_params(objectAlpha, blendMeshLight, chromeMode, chromeTime)
    shader->setVec4("u_params", glm::vec4(1.0f, blendLight, 0.0f, 0.0f));
    shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)); // luminosity=1, no point lights
    shader->setVec4("u_lightPos", glm::vec4(0.0f, 50.0f, 50.0f, 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(0.0f, 0.0f, 50.0f, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(baseDim, baseDim, baseDim, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_baseTint", glm::vec4(tint, 0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)); // no fog
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_shadowParams", glm::vec4(0.0f));

    bgfx::setState(state);
    bgfx::submit(viewId, shader->program);

    if (isGlowMesh)
      blendLight = 1.0f;
  }

  // Chrome glow passes for +7/+9/+11/+13 (centralized via ChromeGlow module)
  if (itemLevel >= 7 && TexValid(ChromeGlow::GetTextures().chrome1) && category >= 0) {
    ChromeGlow::GlowRenderParams gp{};
    gp.modelMat = &mod;
    gp.enhanceLevel = itemLevel;
    gp.category = category;
    gp.itemIndex = itemIndex;
    gp.viewId = viewId;
    gp.time = tNow;
    gp.skipBlendMesh = blendMeshIdx;
    gp.colorScale = 1.0f;
    gp.viewPos = glm::vec3(0.0f, 0.0f, 50.0f);
    gp.luminosity = 1.0f;

    // Filter: skip skin/body meshes for body part items
    // Build a filtered mesh list (exclude skin textures for body parts)
    std::vector<MeshBuffers> glowMeshes;
    for (int mi = 0; mi < (int)model->meshes.size(); ++mi) {
      const auto &mb = model->meshes[mi];
      if (mb.hidden || mb.indexCount == 0) continue;
      if (!TexValid(mb.texture)) continue;
      if (isBodyPart && mi < (int)model->bmd->Meshes.size()) {
        std::string texLower = model->bmd->Meshes[mi].TextureName;
        std::transform(texLower.begin(), texLower.end(), texLower.begin(), ::tolower);
        if (texLower.find("skin_") != std::string::npos || texLower.find("hide") != std::string::npos) continue;
        if (category != 7 && texLower.find("head_") != std::string::npos) continue;
      }
      glowMeshes.push_back(mb);
    }
    ChromeGlow::RenderGlowMeshes(shader, glowMeshes, gp);
  }
}

void ItemModelManager::RenderItemWorld(const std::string &filename,
                                       const glm::vec3 &pos,
                                       const glm::mat4 &view,
                                       const glm::mat4 &proj, float scale,
                                       glm::vec3 rotation,
                                       int16_t defIndex) {
  LoadedItemModel *model = Get(filename);
  if (!model || !model->bmd)
    return;

  Shader *shader = s_shader;
  if (!shader)
    return;

  // Center the model using transformed AABB before rotating
  glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
  glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);

  // BMD-to-world coordinate basis (matches RenderItemWorldShadow lines 649-650)
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  // Main 5.2 AngleMatrix: ZYX rotation order (ZzzMathLib.cpp)
  if (rotation.z != 0)
    mod = glm::rotate(mod, glm::radians(rotation.z), glm::vec3(0, 0, 1));
  if (rotation.y != 0)
    mod = glm::rotate(mod, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  if (rotation.x != 0)
    mod = glm::rotate(mod, glm::radians(rotation.x), glm::vec3(1, 0, 0));

  mod = glm::scale(mod, glm::vec3(scale));
  mod = glm::translate(mod, -tCenter);

  int category = (defIndex >= 0) ? (defIndex / 32) : -1;
  int itemIndex = (defIndex >= 0) ? (defIndex % 32) : -1;
  int blendMeshIdx = (category >= 0) ? GetItemBlendMesh(category, itemIndex) : -1;
  int hiddenMeshIdx = (category >= 0) ? GetItemHiddenMesh(category, itemIndex) : -1;

  // Ground items: self-lit, no directional/terrain/point light reaction
  shader->setVec4("u_terrainLight", glm::vec4(0.85f, 0.85f, 0.85f, 0.0f));
  shader->setVec4("u_lightCount", glm::vec4(0.0f));
  shader->setVec4("u_params2", glm::vec4(1.0f, 0.01f, 0.0f, 0.0f));
  shader->setVec4("u_shadowParams", glm::vec4(0.0f));

  float tNow = (float)glfwGetTime();
  for (int mi = 0; mi < (int)model->meshes.size(); ++mi) {
    const auto &mb = model->meshes[mi];
    if (mb.hidden || mb.indexCount == 0) continue;
    if (!TexValid(mb.texture)) continue;
    // Skip hidden meshes (Main 5.2: HiddenMesh)
    if (hiddenMeshIdx >= 0 && mi < (int)model->bmd->Meshes.size() &&
        model->bmd->Meshes[mi].Texture == hiddenMeshIdx)
      continue;

    bgfx::setTransform(glm::value_ptr(mod));
    bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    shader->setTexture(0, "s_texColor", mb.texture);

    bool isGlowMesh = (blendMeshIdx >= 0 && mi < (int)model->bmd->Meshes.size() &&
                        model->bmd->Meshes[mi].Texture == blendMeshIdx);

    float blendLight = 1.0f;
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_MSAA;

    if (isGlowMesh) {
      blendLight = sinf(tNow * 4.0f) * 0.3f + 0.7f;
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
      state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
    } else if (blendMeshIdx == -2) {
      // Whole-object oscillating brightness (Main 5.2: BlendMesh=-2)
      blendLight = sinf(tNow * 4.0f) * 0.3f + 0.7f;
      state |= BGFX_STATE_CULL_CW;
    } else if (mb.bright) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    } else if (mb.hasAlpha) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                       BGFX_STATE_BLEND_INV_SRC_ALPHA);
    } else {
      state |= BGFX_STATE_CULL_CW;
    }

    shader->setVec4("u_params", glm::vec4(1.0f, blendLight, 0.0f, 0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));

    bgfx::setState(state);
    bgfx::submit(0, shader->program);
  }
}

void ItemModelManager::RenderItemWorldShadow(const std::string &filename,
                                              const glm::vec3 &pos,
                                              const glm::mat4 &view,
                                              const glm::mat4 &proj,
                                              float scale,
                                              glm::vec3 rotation,
                                              int16_t defIndex) {
  if (!s_shadowShader)
    return;
  LoadedItemModel *model = Get(filename);
  if (!model || !model->bmd)
    return;

  int category = (defIndex >= 0) ? (defIndex / 32) : -1;
  int itemIdx = (defIndex >= 0) ? (defIndex % 32) : -1;
  int blendMeshIdx = (category >= 0) ? GetItemBlendMesh(category, itemIdx) : -1;

  // Shadow model matrix: translate to world position + MU coordinate basis
  glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  // Build rotation matrix for item resting angle
  glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
  glm::mat4 rotMat(1.0f);
  if (rotation.x != 0)
    rotMat = glm::rotate(rotMat, glm::radians(rotation.x), glm::vec3(1, 0, 0));
  if (rotation.y != 0)
    rotMat = glm::rotate(rotMat, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  if (rotation.z != 0)
    rotMat = glm::rotate(rotMat, glm::radians(rotation.z), glm::vec3(0, 0, 1));
  glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scale));

  // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
  const float sx = 2000.0f;
  const float sy = 4000.0f;

  auto bones = ComputeBoneMatrices(model->bmd.get(), 0, 0);

  uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                        | BGFX_STATE_DEPTH_TEST_LESS
                        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                 BGFX_STATE_BLEND_INV_SRC_ALPHA);
  uint32_t shadowStencil = BGFX_STENCIL_TEST_EQUAL
                         | BGFX_STENCIL_FUNC_REF(0)
                         | BGFX_STENCIL_FUNC_RMASK(0xFF)
                         | BGFX_STENCIL_OP_FAIL_S_KEEP
                         | BGFX_STENCIL_OP_FAIL_Z_KEEP
                         | BGFX_STENCIL_OP_PASS_Z_INCR;

  static std::vector<glm::vec3> shadowVerts;

  for (int mi = 0; mi < (int)model->bmd->Meshes.size() &&
                    mi < (int)model->shadowMeshes.size();
       ++mi) {
    auto &sm = model->shadowMeshes[mi];
    if (sm.vertexCount == 0 || !bgfx::isValid(sm.vbo))
      continue;

    auto &mesh = model->bmd->Meshes[mi];

    // For items with BlendMesh: render ONLY the glow mesh as shadow
    if (blendMeshIdx >= 0 && mesh.Texture != blendMeshIdx)
      continue;

    shadowVerts.clear();

    auto projectVertex = [&](int vertIdx) {
      auto &srcVert = mesh.Vertices[vertIdx];
      glm::vec3 p = srcVert.Position;

      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        p = MuMath::TransformPoint(
            (const float(*)[4])bones[boneIdx].data(), p);
      }

      p -= tCenter;
      p = glm::vec3(scaleMat * glm::vec4(p, 1.0f));
      p = glm::vec3(rotMat * glm::vec4(p, 1.0f));

      if (p.z < sy) {
        float factor = 1.0f / (p.z - sy);
        p.x += p.z * (p.x + sx) * factor;
        p.y += p.z * (p.y + sx) * factor;
      }
      p.z = 5.0f;
      shadowVerts.push_back(p);
    };

    for (int i = 0; i < mesh.NumTriangles; ++i) {
      auto &tri = mesh.Triangles[i];
      int steps = (tri.Polygon == 3) ? 3 : 4;

      for (int v = 0; v < 3; ++v)
        projectVertex(tri.VertexIndex[v]);

      if (steps == 4) {
        int quadIndices[3] = {0, 2, 3};
        for (int v : quadIndices)
          projectVertex(tri.VertexIndex[v]);
      }
    }

    if (shadowVerts.empty()) continue;

    bgfx::update(sm.vbo, 0,
        bgfx::copy(shadowVerts.data(), (uint32_t)(shadowVerts.size() * sizeof(glm::vec3))));

    bgfx::setTransform(glm::value_ptr(mod));
    bgfx::setVertexBuffer(0, sm.vbo, 0, (uint32_t)shadowVerts.size());
    bgfx::setState(shadowState);
    bgfx::setStencil(shadowStencil);
    bgfx::submit(0, s_shadowShader->program);
  }
}
