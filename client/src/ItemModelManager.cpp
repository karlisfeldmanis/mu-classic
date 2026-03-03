#include "ItemModelManager.hpp"
#include "BMDUtils.hpp"
#include "ItemDatabase.hpp"
#include "Shader.hpp"
#include <GL/glew.h>
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

// Main 5.2: ItemLight — returns BlendMesh index for weapons with glow
int ItemModelManager::GetItemBlendMesh(int category, int itemIndex) {
  if (category == 3 && itemIndex == 0) return 1; // Light Spear: mesh 1 glows
  return -1; // No glow
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
    {270.f, 270.f, 0.f},   //  6 Shields
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

void ItemModelManager::Init(Shader *shader, const std::string &dataPath) {
  s_shader = shader;
  s_dataPath = dataPath;

  // Load shadow shader (same as monsters/NPCs/hero)
  std::ifstream shaderTest("shaders/shadow.vert");
  s_shadowShader = std::make_unique<Shader>(
      shaderTest.good() ? "shaders/shadow.vert" : "../shaders/shadow.vert",
      shaderTest.good() ? "shaders/shadow.frag" : "../shaders/shadow.frag");
}

static void UploadStaticMesh(const Mesh_t &mesh, const std::string &texPath,
                             const std::vector<BoneWorldMatrix> &bones,
                             const std::string &modelFile,
                             std::vector<MeshBuffers> &outBuffers) {
  MeshBuffers mb;
  mb.isDynamic = false;

  // Resolve texture
  auto texInfo = TextureLoader::ResolveWithInfo(texPath, mesh.TextureName);
  mb.texture = texInfo.textureID;
  mb.hasAlpha = texInfo.hasAlpha;

  // Parse script flags from texture name
  auto flags = TextureLoader::ParseScriptFlags(mesh.TextureName);
  mb.bright = flags.bright;
  mb.hidden = flags.hidden;
  mb.noneBlend = flags.noneBlend;

  // Force additive blending for Wings and specific pets to hide black JPEG
  // backgrounds
  {
    std::string texLower = mesh.TextureName;
    std::transform(texLower.begin(), texLower.end(), texLower.begin(),
                   ::tolower);
    std::string modelLower = modelFile;
    std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(),
                   ::tolower);

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

  // Expand vertices per-triangle-corner (matching ObjectRenderer::UploadMesh).
  // BMD stores separate VertexIndex, NormalIndex, TexCoordIndex per triangle
  // corner — we must create a unique vertex for each corner to preserve
  // per-face normals and UVs.
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 tex;
  };
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  for (int i = 0; i < mesh.NumTriangles; ++i) {
    auto &tri = mesh.Triangles[i];
    int steps = (tri.Polygon == 3) ? 3 : 4;
    int startIdx = (int)vertices.size();

    // First triangle (0,1,2)
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

    // Second triangle for quads (0,2,3)
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
        indices.push_back((int)vertices.size() - 1);
      }
    }
  }

  mb.vertexCount = (int)vertices.size();
  mb.indexCount = (int)indices.size();

  if (mb.indexCount == 0) {
    outBuffers.push_back(mb);
    return;
  }

  // Upload to GPU
  glGenVertexArrays(1, &mb.vao);
  glGenBuffers(1, &mb.vbo);
  glGenBuffers(1, &mb.ebo);

  glBindVertexArray(mb.vao);

  glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Layout: Pos(3) + Norm(3) + UV(2) = 8 floats stride
  GLsizei stride = sizeof(Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 3));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(sizeof(float) * 6));

  glBindVertexArray(0);
  outBuffers.push_back(mb);
}

LoadedItemModel *ItemModelManager::Get(const std::string &filename) {
  if (filename.empty())
    return nullptr;
  auto it = s_cache.find(filename);
  if (it != s_cache.end())
    return &it->second;

  // Load new — try Item/ first, then Player/ (armor models live there)
  LoadedItemModel model;
  std::string foundDir = "Item"; // default
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
    s_cache[filename] = {}; // Cache empty to avoid retry
    return nullptr;
  }

  // For body parts (found in Player/), use Player.bmd idle pose (action 1)
  // instead of the body part's own single-frame bind pose which looks unnatural.
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

  // Use Player.bmd idle bones for body parts, own bind pose for everything else
  auto bones = (isPlayerBodyPart && !s_playerIdleBones.empty())
                   ? s_playerIdleBones
                   : ComputeBoneMatrices(model.bmd.get(), 0, 0);
  std::string texPath = s_dataPath + "/" + foundDir + "/";

  // Compute transformed AABB from bone-transformed vertices
  glm::vec3 tMin(1e9f), tMax(-1e9f);
  for (const auto &mesh : model.bmd->Meshes) {
    UploadStaticMesh(mesh, texPath, bones, filename, model.meshes);
    // Accumulate AABB from transformed positions
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
    model.shadowMeshes.push_back(sm);
  }

  s_cache[filename] = std::move(model);
  return &s_cache[filename];
}

void ItemModelManager::RenderItemUI(const std::string &modelFile,
                                    int16_t defIndex, int x, int y, int w,
                                    int h, bool hovered) {
  LoadedItemModel *model = Get(modelFile);
  if (!model || !model->bmd)
    return;

  // Preserve GL state
  GLint lastViewport[4];
  glGetIntegerv(GL_VIEWPORT, lastViewport);
  GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);

  // Setup viewport + scissor (glClear respects scissor, not viewport)
  glViewport(x, y, w, h); // Note: y is from bottom in GL
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, w, h);
  glEnable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT); // Clear depth only for this slot

  // Check shader
  Shader *shader = s_shader;
  if (!shader)
    return;
  shader->use();

  // Auto-fit camera/model based on bone-transformed AABB
  glm::vec3 min = model->transformedMin;
  glm::vec3 max = model->transformedMax;
  glm::vec3 size = max - min;
  glm::vec3 center = (min + max) * 0.5f;
  float maxDim = std::max(std::max(size.x, size.y), size.z);
  if (maxDim < 1.0f)
    maxDim = 1.0f;

  // Use Orthographic projection for UI items to fill grid space perfectly
  float aspect = (float)w / (float)h;
  glm::mat4 proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -100.0f, 100.0f);

  // Camera looking at origin
  glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 50.0f), glm::vec3(0, 0, 0),
                                glm::vec3(0, 1, 0));

  // Model Transformation
  glm::mat4 mod = glm::mat4(1.0f);

  // 1. Orientation to make the item "stand up" vertically in the grid
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
    // MU models are Z-up; our UI camera looks down -Z with Y-up.
    // MU AngleMatrix applies: pitch(X) → yaw(Y) → roll(Z) in MU local space.
    // We first convert MU→GL coords (-90°X), then apply MU angles.
    const auto &pose = (category < kItemPoseCount)
                           ? kItemPoses[category]
                           : kItemPoses[7]; // fallback = helm pose
    mod = glm::rotate(mod, glm::radians(pose.pitch), glm::vec3(1, 0, 0));
    mod = glm::rotate(mod, glm::radians(pose.yaw), glm::vec3(0, 1, 0));
    mod = glm::rotate(mod, glm::radians(pose.roll), glm::vec3(0, 0, 1));
  } else {
    // Zen/Default: Use helm pose
    mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(1, 0, 0));
  }

  // 2. Consistent 360 spin around the GRID'S vertical axis (Y) on hover
  if (hovered) {
    float spin = (float)glfwGetTime() * 180.0f;
    // Apply spin AFTER orientation so it's always around the screen's Y axis
    mod =
        glm::rotate(glm::mat4(1.0f), glm::radians(spin), glm::vec3(0, 1, 0)) *
        mod;
  }

  // 3. Transformation order: Scale * (Spin * Orientation) * Translation
  // Scale to fit: map maxDim to ~1.8 (leaving small margin in 2.0 range)
  float scale = 1.8f / maxDim;
  mod = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * mod;

  // Center the model locally before any rotation
  mod = mod * glm::translate(glm::mat4(1.0f), -center);

  shader->setMat4("projection", proj);
  shader->setMat4("view", view);
  shader->setMat4("model", mod);
  // Set ALL lighting uniforms explicitly for UI — don't rely on stale
  // world-pass values
  shader->setVec3("lightPos", glm::vec3(0, 50, 50.0f));
  shader->setVec3("viewPos", glm::vec3(0, 0, 50.0f));
  shader->setVec3("lightColor",
                  glm::vec3(1.0f, 1.0f, 1.0f)); // Pure white light
  shader->setFloat("blendMeshLight", 1.0f);     // No mesh darkening
  shader->setVec3("terrainLight",
                  glm::vec3(1.0f, 1.0f, 1.0f)); // No terrain darkening
  shader->setFloat("luminosity", 1.0f);         // Full brightness
  shader->setInt("numPointLights", 0);          // No point lights in UI
  shader->setBool("useFog", false);             // No fog in UI
  shader->setFloat("objectAlpha", 1.0f);        // Fully opaque

  // For body-part items (cat 7-11), determine which meshes are skin/head
  // by checking texture names. Body part BMDs include the character skin mesh
  // which should be hidden in inventory/shop display.
  bool isBodyPart = (category >= 7 && category <= 11);

  // Main 5.2: ItemLight — determine if this item has a glow mesh
  int itemIndex = (defIndex >= 0) ? (defIndex % 32) : -1;
  int blendMeshIdx = (category >= 0 && itemIndex >= 0)
                         ? GetItemBlendMesh(category, itemIndex)
                         : -1;

  // Render — disable face culling for double-sided meshes (pet wings etc.)
  glDisable(GL_CULL_FACE);
  for (int mi = 0; mi < (int)model->meshes.size(); ++mi) {
    const auto &mb = model->meshes[mi];
    if (mb.hidden)
      continue;

    // Skip skin/body meshes for body part items in UI.
    // For helms (cat 7): keep head_ meshes (that IS the helm), skip skin_/hide.
    // For armor/pants/gloves/boots (cat 8-11): skip head_, skin_, hide.
    if (isBodyPart && mi < (int)model->bmd->Meshes.size()) {
      std::string texLower = model->bmd->Meshes[mi].TextureName;
      std::transform(texLower.begin(), texLower.end(), texLower.begin(),
                     ::tolower);
      if (texLower.find("skin_") != std::string::npos ||
          texLower.find("hide") != std::string::npos)
        continue;
      if (category != 7 && texLower.find("head_") != std::string::npos)
        continue;
    }
    glBindVertexArray(mb.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    shader->setInt("diffuseMap", 0);
    shader->setBool("useTexture", true);
    shader->setVec3("colorTint", glm::vec3(1));

    // Main 5.2: ItemLight — glow mesh renders additively with pulsing brightness
    bool isGlowMesh = (blendMeshIdx >= 0 && mi == blendMeshIdx);

    if (isGlowMesh) {
      float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
      shader->setFloat("blendMeshLight", pulseLight);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE); // Additive
      glDepthMask(GL_FALSE);
    } else if (mb.hasAlpha || mb.bright) {
      glEnable(GL_BLEND);
      glDepthMask(GL_FALSE);
      if (mb.bright)
        glBlendFunc(GL_ONE, GL_ONE);
      else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
      glDepthMask(GL_TRUE);
    }

    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);
    glDepthMask(GL_TRUE);

    if (isGlowMesh)
      shader->setFloat("blendMeshLight", 1.0f); // Restore
  }
  glEnable(GL_CULL_FACE);
  glBindVertexArray(0);

  // Restore
  glDisable(GL_SCISSOR_TEST);
  glViewport(lastViewport[0], lastViewport[1], lastViewport[2],
              lastViewport[3]);
  if (!depthTest)
    glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
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
  shader->use();

  // Center the model using transformed AABB before rotating
  glm::vec3 tCenter = (model->transformedMin + model->transformedMax) * 0.5f;
  glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);

  // Apply resting rotation
  if (rotation.x != 0)
    mod = glm::rotate(mod, glm::radians(rotation.x), glm::vec3(1, 0, 0));
  if (rotation.y != 0)
    mod = glm::rotate(mod, glm::radians(rotation.y), glm::vec3(0, 1, 0));
  if (rotation.z != 0)
    mod = glm::rotate(mod, glm::radians(rotation.z), glm::vec3(0, 0, 1));

  mod = glm::scale(mod, glm::vec3(scale));
  mod = glm::translate(mod, -tCenter); // Center before rotate

  shader->setMat4("projection", proj);
  shader->setMat4("view", view);
  shader->setMat4("model", mod);
  shader->setVec3("colorTint", glm::vec3(1)); // Reset tint

  // Main 5.2: ItemLight — determine if this item has a glow mesh
  int category = (defIndex >= 0) ? (defIndex / 32) : -1;
  int itemIndex = (defIndex >= 0) ? (defIndex % 32) : -1;
  int blendMeshIdx = (category >= 0) ? GetItemBlendMesh(category, itemIndex) : -1;

  for (int mi = 0; mi < (int)model->meshes.size(); ++mi) {
    const auto &mb = model->meshes[mi];
    if (mb.hidden)
      continue;
    glBindVertexArray(mb.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    shader->setInt("diffuseMap", 0);
    shader->setBool("useTexture", true);

    bool isGlowMesh = (blendMeshIdx >= 0 && mi == blendMeshIdx);

    if (isGlowMesh) {
      float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
      shader->setFloat("blendMeshLight", pulseLight);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE); // Additive
      glDepthMask(GL_FALSE);
    } else if (mb.hasAlpha || mb.bright) {
      glEnable(GL_BLEND);
      if (mb.bright)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }

    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, 0);

    if (isGlowMesh) {
      shader->setFloat("blendMeshLight", 1.0f);
      glDepthMask(GL_TRUE);
    }
  }
  glBindVertexArray(0);
  glDisable(GL_BLEND);
}

void ItemModelManager::RenderItemWorldShadow(const std::string &filename,
                                              const glm::vec3 &pos,
                                              const glm::mat4 &view,
                                              const glm::mat4 &proj,
                                              float scale,
                                              glm::vec3 rotation) {
  if (!s_shadowShader)
    return;
  LoadedItemModel *model = Get(filename);
  if (!model || !model->bmd)
    return;

  // Shadow model matrix: translate + MU coordinate basis (no item rotation --
  // rotation is baked into vertices before shadow projection, same as hero
  // facing)
  glm::mat4 mod = glm::translate(glm::mat4(1.0f), pos);
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 0, 1));
  mod = glm::rotate(mod, glm::radians(-90.0f), glm::vec3(0, 1, 0));

  s_shadowShader->use();
  s_shadowShader->setMat4("projection", proj);
  s_shadowShader->setMat4("view", view);
  s_shadowShader->setMat4("model", mod);

  // Build rotation matrix for item resting angle (applied to vertices in
  // MU-local space before shadow projection, same as facing is applied for
  // characters)
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

  // Compute bone matrices once (static bind pose)
  auto bones = ComputeBoneMatrices(model->bmd.get(), 0, 0);

  for (int mi = 0; mi < (int)model->bmd->Meshes.size() &&
                    mi < (int)model->shadowMeshes.size();
       ++mi) {
    auto &sm = model->shadowMeshes[mi];
    if (sm.vertexCount == 0 || sm.vao == 0)
      continue;

    auto &mesh = model->bmd->Meshes[mi];
    std::vector<glm::vec3> shadowVerts;
    shadowVerts.reserve(sm.vertexCount);

    auto projectVertex = [&](int vertIdx) {
      auto &srcVert = mesh.Vertices[vertIdx];
      glm::vec3 pos = srcVert.Position;

      // Apply bone transform (bind pose)
      int boneIdx = srcVert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        pos = MuMath::TransformPoint(
            (const float(*)[4])bones[boneIdx].data(), pos);
      }

      // Center, scale, then apply resting rotation (in MU-local space)
      pos -= tCenter;
      pos = glm::vec3(scaleMat * glm::vec4(pos, 1.0f));
      pos = glm::vec3(rotMat * glm::vec4(pos, 1.0f));

      // Flatten shadow to ground (items lie flat, so simple projection
      // avoids perspective distortion from extreme rotation angles)
      pos.z = 5.0f;
      shadowVerts.push_back(pos);
    };

    for (int i = 0; i < mesh.NumTriangles; ++i) {
      auto &tri = mesh.Triangles[i];
      int steps = (tri.Polygon == 3) ? 3 : 4;

      // First triangle (0,1,2)
      for (int v = 0; v < 3; ++v)
        projectVertex(tri.VertexIndex[v]);

      // Second triangle for quads (0,2,3)
      if (steps == 4) {
        int quadIndices[3] = {0, 2, 3};
        for (int v : quadIndices)
          projectVertex(tri.VertexIndex[v]);
      }
    }

    glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    shadowVerts.size() * sizeof(glm::vec3),
                    shadowVerts.data());
    glBindVertexArray(sm.vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadowVerts.size());
  }
  glBindVertexArray(0);
}
