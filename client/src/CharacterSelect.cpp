#include "CharacterSelect.hpp"
#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "ClientTypes.hpp"
#include "GrassRenderer.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "ObjectRenderer.hpp"
#include "PacketDefs.hpp"
#include "TextureLoader.hpp"
#include "imgui.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace CharacterSelect {

// ── Internal state ──

static Context s_ctx;
static bool s_initialized = false;

// Character slots from server
static CharSlot s_slots[MAX_SLOTS] = {};
static int s_slotCount = 0;
static int s_selectedSlot = -1;

// Creation modal state
static bool s_createOpen = false;
static char s_createName[11] = {};
static int s_createClass = 16; // Default to DK
static char s_statusMsg[64] = {};
static float s_statusTimer = 0.0f;

// Delete confirmation
static bool s_deleteConfirm = false;


// Terrain
static Terrain s_terrain;
static TerrainData s_terrainData;
static bool s_terrainLoaded = false;

// World objects (ruins, columns, trees)
static ObjectRenderer s_objectRenderer;

// Grass billboards
static GrassRenderer s_grassRenderer;

// Player.bmd skeleton (shared across all characters)
static std::unique_ptr<BMDData> s_playerSkeleton;

// Body part BMDs per class (5 parts: helm, armor, pant, glove, boot)
static constexpr int PART_COUNT = 5;
struct ClassParts {
  std::unique_ptr<BMDData> bmd[PART_COUNT];
  bool loaded = false;
};
static ClassParts s_classParts[4]; // 0=DW, 1=DK, 2=ELF, 3=MG

// Shadow mesh (VAO/VBO for projected shadow vertices)
struct ShadowMesh {
  unsigned int vao = 0, vbo = 0;
  int vertexCount = 0;
};

// Per-slot rendering data
struct SlotRender {
  std::vector<MeshBuffers> meshes[PART_COUNT];
  std::vector<ShadowMesh> shadowMeshes[PART_COUNT];
  std::unique_ptr<BMDData> partOverrideBmd[PART_COUNT]; // null = use class default
  float animFrame = 0.0f;

  // Weapon (right hand) — attached to bone 47 with back offsets
  std::unique_ptr<BMDData> weaponBmd;
  std::vector<MeshBuffers> weaponMeshes;
  std::vector<ShadowMesh> weaponShadowMeshes;
  std::vector<BoneWorldMatrix> weaponLocalBones;

  // Shield / left hand — attached to bone 47 with shield back offsets
  std::unique_ptr<BMDData> shieldBmd;
  std::vector<MeshBuffers> shieldMeshes;
  std::vector<ShadowMesh> shieldShadowMeshes;
  std::vector<BoneWorldMatrix> shieldLocalBones;

  // Base head (class default HelmClassXX.bmd) shown under accessory helms
  std::vector<MeshBuffers> baseHeadMeshes;
  std::vector<ShadowMesh> baseHeadShadowMeshes;
  bool showBaseHead = false;

  // BlendMesh glow indices (-1 = none)
  int weaponBlendMesh = -1;
  int shieldBlendMesh = -1;
};
static SlotRender s_slotRender[MAX_SLOTS];

// Model shader (reuse existing model.vert/frag)
static std::unique_ptr<Shader> s_modelShader;
static std::unique_ptr<Shader> s_shadowShader;
static std::unique_ptr<Shader> s_outlineShader;

static float s_time = 0.0f;

// Point lights collected from light-emitting world objects
struct CSPointLight {
  glm::vec3 position;
  glm::vec3 color;
  float range;
  int objectType;
};
static std::vector<CSPointLight> s_pointLights;
static constexpr int CS_MAX_POINT_LIGHTS = 64;

// Luminosity — near full daylight
static constexpr float CS_LUMINOSITY = 1.0f;

// Sun direction: high warm directional light for realistic top-down illumination
// Position is relative to scene — far above and slightly behind camera
static glm::vec3 s_sunLightPos;
static constexpr glm::vec3 CS_SUN_COLOR = glm::vec3(1.1f, 0.95f, 0.8f); // warm

// Light properties for object types (matches main.cpp GetLightProperties)
struct CSLightTemplate {
  glm::vec3 color;
  float range;
  float heightOffset;
};

static const CSLightTemplate *GetCSLightProps(int type) {
  static const CSLightTemplate fire = {{1.5f, 0.9f, 0.5f}, 800.0f, 150.0f};
  static const CSLightTemplate bonfire = {{1.5f, 0.75f, 0.3f}, 1000.0f, 100.0f};
  static const CSLightTemplate gate = {{1.5f, 0.9f, 0.5f}, 800.0f, 200.0f};
  static const CSLightTemplate bridge = {{1.2f, 0.7f, 0.4f}, 700.0f, 50.0f};
  static const CSLightTemplate streetLight = {{1.5f, 1.2f, 0.75f}, 800.0f, 250.0f};
  static const CSLightTemplate candle = {{1.2f, 0.7f, 0.3f}, 600.0f, 80.0f};
  static const CSLightTemplate lightFixture = {{1.2f, 0.85f, 0.5f}, 700.0f, 150.0f};
  switch (type) {
  case 50: case 51: return &fire;
  case 52: return &bonfire;
  case 55: return &gate;
  case 80: return &bridge;
  case 90: return &streetLight;
  case 130: case 131: case 132: return &lightFixture;
  case 150: return &candle;
  default: return nullptr;
  }
}

// ── Face portrait for character creation (NewFace BMDs from Data/Logo/) ──
static std::unique_ptr<BMDData> s_faceModels[4]; // indexed by ClassToIndex()
static std::vector<MeshBuffers> s_faceMeshes;     // current face meshes
static float s_faceAnimFrame = 0.0f;
static int s_faceLoadedClass = -1;

// Cached AABB of current face model (for auto-framing)
static glm::vec3 s_faceAABBMin(0.0f);
static glm::vec3 s_faceAABBMax(0.0f);

// FBO for rendering face portrait to texture
static GLuint s_faceFBO = 0;
static GLuint s_faceColorTex = 0;
static GLuint s_faceDepthRBO = 0;
static constexpr int FACE_TEX_W = 410;
static constexpr int FACE_TEX_H = 500;

// Per-class face rendering parameters (Z rotation for 3/4 view angle)
struct FaceRenderParams {
  float angleZ; // Z-axis rotation for 3/4 view
};
static constexpr FaceRenderParams FACE_PARAMS[4] = {
    {-12.0f},  // DW — slight left turn
    {-40.0f},  // DK — 3/4 left turn
    {  5.0f},  // ELF — slight right turn
    {-13.0f},  // MG — slight left turn
};

// Character slot positions (world coordinates) — customize these
struct SlotPos {
  float worldX, worldZ, facingDeg;
};
// Character slots: 5 positions on the road
// Camera at (24524, 520, 21331), yaw=156.7° pitch=-17.3°
// forward*720 from new camera ≈ same world position as before
static constexpr float SCENE_CX = 23863.0f;
static constexpr float SCENE_CZ = 21615.5f;
static constexpr float FWD_X = -0.919f;
static constexpr float FWD_Z = 0.395f;
static constexpr float RIGHT_X = 0.395f;
static constexpr float RIGHT_Z = 0.919f;
static constexpr float FACE_DEG = 203.3f;
// Spread 120 units apart along right axis, slight arc forward
static constexpr SlotPos SLOT_POSITIONS[MAX_SLOTS] = {
    {SCENE_CX + RIGHT_X * -240 + FWD_X * 30, SCENE_CZ + RIGHT_Z * -240 + FWD_Z * 30, FACE_DEG},
    {SCENE_CX + RIGHT_X * -120 + FWD_X * 8, SCENE_CZ + RIGHT_Z * -120 + FWD_Z * 8, FACE_DEG},
    {SCENE_CX, SCENE_CZ, FACE_DEG},
    {SCENE_CX + RIGHT_X * 120 + FWD_X * 8, SCENE_CZ + RIGHT_Z * 120 + FWD_Z * 8, FACE_DEG},
    {SCENE_CX + RIGHT_X * 240 + FWD_X * 30, SCENE_CZ + RIGHT_Z * 240 + FWD_Z * 30, FACE_DEG},
};

// Camera
static glm::vec3 s_camPos;
static glm::vec3 s_camTarget;
static glm::mat4 s_viewMatrix;
static glm::mat4 s_projMatrix;

static float s_camYaw = 0.0f;
static float s_camPitch = 0.0f;
static GLFWwindow *s_window = nullptr;

// ── Class info helpers ──

// Map classCode (0=DW, 16=DK, 32=ELF, 48=MG) to class parts index (0-3)
static int ClassToIndex(uint8_t classCode) {
  switch (classCode) {
  case CLASS_DW:  return 0;
  case CLASS_DK:  return 1;
  case CLASS_ELF: return 2;
  case CLASS_MG:  return 3;
  default:        return 1;
  }
}

// Class suffix for body part BMD files
static const char *ClassSuffix(int idx) {
  static const char *suffixes[] = {"Class01", "Class02", "Class03", "Class04"};
  return suffixes[idx];
}

// ── Character model loading (Player.bmd + body parts, same as HeroCharacter) ──

static void LoadPlayerModels() {
  std::string playerPath = s_ctx.dataPath + "/Player/";

  // Load shared skeleton
  s_playerSkeleton = BMDParser::Parse(playerPath + "player.bmd");
  if (!s_playerSkeleton) {
    printf("[CharSelect] Failed to load Player.bmd skeleton\n");
    return;
  }
  printf("[CharSelect] Player.bmd: %d bones, %d actions\n",
         (int)s_playerSkeleton->Bones.size(),
         (int)s_playerSkeleton->Actions.size());

  // Load body parts for each class
  const char *partPrefixes[] = {"Helm", "Armor", "Pant", "Glove", "Boot"};
  for (int ci = 0; ci < 4; ci++) {
    const char *suffix = ClassSuffix(ci);
    bool anyLoaded = false;
    for (int p = 0; p < PART_COUNT; p++) {
      char filename[64];
      snprintf(filename, sizeof(filename), "%s%s.bmd", partPrefixes[p], suffix);
      std::string fullPath = playerPath + filename;
      s_classParts[ci].bmd[p] = BMDParser::Parse(fullPath);
      if (s_classParts[ci].bmd[p])
        anyLoaded = true;
    }
    s_classParts[ci].loaded = anyLoaded;
    printf("[CharSelect] %s parts: %s\n", suffix, anyLoaded ? "OK" : "MISSING");
  }
}

// ── Face portrait BMD loading ──

static void LoadFaceModels() {
  std::string logoPath = s_ctx.dataPath + "/Logo/";
  const char *faceFiles[] = {"NewFace01.bmd", "NewFace02.bmd", "NewFace03.bmd",
                              "NewFace04.bmd"};
  for (int i = 0; i < 4; i++) {
    s_faceModels[i] = BMDParser::Parse(logoPath + faceFiles[i]);
    if (s_faceModels[i]) {
      printf("[CharSelect] Face model %s: %d bones, %d actions, %d meshes\n",
             faceFiles[i], (int)s_faceModels[i]->Bones.size(),
             (int)s_faceModels[i]->Actions.size(),
             (int)s_faceModels[i]->Meshes.size());
    } else {
      printf("[CharSelect] WARNING: Failed to load %s\n", faceFiles[i]);
    }
  }
}

static void SetupFaceFBO() {
  // Create FBO for face portrait rendering
  glGenFramebuffers(1, &s_faceFBO);
  glGenTextures(1, &s_faceColorTex);
  glGenRenderbuffers(1, &s_faceDepthRBO);

  glBindTexture(GL_TEXTURE_2D, s_faceColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FACE_TEX_W, FACE_TEX_H, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindRenderbuffer(GL_RENDERBUFFER, s_faceDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, FACE_TEX_W,
                        FACE_TEX_H);

  glBindFramebuffer(GL_FRAMEBUFFER, s_faceFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         s_faceColorTex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, s_faceDepthRBO);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    printf("[CharSelect] WARNING: Face FBO incomplete (0x%x)\n", status);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  printf("[CharSelect] Face FBO created (%dx%d)\n", FACE_TEX_W, FACE_TEX_H);
}

static void CleanupFaceFBO() {
  if (s_faceFBO) {
    glDeleteFramebuffers(1, &s_faceFBO);
    s_faceFBO = 0;
  }
  if (s_faceColorTex) {
    glDeleteTextures(1, &s_faceColorTex);
    s_faceColorTex = 0;
  }
  if (s_faceDepthRBO) {
    glDeleteRenderbuffers(1, &s_faceDepthRBO);
    s_faceDepthRBO = 0;
  }
}

// Rebuild face meshes for a given class index, compute AABB for auto-framing
static void RebuildFaceMeshes(int classIdx) {
  CleanupMeshBuffers(s_faceMeshes);
  s_faceMeshes.clear();
  s_faceLoadedClass = -1;
  s_faceAABBMin = glm::vec3(0.0f);
  s_faceAABBMax = glm::vec3(0.0f);

  if (classIdx < 0 || classIdx >= 4 || !s_faceModels[classIdx])
    return;

  auto *bmd = s_faceModels[classIdx].get();
  int action = (bmd->Actions.size() > 1) ? 1 : 0;
  auto bones = ComputeBoneMatrices(bmd, action, 0);
  std::string texDir = s_ctx.dataPath + "/Logo/";
  AABB aabb;

  for (auto &mesh : bmd->Meshes) {
    UploadMeshWithBones(mesh, texDir, bones, s_faceMeshes, aabb, true);
  }

  // Compute AABB across all skinned vertices for auto-framing
  s_faceAABBMin = glm::vec3(1e9f);
  s_faceAABBMax = glm::vec3(-1e9f);
  for (auto &mesh : bmd->Meshes) {
    for (int vi = 0; vi < mesh.NumVertices; vi++) {
      auto &v = mesh.Vertices[vi];
      glm::vec3 pos = v.Position;
      int boneIdx = v.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size())
        pos = MuMath::TransformPoint(
            (const float(*)[4])bones[boneIdx].data(), pos);
      s_faceAABBMin = glm::min(s_faceAABBMin, pos);
      s_faceAABBMax = glm::max(s_faceAABBMax, pos);
    }
  }

  s_faceLoadedClass = classIdx;
  s_faceAnimFrame = 0.0f;
  printf("[CharSelect] Face AABB: min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f)\n",
         s_faceAABBMin.x, s_faceAABBMin.y, s_faceAABBMin.z,
         s_faceAABBMax.x, s_faceAABBMax.y, s_faceAABBMax.z);
}

// Re-skin face meshes for animation
static void ReskinFace() {
  if (s_faceLoadedClass < 0 || !s_faceModels[s_faceLoadedClass])
    return;

  auto *bmd = s_faceModels[s_faceLoadedClass].get();
  int action = (bmd->Actions.size() > 1) ? 1 : 0;
  auto bones = ComputeBoneMatricesInterpolated(bmd, action, s_faceAnimFrame);

  for (int mi = 0; mi < (int)s_faceMeshes.size() && mi < (int)bmd->Meshes.size();
       ++mi) {
    RetransformMeshWithBones(bmd->Meshes[mi], bones, s_faceMeshes[mi]);
  }
}

// Render face model to FBO with AABB-based auto-framing
// Model is anchored to the bottom of the FBO (feet/bottom of bust at bottom edge)
static void RenderFaceToFBO() {
  if (s_faceLoadedClass < 0 || s_faceMeshes.empty() || !s_modelShader ||
      !s_faceFBO)
    return;

  GLint prevViewport[4];
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  GLint prevFBO;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

  glBindFramebuffer(GL_FRAMEBUFFER, s_faceFBO);
  glViewport(0, 0, FACE_TEX_W, FACE_TEX_H);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  auto &fp = FACE_PARAMS[s_faceLoadedClass];

  // Auto-frame: compute camera from AABB
  // In BMD space: X/Y are horizontal, Z is up
  // Apply the Z rotation to the AABB center for correct framing
  float angleZ = glm::radians(fp.angleZ);
  glm::vec3 center = (s_faceAABBMin + s_faceAABBMax) * 0.5f;
  float height = s_faceAABBMax.z - s_faceAABBMin.z;
  float width = glm::length(glm::vec2(s_faceAABBMax.x - s_faceAABBMin.x,
                                       s_faceAABBMax.y - s_faceAABBMin.y));

  // Anchor to bottom: shift target up so bottom of model aligns with bottom of FBO
  // The target Z is shifted up by half the height (center) + a small margin
  float aspect = (float)FACE_TEX_W / (float)FACE_TEX_H;
  float fovDeg = 10.0f;
  float fov = glm::radians(fovDeg);

  // Compute camera distance to fit the model width (tighter framing)
  float margin = 1.05f;
  float fitHeight = height * margin;
  float fitWidth = width * margin;
  float distH = (fitHeight * 0.5f) / tanf(fov * 0.5f);
  float distW = (fitWidth * 0.5f) / (tanf(fov * 0.5f) * aspect);
  float camDist = std::max(distH, distW);

  // Push model DOWN in FBO: camera targets above model center so the
  // bust renders lower. Combined with UV bottom crop in the display,
  // the arm stumps get hidden (overflow:hidden effect).
  float modelCenter = (s_faceAABBMin.z + s_faceAABBMax.z) * 0.5f;
  float pushDown = height * 0.15f;
  float targetZ = modelCenter + pushDown;

  glm::vec3 camP(0.0f, -camDist, targetZ);
  glm::vec3 target(0.0f, 0.0f, targetZ);
  glm::mat4 faceView = glm::lookAt(camP, target, glm::vec3(0.0f, 0.0f, 1.0f));
  glm::mat4 faceProj = glm::perspective(fov, aspect, 1.0f, camDist * 3.0f);

  // Model matrix: per-class Z rotation
  glm::mat4 model(1.0f);
  model = glm::rotate(model, angleZ, glm::vec3(0, 0, 1));

  s_modelShader->use();
  s_modelShader->setMat4("view", faceView);
  s_modelShader->setMat4("projection", faceProj);
  s_modelShader->setMat4("model", model);
  s_modelShader->setFloat("objectAlpha", 1.0f);
  s_modelShader->setVec3("lightPos", glm::vec3(20.0f, -300.0f, 100.0f));
  s_modelShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
  s_modelShader->setVec3("viewPos", camP);
  s_modelShader->setVec3("terrainLight", glm::vec3(1.0f));
  s_modelShader->setFloat("blendMeshLight", 1.0f);
  s_modelShader->setFloat("luminosity", 1.0f);
  s_modelShader->setBool("useFog", false);
  s_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
  s_modelShader->setFloat("outlineOffset", 0.0f);
  s_modelShader->setInt("numPointLights", 0);

  glDisable(GL_CULL_FACE);
  for (auto &mb : s_faceMeshes) {
    if (mb.indexCount == 0)
      continue;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mb.texture);
    s_modelShader->setInt("texture_diffuse", 0);
    glBindVertexArray(mb.vao);
    glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
  }
  glEnable(GL_CULL_FACE);

  glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

static std::vector<ShadowMesh> CreateShadowMeshes(const BMDData *bmd) {
  std::vector<ShadowMesh> meshes;
  if (!bmd)
    return meshes;
  for (auto &mesh : bmd->Meshes) {
    ShadowMesh sm;
    // Count vertices: 3 per tri, 6 per quad
    int count = 0;
    for (int i = 0; i < mesh.NumTriangles; i++)
      count += (mesh.Triangles[i].Polygon == 4) ? 6 : 3;
    sm.vertexCount = count;
    if (count == 0) {
      meshes.push_back(sm);
      continue;
    }
    glGenVertexArrays(1, &sm.vao);
    glGenBuffers(1, &sm.vbo);
    glBindVertexArray(sm.vao);
    glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(glm::vec3), nullptr,
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    meshes.push_back(sm);
  }
  return meshes;
}

static void InitSlotMeshes(int slot) {
  if (!s_slots[slot].occupied || !s_playerSkeleton)
    return;

  int ci = ClassToIndex(s_slots[slot].classCode);
  if (!s_classParts[ci].loaded)
    return;

  static constexpr int IDLE_ACTION = 1; // ACTION_STOP_MALE
  auto bones = ComputeBoneMatrices(s_playerSkeleton.get(), IDLE_ACTION, 0.0f);
  std::string texDirPlayer = s_ctx.dataPath + "/Player/";
  std::string texDirItem = s_ctx.dataPath + "/Item/";

  // --- Body parts (with equipment override) ---
  // equip[2]=helm, equip[3]=armor, equip[4]=pants, equip[5]=gloves, equip[6]=boots
  for (int p = 0; p < PART_COUNT; p++) {
    BMDData *partBmd = s_classParts[ci].bmd[p].get(); // default

    // Check if this slot has equipment override
    auto &eq = s_slots[slot].equip[2 + p]; // equip indices 2..6 map to parts 0..4
    if (eq.category != 0xFF) {
      std::string modelFile =
          ItemDatabase::GetBodyPartModelFile(eq.category, eq.itemIndex);
      if (!modelFile.empty()) {
        auto overrideBmd = BMDParser::Parse(texDirPlayer + modelFile);
        if (overrideBmd) {
          s_slotRender[slot].partOverrideBmd[p] = std::move(overrideBmd);
          partBmd = s_slotRender[slot].partOverrideBmd[p].get();
        }
      }
    }

    if (!partBmd)
      continue;
    AABB aabb;
    for (auto &mesh : partBmd->Meshes) {
      UploadMeshWithBones(mesh, texDirPlayer, bones,
                          s_slotRender[slot].meshes[p], aabb, true);
    }
    s_slotRender[slot].shadowMeshes[p] = CreateShadowMeshes(partBmd);
  }

  // --- Base head for accessory helms (Pad, Bronze, Elf helms show the face) ---
  // Main 5.2: certain helms are "open" and the default class head renders underneath
  s_slotRender[slot].showBaseHead = false;
  if (s_slotRender[slot].partOverrideBmd[0]) {
    // Check if the override helm is an accessory type
    auto &helmEq = s_slots[slot].equip[2]; // equip[2] = helm
    std::string helmFile =
        ItemDatabase::GetBodyPartModelFile(helmEq.category, helmEq.itemIndex);
    std::string lower = helmFile;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    bool isAccessory = (lower.find("helmmale01") != std::string::npos ||
                        lower.find("helmmale03") != std::string::npos ||
                        lower.find("helmelf01") != std::string::npos ||
                        lower.find("helmelf02") != std::string::npos ||
                        lower.find("helmelf03") != std::string::npos ||
                        lower.find("helmelf04") != std::string::npos);
    if (isAccessory && s_classParts[ci].bmd[0]) {
      // Load class default head (HelmClassXX.bmd) underneath
      AABB headAABB;
      for (auto &mesh : s_classParts[ci].bmd[0]->Meshes) {
        UploadMeshWithBones(mesh, texDirPlayer, bones,
                            s_slotRender[slot].baseHeadMeshes, headAABB, true);
      }
      s_slotRender[slot].baseHeadShadowMeshes =
          CreateShadowMeshes(s_classParts[ci].bmd[0].get());
      s_slotRender[slot].showBaseHead = true;
      printf("[CharSelect] Slot %d: base head loaded under accessory helm\n", slot);
    }
  }

  // --- Right hand weapon (equip[0]) ---
  auto &rh = s_slots[slot].equip[0];
  if (rh.category != 0xFF) {
    int16_t defIdx = (int16_t)rh.category * 32 + (int16_t)rh.itemIndex;
    auto &defs = ItemDatabase::GetItemDefs();
    auto it = defs.find(defIdx);
    if (it != defs.end() && !it->second.modelFile.empty()) {
      auto bmd = BMDParser::Parse(texDirItem + it->second.modelFile);
      if (bmd) {
        auto localBones = ComputeBoneMatrices(bmd.get());
        if (localBones.empty()) {
          BoneWorldMatrix identity{};
          identity[0] = {1, 0, 0, 0};
          identity[1] = {0, 1, 0, 0};
          identity[2] = {0, 0, 1, 0};
          localBones = {identity};
        }
        AABB weaponAABB;
        for (auto &mesh : bmd->Meshes) {
          UploadMeshWithBones(mesh, texDirItem, localBones,
                              s_slotRender[slot].weaponMeshes, weaponAABB, true);
        }
        s_slotRender[slot].weaponShadowMeshes = CreateShadowMeshes(bmd.get());
        s_slotRender[slot].weaponLocalBones = std::move(localBones);
        s_slotRender[slot].weaponBmd = std::move(bmd);
        s_slotRender[slot].weaponBlendMesh =
            ItemModelManager::GetItemBlendMesh(rh.category, rh.itemIndex);
        printf("[CharSelect] Slot %d: weapon loaded (%d meshes)\n", slot,
               (int)s_slotRender[slot].weaponMeshes.size());
      }
    }
  }

  // --- Left hand / shield (equip[1]) ---
  auto &lh = s_slots[slot].equip[1];
  if (lh.category != 0xFF) {
    int16_t defIdx = (int16_t)lh.category * 32 + (int16_t)lh.itemIndex;
    auto &defs = ItemDatabase::GetItemDefs();
    auto it = defs.find(defIdx);
    if (it != defs.end() && !it->second.modelFile.empty()) {
      auto bmd = BMDParser::Parse(texDirItem + it->second.modelFile);
      if (bmd) {
        auto localBones = ComputeBoneMatrices(bmd.get());
        if (localBones.empty()) {
          BoneWorldMatrix identity{};
          identity[0] = {1, 0, 0, 0};
          identity[1] = {0, 1, 0, 0};
          identity[2] = {0, 0, 1, 0};
          localBones = {identity};
        }
        AABB shieldAABB;
        for (auto &mesh : bmd->Meshes) {
          UploadMeshWithBones(mesh, texDirItem, localBones,
                              s_slotRender[slot].shieldMeshes, shieldAABB, true);
        }
        s_slotRender[slot].shieldShadowMeshes = CreateShadowMeshes(bmd.get());
        s_slotRender[slot].shieldLocalBones = std::move(localBones);
        s_slotRender[slot].shieldBmd = std::move(bmd);
        printf("[CharSelect] Slot %d: shield loaded (%d meshes)\n", slot,
               (int)s_slotRender[slot].shieldMeshes.size());
      }
    }
  }

  s_slotRender[slot].animFrame = 0.0f;
}

// Re-skin weapon or shield vertices onto the back bone.
// Pattern matches HeroCharacter safe-zone rendering (lines 650-831).
static void ReskinAttachedItem(const std::vector<BoneWorldMatrix> &charBones,
                               const BMDData *bmd,
                               const std::vector<BoneWorldMatrix> &localBones,
                               std::vector<MeshBuffers> &meshBuffers,
                               const glm::vec3 &rotDeg,
                               const glm::vec3 &offset) {
  static constexpr int BONE_BACK = 47;
  if (!bmd || meshBuffers.empty() || BONE_BACK >= (int)charBones.size())
    return;

  BoneWorldMatrix offsetMat =
      MuMath::BuildWeaponOffsetMatrix(rotDeg, offset);
  BoneWorldMatrix parentMat;
  MuMath::ConcatTransforms((const float(*)[4])charBones[BONE_BACK].data(),
                           (const float(*)[4])offsetMat.data(),
                           (float(*)[4])parentMat.data());

  std::vector<BoneWorldMatrix> finalBones(localBones.size());
  for (int bi = 0; bi < (int)localBones.size(); ++bi) {
    MuMath::ConcatTransforms((const float(*)[4])parentMat.data(),
                             (const float(*)[4])localBones[bi].data(),
                             (float(*)[4])finalBones[bi].data());
  }

  for (int mi = 0; mi < (int)meshBuffers.size() &&
                   mi < (int)bmd->Meshes.size();
       ++mi) {
    auto &mesh = bmd->Meshes[mi];
    auto &mb = meshBuffers[mi];
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
        if (boneIdx >= 0 && boneIdx < (int)finalBones.size()) {
          vv.pos = MuMath::TransformPoint(
              (const float(*)[4])finalBones[boneIdx].data(), srcPos);
          vv.normal = MuMath::RotateVector(
              (const float(*)[4])finalBones[boneIdx].data(), srcNorm);
        } else {
          vv.pos = MuMath::TransformPoint(
              (const float(*)[4])parentMat.data(), srcPos);
          vv.normal = MuMath::RotateVector(
              (const float(*)[4])parentMat.data(), srcNorm);
        }
        vv.tex = (tri.TexCoordIndex[v] < mesh.NumTexCoords)
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
}

static void ReskinSlot(int slot) {
  if (!s_slots[slot].occupied || !s_playerSkeleton)
    return;

  int ci = ClassToIndex(s_slots[slot].classCode);
  if (!s_classParts[ci].loaded)
    return;

  // Idle action = 1 (ACTION_STOP_MALE) — interpolated for smooth sub-frame blending
  static constexpr int IDLE_ACTION = 1;
  auto bones = ComputeBoneMatricesInterpolated(s_playerSkeleton.get(), IDLE_ACTION,
                                               s_slotRender[slot].animFrame);

  // Re-skin body parts (use override BMD if present)
  for (int p = 0; p < PART_COUNT; p++) {
    BMDData *bmd = s_slotRender[slot].partOverrideBmd[p]
                       ? s_slotRender[slot].partOverrideBmd[p].get()
                       : s_classParts[ci].bmd[p].get();
    if (!bmd)
      continue;
    for (int mi = 0; mi < (int)s_slotRender[slot].meshes[p].size() &&
                     mi < (int)bmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(bmd->Meshes[mi], bones,
                               s_slotRender[slot].meshes[p][mi]);
    }
  }

  // Re-skin base head (class default head under accessory helm)
  if (s_slotRender[slot].showBaseHead && s_classParts[ci].bmd[0]) {
    BMDData *headBmd = s_classParts[ci].bmd[0].get();
    for (int mi = 0; mi < (int)s_slotRender[slot].baseHeadMeshes.size() &&
                     mi < (int)headBmd->Meshes.size();
         ++mi) {
      RetransformMeshWithBones(headBmd->Meshes[mi], bones,
                               s_slotRender[slot].baseHeadMeshes[mi]);
    }
  }

  // Re-skin weapon on back: rotation(70,0,90) + offset(-20,5,40)
  ReskinAttachedItem(bones, s_slotRender[slot].weaponBmd.get(),
                     s_slotRender[slot].weaponLocalBones,
                     s_slotRender[slot].weaponMeshes,
                     glm::vec3(70.f, 0.f, 90.f), glm::vec3(-20.f, 5.f, 40.f));

  // Re-skin shield on back: rotation(70,0,90) + offset(-10,0,0)
  ReskinAttachedItem(bones, s_slotRender[slot].shieldBmd.get(),
                     s_slotRender[slot].shieldLocalBones,
                     s_slotRender[slot].shieldMeshes,
                     glm::vec3(70.f, 0.f, 90.f), glm::vec3(-10.f, 0.f, 0.f));
}

// Sample terrain lightmap at world position (same as HeroCharacter)
static glm::vec3 SampleTerrainLight(float worldX, float worldZ) {
  const int SIZE = 256;
  if (s_terrainData.lightmap.size() < (size_t)(SIZE * SIZE))
    return glm::vec3(1.0f);

  float gz = worldX / 100.0f;
  float gx = worldZ / 100.0f;
  int xi = (int)gx, zi = (int)gz;
  if (xi < 0 || zi < 0 || xi > SIZE - 2 || zi > SIZE - 2)
    return glm::vec3(0.5f);

  float xd = gx - (float)xi, zd = gz - (float)zi;
  const glm::vec3 &c00 = s_terrainData.lightmap[zi * SIZE + xi];
  const glm::vec3 &c10 = s_terrainData.lightmap[zi * SIZE + (xi + 1)];
  const glm::vec3 &c01 = s_terrainData.lightmap[(zi + 1) * SIZE + xi];
  const glm::vec3 &c11 = s_terrainData.lightmap[(zi + 1) * SIZE + (xi + 1)];
  glm::vec3 left = c00 + (c01 - c00) * zd;
  glm::vec3 right = c10 + (c11 - c10) * zd;
  return left + (right - left) * xd;
}

// ── Public interface ──

void Init(const Context &ctx) {
  s_ctx = ctx;
  s_initialized = true;
  s_selectedSlot = -1;
  s_createOpen = false;
  s_deleteConfirm = false;
  memset(s_createName, 0, sizeof(s_createName));
  memset(s_statusMsg, 0, sizeof(s_statusMsg));
  s_time = 0.0f;

  // Load Lorencia (World1) terrain
  printf("[CharSelect] Loading Lorencia terrain\n");

  s_terrain.Init();
  s_terrainData = TerrainParser::LoadWorld(1, s_ctx.dataPath);
  if (!s_terrainData.heightmap.empty()) {
    s_terrain.Load(s_terrainData, 1, s_ctx.dataPath);
    // Dark smoke fog — hides distant fields evenly
    s_terrain.SetFogColor(glm::vec3(0.08f, 0.07f, 0.06f));
    s_terrain.SetFogRange(2500.0f, 5500.0f);
    s_terrainLoaded = true;
    printf("[CharSelect] Lorencia terrain loaded\n");
  } else {
    printf("[CharSelect] WARNING: Failed to load Lorencia terrain\n");
  }

  // Load Lorencia objects — only those near the visible scene area
  if (s_terrainLoaded && !s_terrainData.objects.empty()) {
    // Filter objects within radius of camera to avoid loading/rendering
    // distant invisible objects
    const float cullRadius = 3000.0f;
    glm::vec3 camXZ(s_camPos.x, 0.0f, s_camPos.z);
    std::vector<ObjectData> visibleObjects;
    for (auto &obj : s_terrainData.objects) {
      float dx = obj.position.x - camXZ.x;
      float dz = obj.position.z - camXZ.z;
      if (dx * dx + dz * dz < cullRadius * cullRadius)
        visibleObjects.push_back(obj);
    }

    s_objectRenderer.Init();
    s_objectRenderer.SetTerrainLightmap(s_terrainData.lightmap);
    s_objectRenderer.SetTerrainMapping(&s_terrainData.mapping);
    s_objectRenderer.SetTerrainHeightmap(s_terrainData.heightmap);
    s_objectRenderer.SetFogEnabled(true);
    s_objectRenderer.SetFogColor(glm::vec3(0.08f, 0.07f, 0.06f));
    s_objectRenderer.SetFogRange(2500.0f, 5500.0f);
    std::string objectDir = s_ctx.dataPath + "/Object1";
    s_objectRenderer.LoadObjects(visibleObjects, objectDir);
    s_objectRenderer.SetLuminosity(CS_LUMINOSITY);
    printf("[CharSelect] Lorencia objects loaded: %d/%d instances (culled to "
           "%.0f radius), %d models\n",
           s_objectRenderer.GetInstanceCount(),
           (int)s_terrainData.objects.size(), cullRadius,
           s_objectRenderer.GetModelCount());

    // Collect point lights from light-emitting world objects (same as main.cpp)
    s_pointLights.clear();
    for (auto &inst : s_objectRenderer.GetInstances()) {
      auto *props = GetCSLightProps(inst.type);
      if (!props)
        continue;
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      CSPointLight pl;
      pl.position = worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f);
      pl.color = props->color;
      pl.range = props->range;
      pl.objectType = inst.type;
      s_pointLights.push_back(pl);
    }
    if ((int)s_pointLights.size() > CS_MAX_POINT_LIGHTS)
      s_pointLights.resize(CS_MAX_POINT_LIGHTS);

    // Pass collected lights to object renderer
    {
      std::vector<glm::vec3> plPos, plCol;
      std::vector<float> plRange;
      for (auto &pl : s_pointLights) {
        plPos.push_back(pl.position);
        plCol.push_back(pl.color);
        plRange.push_back(pl.range);
      }
      s_objectRenderer.SetPointLights(plPos, plCol, plRange);
    }
    printf("[CharSelect] Collected %d point lights from world objects\n",
           (int)s_pointLights.size());
  }

  // Load grass billboards
  if (s_terrainLoaded) {
    s_grassRenderer.Init();
    s_grassRenderer.Load(s_terrainData, 1, s_ctx.dataPath);
    s_grassRenderer.SetFogColor(glm::vec3(0.08f, 0.07f, 0.06f));
    s_grassRenderer.SetFogRange(2500.0f, 5500.0f);
    printf("[CharSelect] Grass loaded\n");
  }

  // Set luminosity on terrain for moody atmosphere
  if (s_terrainLoaded) {
    s_terrain.SetLuminosity(CS_LUMINOSITY);

    // Pass point lights to terrain for CPU-side lightmap illumination
    std::vector<glm::vec3> plPos, plCol;
    std::vector<float> plRange;
    std::vector<int> plTypes;
    for (auto &pl : s_pointLights) {
      plPos.push_back(pl.position);
      plCol.push_back(pl.color);
      plRange.push_back(pl.range);
      plTypes.push_back(pl.objectType);
    }
    s_terrain.SetPointLights(plPos, plCol, plRange, plTypes);
  }

  // Camera: locked position and angle from exploration
  s_camPos = glm::vec3(24524.4f, 520.3f, 21331.1f);
  s_camYaw = 156.7f;
  s_camPitch = -17.3f;

  // Compute target from yaw/pitch
  float yR = glm::radians(s_camYaw), pR = glm::radians(s_camPitch);
  glm::vec3 fwd(cosf(pR) * cosf(yR), sinf(pR), cosf(pR) * sinf(yR));
  s_camTarget = s_camPos + fwd * 1000.0f;
  s_window = ctx.window;

  // Sun position: high above and slightly behind/left of the scene center
  // Creates a warm top-down directional light that adds realistic sun effect
  s_sunLightPos = glm::vec3(SCENE_CX + 2000.0f, 3000.0f, SCENE_CZ - 1000.0f);

  s_viewMatrix =
      glm::lookAt(s_camPos, s_camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  printf("[CharSelect] Camera: pos=(%.1f, %.1f, %.1f) yaw=%.1f pitch=%.1f\n",
         s_camPos.x, s_camPos.y, s_camPos.z, s_camYaw, s_camPitch);

  // Load model shader (check shaders/ first, fall back to ../shaders/)
  {
    std::ifstream shaderTest("shaders/model.vert");
    std::string sp = shaderTest.good() ? "shaders/" : "../shaders/";
    try {
      s_modelShader = std::make_unique<Shader>(
          (sp + "model.vert").c_str(), (sp + "model.frag").c_str());
      printf("[CharSelect] Model shader loaded\n");
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load model shader\n");
    }
    try {
      s_shadowShader = std::make_unique<Shader>(
          (sp + "shadow.vert").c_str(), (sp + "shadow.frag").c_str());
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load shadow shader\n");
    }
    try {
      s_outlineShader = std::make_unique<Shader>(
          (sp + "outline.vert").c_str(), (sp + "outline.frag").c_str());
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load outline shader\n");
    }
  }

  // Load Player.bmd skeleton + class body parts (same as main game)
  LoadPlayerModels();

  // Load NewFace BMD models for character creation portrait
  LoadFaceModels();
  SetupFaceFBO();

  printf("[CharSelect] Initialized\n");
}

void Shutdown() {
  s_grassRenderer.Cleanup();
  s_objectRenderer.Cleanup();
  for (int i = 0; i < MAX_SLOTS; i++) {
    for (int p = 0; p < PART_COUNT; p++) {
      CleanupMeshBuffers(s_slotRender[i].meshes[p]);
      for (auto &sm : s_slotRender[i].shadowMeshes[p]) {
        if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
        if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
      }
      s_slotRender[i].shadowMeshes[p].clear();
      s_slotRender[i].partOverrideBmd[p].reset();
    }
    // Weapon cleanup
    CleanupMeshBuffers(s_slotRender[i].weaponMeshes);
    for (auto &sm : s_slotRender[i].weaponShadowMeshes) {
      if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
    }
    s_slotRender[i].weaponShadowMeshes.clear();
    s_slotRender[i].weaponLocalBones.clear();
    s_slotRender[i].weaponBmd.reset();
    // Base head cleanup
    CleanupMeshBuffers(s_slotRender[i].baseHeadMeshes);
    for (auto &sm : s_slotRender[i].baseHeadShadowMeshes) {
      if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
    }
    s_slotRender[i].baseHeadShadowMeshes.clear();
    s_slotRender[i].showBaseHead = false;
    // Shield cleanup
    CleanupMeshBuffers(s_slotRender[i].shieldMeshes);
    for (auto &sm : s_slotRender[i].shieldShadowMeshes) {
      if (sm.vao) glDeleteVertexArrays(1, &sm.vao);
      if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
    }
    s_slotRender[i].shieldShadowMeshes.clear();
    s_slotRender[i].shieldLocalBones.clear();
    s_slotRender[i].shieldBmd.reset();
  }
  s_playerSkeleton.reset();
  for (int i = 0; i < 4; i++)
    for (int p = 0; p < PART_COUNT; p++)
      s_classParts[i].bmd[p].reset();
  s_modelShader.reset();
  s_shadowShader.reset();
  s_outlineShader.reset();

  // Face portrait cleanup
  CleanupMeshBuffers(s_faceMeshes);
  s_faceMeshes.clear();
  s_faceLoadedClass = -1;
  CleanupFaceFBO();
  for (int i = 0; i < 4; i++)
    s_faceModels[i].reset();

  s_terrainLoaded = false;
  s_initialized = false;
  printf("[CharSelect] Shutdown\n");
}

void SetCharacterList(const CharSlot *slots, int count) {
  // Clear all slots
  for (int i = 0; i < MAX_SLOTS; i++) {
    s_slots[i] = {};
    for (int p = 0; p < PART_COUNT; p++) {
      CleanupMeshBuffers(s_slotRender[i].meshes[p]);
      s_slotRender[i].partOverrideBmd[p].reset();
    }
    CleanupMeshBuffers(s_slotRender[i].weaponMeshes);
    s_slotRender[i].weaponBmd.reset();
    s_slotRender[i].weaponLocalBones.clear();
    CleanupMeshBuffers(s_slotRender[i].baseHeadMeshes);
    s_slotRender[i].baseHeadShadowMeshes.clear();
    s_slotRender[i].showBaseHead = false;
    CleanupMeshBuffers(s_slotRender[i].shieldMeshes);
    s_slotRender[i].shieldBmd.reset();
    s_slotRender[i].shieldLocalBones.clear();
  }
  s_slotCount = std::min(count, MAX_SLOTS);

  for (int i = 0; i < count && i < MAX_SLOTS; i++)
    s_slots[i] = slots[i];

  // Auto-select first occupied slot
  s_selectedSlot = -1;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_slots[i].occupied) {
      s_selectedSlot = i;
      break;
    }
  }

  // Build character meshes for occupied slots with staggered animation
  const float animOffsets[MAX_SLOTS] = {0.0f, 7.3f, 14.1f, 4.8f, 11.6f};
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (s_slots[i].occupied) {
      s_slotRender[i].animFrame = animOffsets[i];
      InitSlotMeshes(i);
    }
  }

  // Auto-open create window if no characters exist
  bool anyOccupied = false;
  for (int i = 0; i < MAX_SLOTS; i++)
    if (s_slots[i].occupied) { anyOccupied = true; break; }
  if (!anyOccupied) {
    s_createOpen = true;
    memset(s_createName, 0, sizeof(s_createName));
    s_createClass = CLASS_DK;
    RebuildFaceMeshes(ClassToIndex(CLASS_DK));
  }

  printf("[CharSelect] Received %d characters\n", count);
}

void OnCreateResult(uint8_t result, const char *name, uint8_t slot,
                    uint8_t classCode) {
  if (result == 1) {
    snprintf(s_statusMsg, sizeof(s_statusMsg), "Character '%s' created!", name);
    s_createOpen = false;
    memset(s_createName, 0, sizeof(s_createName));
  } else if (result == 2) {
    snprintf(s_statusMsg, sizeof(s_statusMsg), "Name '%s' already taken", name);
  } else {
    snprintf(s_statusMsg, sizeof(s_statusMsg), "Character creation failed");
  }
  s_statusTimer = 3.0f;
}

void OnDeleteResult(uint8_t result) {
  if (result == 1) {
    snprintf(s_statusMsg, sizeof(s_statusMsg), "Character deleted");
    s_selectedSlot = -1;
  } else {
    snprintf(s_statusMsg, sizeof(s_statusMsg), "Delete failed");
  }
  s_deleteConfirm = false;
  s_statusTimer = 3.0f;
}

void Update(float dt) {
  s_time += dt;

  if (s_statusTimer > 0.0f)
    s_statusTimer -= dt;

  // Animate character models (idle animation using Player.bmd, action 1)
  if (s_playerSkeleton && s_playerSkeleton->Actions.size() > 1) {
    float maxFrame = (float)s_playerSkeleton->Actions[1].NumAnimationKeys;
    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied)
        continue;
      s_slotRender[i].animFrame += dt * 5.0f; // Slow, smooth idle for char select
      if (s_slotRender[i].animFrame >= maxFrame)
        s_slotRender[i].animFrame -= maxFrame;
      ReskinSlot(i);
    }
  }

  // Animate face portrait when create window is open
  if (s_createOpen && s_faceLoadedClass >= 0 &&
      s_faceModels[s_faceLoadedClass]) {
    auto *bmd = s_faceModels[s_faceLoadedClass].get();
    int action = (bmd->Actions.size() > 1) ? 1 : 0;
    float maxFrame = (float)bmd->Actions[action].NumAnimationKeys;
    float playSpeed = 0.3f; // Main 5.2: PlaySpeed=0.3 for face models
    s_faceAnimFrame += dt * playSpeed * 25.0f; // tick-based: 25fps * playSpeed
    if (s_faceAnimFrame >= maxFrame)
      s_faceAnimFrame -= maxFrame;
    ReskinFace();
  }
}

// Project world position to screen coordinates
static glm::vec2 ProjectToScreen(const glm::vec3 &worldPos, int winW,
                                 int winH) {
  glm::vec4 clip =
      s_projMatrix * s_viewMatrix * glm::vec4(worldPos, 1.0f);
  if (clip.w <= 0)
    return glm::vec2(-1, -1);
  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  return glm::vec2((ndc.x * 0.5f + 0.5f) * winW,
                   (1.0f - (ndc.y * 0.5f + 0.5f)) * winH);
}

void Render(int windowWidth, int windowHeight) {
  if (!s_initialized)
    return;

  // Setup projection
  float aspect = (float)windowWidth / (float)std::max(windowHeight, 1);
  s_projMatrix = glm::perspective(glm::radians(35.0f), aspect, 10.0f, 50000.0f);

  // Fixed camera: lookAt from camera position to character slots center
  glm::vec3 camPos = s_camPos;
  s_viewMatrix = glm::lookAt(camPos, s_camTarget, glm::vec3(0.0f, 1.0f, 0.0f));

  // Clear
  glClearColor(0.08f, 0.07f, 0.06f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  // Render terrain
  if (s_terrainLoaded) {
    s_terrain.Render(s_viewMatrix, s_projMatrix, s_time, camPos);
  }

  // Render world objects (ruins, columns, trees)
  if (s_objectRenderer.GetInstanceCount() > 0) {
    s_objectRenderer.Render(s_viewMatrix, s_projMatrix, camPos, s_time);
  }

  // Render grass billboards
  s_grassRenderer.Render(s_viewMatrix, s_projMatrix, s_time, camPos);

  // Render drop shadows BEFORE characters so characters draw on top
  if (s_shadowShader && s_playerSkeleton) {
    s_shadowShader->use();
    s_shadowShader->setMat4("projection", s_projMatrix);
    s_shadowShader->setMat4("view", s_viewMatrix);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST); // Shadows always on ground, never clipped by character
    glDisable(GL_CULL_FACE);

    // Stencil: draw each shadow pixel exactly once (prevents overlap darkening).
    // Body + weapon + shield all share the same stencil → unified single shadow.
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_INCR, GL_INCR);

    // Shadow projection constants (from ZzzBMD.cpp RenderBodyShadow)
    const float sx = 2000.0f;
    const float sy = 4000.0f;

    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied)
        continue;

      int ci = ClassToIndex(s_slots[i].classCode);
      if (!s_classParts[ci].loaded)
        continue;

      // Clear stencil per character so shadows don't block each other
      glClear(GL_STENCIL_BUFFER_BIT);

      auto &sp = SLOT_POSITIONS[i];
      float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ)
                                    : 0.0f;

      // Shadow model: no facing rotation (facing baked into vertices)
      glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                        glm::vec3(sp.worldX, slotY, sp.worldZ));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      s_shadowShader->setMat4("model", model);

      float facing = sp.facingDeg * (3.14159f / 180.0f);
      float cosF = cosf(facing);
      float sinF = sinf(facing);

      // Compute bones for current animation frame (interpolated for smooth shadows)
      auto bones = ComputeBoneMatricesInterpolated(s_playerSkeleton.get(), 1,
                                                    s_slotRender[i].animFrame);

      // Lambda: project shadow verts for a BMD using given bone matrices
      auto projectShadow = [&](const BMDData *bmd,
                                std::vector<ShadowMesh> &smVec,
                                const std::vector<BoneWorldMatrix> &boneSet) {
        for (int mi = 0;
             mi < (int)bmd->Meshes.size() && mi < (int)smVec.size(); ++mi) {
          auto &sm = smVec[mi];
          if (sm.vertexCount == 0 || sm.vao == 0)
            continue;

          auto &mesh = bmd->Meshes[mi];
          std::vector<glm::vec3> shadowVerts;
          shadowVerts.reserve(sm.vertexCount);

          for (int t = 0; t < mesh.NumTriangles; ++t) {
            auto &tri = mesh.Triangles[t];
            int steps = (tri.Polygon == 3) ? 3 : 4;
            for (int v = 0; v < 3; ++v) {
              auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
              glm::vec3 pos = srcVert.Position;
              int boneIdx = srcVert.Node;
              if (boneIdx >= 0 && boneIdx < (int)boneSet.size())
                pos = MuMath::TransformPoint(
                    (const float(*)[4])boneSet[boneIdx].data(), pos);

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
              int qi[3] = {0, 2, 3};
              for (int v : qi) {
                auto &srcVert = mesh.Vertices[tri.VertexIndex[v]];
                glm::vec3 pos = srcVert.Position;
                int boneIdx = srcVert.Node;
                if (boneIdx >= 0 && boneIdx < (int)boneSet.size())
                  pos = MuMath::TransformPoint(
                      (const float(*)[4])boneSet[boneIdx].data(), pos);

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

      // Body part shadows (use override BMD if present)
      for (int p = 0; p < PART_COUNT; p++) {
        BMDData *bmd = s_slotRender[i].partOverrideBmd[p]
                           ? s_slotRender[i].partOverrideBmd[p].get()
                           : s_classParts[ci].bmd[p].get();
        if (!bmd)
          continue;
        projectShadow(bmd, s_slotRender[i].shadowMeshes[p], bones);
      }

      // Weapon shadow (compute weapon final bones for shadow projection)
      static constexpr int BONE_BACK = 47;
      if (s_slotRender[i].weaponBmd && BONE_BACK < (int)bones.size()) {
        BoneWorldMatrix wOffsetMat = MuMath::BuildWeaponOffsetMatrix(
            glm::vec3(70.f, 0.f, 90.f), glm::vec3(-20.f, 5.f, 40.f));
        BoneWorldMatrix wParentMat;
        MuMath::ConcatTransforms((const float(*)[4])bones[BONE_BACK].data(),
                                 (const float(*)[4])wOffsetMat.data(),
                                 (float(*)[4])wParentMat.data());
        auto &wLocal = s_slotRender[i].weaponLocalBones;
        std::vector<BoneWorldMatrix> wFinal(wLocal.size());
        for (int bi = 0; bi < (int)wLocal.size(); ++bi)
          MuMath::ConcatTransforms((const float(*)[4])wParentMat.data(),
                                   (const float(*)[4])wLocal[bi].data(),
                                   (float(*)[4])wFinal[bi].data());
        projectShadow(s_slotRender[i].weaponBmd.get(),
                       s_slotRender[i].weaponShadowMeshes, wFinal);
      }

      // Shield shadow
      if (s_slotRender[i].shieldBmd && BONE_BACK < (int)bones.size()) {
        BoneWorldMatrix sOffsetMat = MuMath::BuildWeaponOffsetMatrix(
            glm::vec3(70.f, 0.f, 90.f), glm::vec3(-10.f, 0.f, 0.f));
        BoneWorldMatrix sParentMat;
        MuMath::ConcatTransforms((const float(*)[4])bones[BONE_BACK].data(),
                                 (const float(*)[4])sOffsetMat.data(),
                                 (float(*)[4])sParentMat.data());
        auto &sLocal = s_slotRender[i].shieldLocalBones;
        std::vector<BoneWorldMatrix> sFinal(sLocal.size());
        for (int bi = 0; bi < (int)sLocal.size(); ++bi)
          MuMath::ConcatTransforms((const float(*)[4])sParentMat.data(),
                                   (const float(*)[4])sLocal[bi].data(),
                                   (float(*)[4])sFinal[bi].data());
        projectShadow(s_slotRender[i].shieldBmd.get(),
                       s_slotRender[i].shieldShadowMeshes, sFinal);
      }
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
  }

  // Render character models at slot positions
  if (s_modelShader) {
    s_modelShader->use();
    s_modelShader->setMat4("view", s_viewMatrix);
    s_modelShader->setMat4("projection", s_projMatrix);
    s_modelShader->setFloat("objectAlpha", 1.0f);
    // Elegant directional sunlight from above — warm, high position
    s_modelShader->setVec3("lightPos", s_sunLightPos);
    s_modelShader->setVec3("lightColor", CS_SUN_COLOR);
    s_modelShader->setVec3("viewPos", camPos);
    s_modelShader->setFloat("blendMeshLight", 1.0f);
    s_modelShader->setFloat("luminosity", CS_LUMINOSITY);
    s_modelShader->setBool("useFog", false);
    s_modelShader->setVec2("texCoordOffset", glm::vec2(0.0f));
    s_modelShader->setFloat("outlineOffset", 0.0f);

    // Set point lights from nearby fire/torch world objects
    int numPL = (int)s_pointLights.size();
    s_modelShader->setInt("numPointLights", numPL);
    for (int pli = 0; pli < numPL; ++pli) {
      std::string idx = std::to_string(pli);
      s_modelShader->setVec3(("pointLightPos[" + idx + "]").c_str(),
                              s_pointLights[pli].position);
      s_modelShader->setVec3(("pointLightColor[" + idx + "]").c_str(),
                              s_pointLights[pli].color);
      s_modelShader->setFloat(("pointLightRange[" + idx + "]").c_str(),
                               s_pointLights[pli].range);
    }

    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied)
        continue;

      auto &sp = SLOT_POSITIONS[i];
      float facing = sp.facingDeg * (3.14159f / 180.0f);

      // Get terrain height at slot position for correct Y placement
      float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ)
                                    : 0.0f;

      // Same model matrix as HeroCharacter (no extra scale needed)
      glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                        glm::vec3(sp.worldX, slotY, sp.worldZ));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, facing, glm::vec3(0, 0, 1));

      // Terrain lightmap lighting at character position
      glm::vec3 tLight = SampleTerrainLight(sp.worldX, sp.worldZ);
      s_modelShader->setVec3("terrainLight", tLight);

      // Selected character slightly brighter sun
      float brightness = (i == s_selectedSlot) ? 1.2f : 1.0f;
      s_modelShader->setVec3("lightColor",
                             CS_SUN_COLOR * brightness);
      s_modelShader->setMat4("model", model);

      // Draw all body parts
      for (int p = 0; p < PART_COUNT; p++) {
        for (auto &mb : s_slotRender[i].meshes[p]) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, mb.texture);
          s_modelShader->setInt("texture_diffuse", 0);
          glBindVertexArray(mb.vao);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }
      // Draw base head (class default face under accessory helm)
      if (s_slotRender[i].showBaseHead) {
        for (auto &mb : s_slotRender[i].baseHeadMeshes) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, mb.texture);
          s_modelShader->setInt("texture_diffuse", 0);
          glBindVertexArray(mb.vao);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }

      // Draw weapon (pre-skinned to back bone in ReskinSlot)
      {
        int blendIdx = s_slotRender[i].weaponBlendMesh;
        int mi = 0;
        for (auto &mb : s_slotRender[i].weaponMeshes) {
          if (mb.indexCount == 0) { mi++; continue; }
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, mb.texture);
          s_modelShader->setInt("texture_diffuse", 0);
          glBindVertexArray(mb.vao);
          if (blendIdx >= 0 && mi == blendIdx) {
            float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
            s_modelShader->setFloat("blendMeshLight", pulseLight);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            s_modelShader->setFloat("blendMeshLight", 1.0f);
          } else {
            glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
          }
          mi++;
        }
      }

      // Draw shield
      {
        int blendIdx = s_slotRender[i].shieldBlendMesh;
        int mi = 0;
        for (auto &mb : s_slotRender[i].shieldMeshes) {
          if (mb.indexCount == 0) { mi++; continue; }
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, mb.texture);
          s_modelShader->setInt("texture_diffuse", 0);
          glBindVertexArray(mb.vao);
          if (blendIdx >= 0 && mi == blendIdx) {
            float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
            s_modelShader->setFloat("blendMeshLight", pulseLight);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            s_modelShader->setFloat("blendMeshLight", 1.0f);
          } else {
            glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
          }
          mi++;
        }
      }

    }
  }

  // Render silhouette outline for selected character
  if (s_outlineShader && s_selectedSlot >= 0 &&
      s_slots[s_selectedSlot].occupied && !s_createOpen) {
    int i = s_selectedSlot;
    auto &sp = SLOT_POSITIONS[i];
    float facing = sp.facingDeg * (3.14159f / 180.0f);
    float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ)
                                  : 0.0f;

    glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                      glm::vec3(sp.worldX, slotY, sp.worldZ));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    model = glm::rotate(model, facing, glm::vec3(0, 0, 1));

    s_outlineShader->use();
    s_outlineShader->setMat4("projection", s_projMatrix);
    s_outlineShader->setMat4("view", s_viewMatrix);

    glDisable(GL_CULL_FACE);

    // Pass 1: Write silhouette to stencil (depth off for complete coverage)
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    s_outlineShader->setMat4("model", model);
    s_outlineShader->setFloat("outlineThickness", 0.0f);

    for (int p = 0; p < PART_COUNT; p++) {
      for (auto &mb : s_slotRender[i].meshes[p]) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }
    if (s_slotRender[i].showBaseHead) {
      for (auto &mb : s_slotRender[i].baseHeadMeshes) {
        if (mb.indexCount == 0 || mb.hidden) continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }
    for (auto &mb : s_slotRender[i].weaponMeshes) {
      if (mb.indexCount == 0) continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    for (auto &mb : s_slotRender[i].shieldMeshes) {
      if (mb.indexCount == 0) continue;
      glBindVertexArray(mb.vao);
      glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    // Pass 2: Draw outline layers where stencil != 1
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    s_outlineShader->setVec3("outlineColor", 0.8f, 0.4f, 0.15f);
    s_outlineShader->setMat4("model", model);

    constexpr float thicknesses[] = {5.0f, 3.5f, 2.0f};
    constexpr float alphas[] = {0.08f, 0.18f, 0.35f};

    for (int layer = 0; layer < 3; ++layer) {
      s_outlineShader->setFloat("outlineThickness", thicknesses[layer]);
      s_outlineShader->setFloat("outlineAlpha", alphas[layer]);

      for (int p = 0; p < PART_COUNT; p++) {
        for (auto &mb : s_slotRender[i].meshes[p]) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          glBindVertexArray(mb.vao);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }
      if (s_slotRender[i].showBaseHead) {
        for (auto &mb : s_slotRender[i].baseHeadMeshes) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          glBindVertexArray(mb.vao);
          glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }
      for (auto &mb : s_slotRender[i].weaponMeshes) {
        if (mb.indexCount == 0) continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
      }
      for (auto &mb : s_slotRender[i].shieldMeshes) {
        if (mb.indexCount == 0) continue;
        glBindVertexArray(mb.vao);
        glDrawElements(GL_TRIANGLES, mb.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
  }

  // Render face portrait to FBO (if create window is open)
  if (s_createOpen && s_faceLoadedClass >= 0) {
    // Rebuild face meshes if class changed
    int wantIdx = ClassToIndex(s_createClass);
    if (wantIdx != s_faceLoadedClass) {
      RebuildFaceMeshes(wantIdx);
    }
    RenderFaceToFBO();
  }

  // ── ImGui UI overlay ──
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
  ImGui::Begin("##CharSelectOverlay",
               nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  float cx = windowWidth * 0.5f;

  // Title (hidden during character creation)
  if (!s_createOpen) {
    const char *title = "MU Online Remaster";
    ImVec2 tsz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(cx - tsz.x * 0.5f, 30), IM_COL32(220, 200, 160, 255),
                title);
  }

  // Character name plates (floating labels above each character)
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!s_slots[i].occupied)
      continue;
    auto &ch = s_slots[i];
    auto &sp = SLOT_POSITIONS[i];

    // Project character head position to screen
    float slotY = s_terrainLoaded
                      ? s_terrain.GetHeight(sp.worldX, sp.worldZ)
                      : 0.0f;
    glm::vec3 headPos(sp.worldX, slotY + 220.0f, sp.worldZ);
    glm::vec2 screenPos = ProjectToScreen(headPos, windowWidth, windowHeight);
    if (screenPos.x < 0)
      continue;

    // Line 1: character name
    // Line 2: "ClassName Level"
    const char *className = GetClassStats(ch.classCode).name;
    char classLine[64];
    snprintf(classLine, sizeof(classLine), "%s %d", className, ch.level);

    ImVec2 nameSize = ImGui::CalcTextSize(ch.name);
    ImVec2 classSize = ImGui::CalcTextSize(classLine);
    float maxW = std::max(nameSize.x, classSize.x);
    float lineH = nameSize.y + 2.0f;
    float totalH = lineH * 2.0f + 8.0f;

    // Background rect
    float bgW = maxW + 24.0f;
    float bgX = screenPos.x - bgW * 0.5f;
    float bgY = screenPos.y - totalH;
    dl->AddRectFilledMultiColor(ImVec2(bgX, bgY), ImVec2(bgX + bgW, bgY + totalH),
                      IM_COL32(8, 8, 18, 180), IM_COL32(8, 8, 18, 180),
                      IM_COL32(14, 14, 26, 170), IM_COL32(14, 14, 26, 170));
    dl->AddRect(ImVec2(bgX, bgY), ImVec2(bgX + bgW, bgY + totalH),
                IM_COL32(65, 60, 45, 140), 4.0f);

    // Name (white, gold for selected) — with shadow
    ImU32 nameColor = (i == s_selectedSlot)
                          ? IM_COL32(255, 220, 150, 255)
                          : IM_COL32(255, 255, 255, 255);
    float nx = screenPos.x - nameSize.x * 0.5f;
    dl->AddText(ImVec2(nx + 1, bgY + 5.0f), IM_COL32(0, 0, 0, 200), ch.name);
    dl->AddText(ImVec2(nx, bgY + 4.0f), nameColor, ch.name);

    // Class + level (orange) — with shadow
    float cx2 = screenPos.x - classSize.x * 0.5f;
    dl->AddText(ImVec2(cx2 + 1, bgY + 5.0f + lineH),
                IM_COL32(0, 0, 0, 200), classLine);
    dl->AddText(ImVec2(cx2, bgY + 4.0f + lineH),
                IM_COL32(255, 180, 80, 255), classLine);
  }

  ImGui::End();

  // ── Bottom button bar (hidden during character creation) ──
  if (!s_createOpen) {
  float btnW = 100, btnH = 36, btnGap = 8;
  float btnY = windowHeight - 55.0f;
  const ImGuiWindowFlags kBtnFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.14f, 0.13f, 0.10f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.27f, 0.18f, 0.7f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.84f, 0.78f, 0.94f));

  // Left button: [Create]
  ImGui::SetNextWindowPos(ImVec2(10, btnY - 5));
  ImGui::SetNextWindowSize(ImVec2(btnW + 20, btnH + 10));
  ImGui::Begin("##CSBtnLeft", nullptr, kBtnFlags);
  if (ImGui::Button("Create", ImVec2(btnW, btnH))) {
    int count = 0;
    for (int i = 0; i < MAX_SLOTS; i++)
      if (s_slots[i].occupied)
        count++;
    if (count < MAX_SLOTS) {
      s_createOpen = true;
      memset(s_createName, 0, sizeof(s_createName));
      s_createClass = CLASS_DK;
      RebuildFaceMeshes(ClassToIndex(CLASS_DK));
    } else {
      snprintf(s_statusMsg, sizeof(s_statusMsg), "Maximum 5 characters");
      s_statusTimer = 2.0f;
    }
  }
  ImGui::End();

  // Right buttons: [Connect] [Delete] — only shown when a character is selected
  bool hasSelection = (s_selectedSlot >= 0 && s_slots[s_selectedSlot].occupied);
  if (hasSelection) {
    float rightX = windowWidth - 10.0f - btnW * 2 - btnGap - 20;
    ImGui::SetNextWindowPos(ImVec2(rightX, btnY - 5));
    ImGui::SetNextWindowSize(ImVec2(btnW * 2 + btnGap + 20, btnH + 10));
    ImGui::Begin("##CSBtnRight", nullptr, kBtnFlags);
    if (ImGui::Button("Connect", ImVec2(btnW, btnH))) {
      if (s_ctx.server) {
        s_ctx.server->SendCharSelect(s_slots[s_selectedSlot].name);
        if (s_ctx.onCharSelected)
          s_ctx.onCharSelected();
      }
    }
    ImGui::SameLine(0, btnGap);
    if (ImGui::Button("Delete", ImVec2(btnW, btnH))) {
      s_deleteConfirm = true;
    }
    ImGui::End();
  }

  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
  } // end if (!s_createOpen) bottom bar

  // ── Character slot click areas (invisible buttons over each slot) ──
  // We handle this via OnMouseClick instead of ImGui

  // ── Create character panel: model container above, form container below ──
  if (s_createOpen) {
    int wantIdx = ClassToIndex(s_createClass);
    if (wantIdx != s_faceLoadedClass) {
      RebuildFaceMeshes(wantIdx);
    }

    auto &cs = GetClassStats(s_createClass);
    const uint8_t classCodes[] = {CLASS_DW, CLASS_DK, CLASS_ELF, CLASS_MG};
    const char *classNames[] = {"Dark Wizard", "Dark Knight", "Elf",
                                "Magic Gladiator"};
    const int numClasses = 4;
    int classIdx = 0;
    for (int i = 0; i < numClasses; i++)
      if (classCodes[i] == s_createClass) { classIdx = i; break; }

    float W = (float)windowWidth;
    float H = (float)windowHeight;

    // Panel dimensions
    float uiScale = std::min(W / 640.0f, H / 480.0f);
    uiScale = std::max(uiScale, 1.0f);
    float panelW = 454.0f * uiScale;
    panelW = std::min(panelW, W * 0.85f);

    // Form container: fixed height for name input + description
    float formH = 70.0f * uiScale;

    // Right-side overlay width (stats + class buttons)
    float statOverlayW = panelW * (130.0f / 454.0f);
    float statOverlayX = 0.0f; // set after panelX is known

    // Compute face display size from panel width — model fills left area
    // UV crop: hide the bottom 15% of the FBO to crop arm stumps
    // This makes the model appear bigger than the container (overflow:hidden)
    float uvCropBottom = 0.25f; // fraction of FBO bottom to hide
    float uvCropTop = 0.10f;   // fraction of FBO top to hide (empty space above head)
    float uvVisibleFrac = 1.0f - uvCropBottom - uvCropTop;
    float fboAspect = (float)FACE_TEX_W / (float)FACE_TEX_H;
    float croppedAspect = (float)FACE_TEX_W / ((float)FACE_TEX_H * uvVisibleFrac);
    float modelAreaW = panelW - statOverlayW;
    float faceDispW = modelAreaW;
    float faceDispH = faceDispW / croppedAspect;
    // Cap model height to 70% of screen
    float maxModelH = H * 0.70f;
    if (faceDispH > maxModelH) {
      faceDispH = maxModelH;
      faceDispW = faceDispH * croppedAspect;
    }

    // Model container height = exactly the face display height (no gap)
    float modelH = faceDispH;
    float panelH = modelH + formH;
    panelH = std::min(panelH, H * 0.92f);
    modelH = panelH - formH;
    // Recalculate face if model was clamped
    if (faceDispH > modelH) {
      faceDispH = modelH;
      faceDispW = faceDispH * fboAspect;
    }

    float panelX = (W - panelW) * 0.5f;
    float panelY = (H - panelH) * 0.45f;
    statOverlayX = panelX + panelW - statOverlayW;

    // Zone boundaries
    float formY = panelY + modelH; // form starts directly after model

    // Center face horizontally in the model area, flush to bottom (= formY)
    float faceX = panelX + (modelAreaW - faceDispW) * 0.5f;
    float faceY = panelY; // model fills entire model zone top-to-bottom

    // ImGui window for interactive controls
    ImGui::SetNextWindowPos(ImVec2(panelX - 5, panelY - 5));
    ImGui::SetNextWindowSize(ImVec2(panelW + 10, panelH + 10));
    ImGui::Begin("##CreatePanel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *cdl = ImGui::GetWindowDrawList();

    // ── Model container background ──
    cdl->AddRectFilledMultiColor(ImVec2(panelX, panelY),
                       ImVec2(panelX + panelW, formY),
                       IM_COL32(8, 8, 18, 160), IM_COL32(8, 8, 18, 160),
                       IM_COL32(14, 14, 26, 150), IM_COL32(14, 14, 26, 150));
    cdl->AddRect(ImVec2(panelX, panelY),
                 ImVec2(panelX + panelW, formY),
                 IM_COL32(5, 5, 10, 220), 2.0f, 0, 2.0f);
    cdl->AddRect(ImVec2(panelX + 2, panelY + 2),
                 ImVec2(panelX + panelW - 2, formY - 2),
                 IM_COL32(80, 70, 45, 120));

    // ── Face portrait — anchored to bottom of model container ──
    if (s_faceColorTex && s_faceLoadedClass >= 0) {
      ImGui::SetCursorPos(ImVec2(faceX - (panelX - 5), faceY - (panelY - 5)));
      // UV crop: hide empty space above head (top) and arm stumps (bottom)
      // OpenGL FBO: UV y=0 is bottom of scene, y=1 is top
      // Display flip: UV0=(0, 1-cropTop)=top, UV1=(1, cropBottom)=bottom
      ImGui::Image((ImTextureID)(intptr_t)s_faceColorTex,
                   ImVec2(faceDispW, faceDispH),
                   ImVec2(0, 1.0f - uvCropTop), ImVec2(1, uvCropBottom));
    }

    // ── Stats panel (overlaid, bottom-right of model zone, above class buttons) ──
    // Anchor to bottom: class buttons at bottom, stats above them
    float cbH_pre = 26.0f * uiScale;
    float cbTotalH = 4.5f * cbH_pre; // 4 buttons + 0.5 gap before MG
    float bottomMargin = 6.0f * uiScale;
    float gapBetween = 4.0f * uiScale;
    {
      float statX = statOverlayX;
      float statW = statOverlayW - 4 * uiScale;
      float statH = 85.0f * uiScale;
      float statY = formY - bottomMargin - cbTotalH - gapBetween - statH;

      cdl->AddRectFilled(ImVec2(statX, statY),
                         ImVec2(statX + statW, statY + statH),
                         IM_COL32(8, 8, 16, 180), 3.0f);
      cdl->AddRect(ImVec2(statX, statY),
                   ImVec2(statX + statW, statY + statH),
                   IM_COL32(65, 60, 45, 120), 3.0f);

      const char *statLabels[] = {"Strength", "Agility", "Vitality", "Energy"};
      int statValues[] = {cs.str, cs.dex, cs.vit, cs.ene};
      float lineH = 17.0f * uiScale;
      for (int i = 0; i < 4; i++) {
        float ly = statY + 8 * uiScale + i * lineH;
        cdl->AddText(ImVec2(statX + 8 * uiScale, ly),
                     IM_COL32(255, 255, 255, 255), statLabels[i]);
        char valStr[8];
        snprintf(valStr, sizeof(valStr), "%d", statValues[i]);
        ImVec2 valSz = ImGui::CalcTextSize(valStr);
        cdl->AddText(ImVec2(statX + statW - 8 * uiScale - valSz.x, ly),
                     IM_COL32(255, 165, 0, 255), valStr);
      }
    }

    // ── Class selection buttons (overlaid, right side, anchored to bottom) ──
    {
      float cbAbsX = statOverlayX;
      float cbAbsY = formY - bottomMargin - cbTotalH;
      float cbW = statOverlayW - 4 * uiScale;
      float cbH = cbH_pre;
      float cbGap = 0.0f;

      float cbX = cbAbsX - (panelX - 5);
      float cbY = cbAbsY - (panelY - 5);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0.04f, 0.04f, 0.07f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.14f, 0.13f, 0.10f, 0.92f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border,
                            ImVec4(0.25f, 0.22f, 0.14f, 0.55f));

      float yAccum = 0.0f;
      for (int i = 0; i < numClasses; i++) {
        if (i == 3) yAccum += cbH * 0.5f; // gap before MG

        ImGui::SetCursorPos(ImVec2(cbX, cbY + yAccum));

        bool isSelected = (classCodes[i] == s_createClass);
        if (isSelected) {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.16f, 0.14f, 0.08f, 0.92f));
          ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(1.0f, 0.88f, 0.55f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Border,
                                ImVec4(0.70f, 0.63f, 0.35f, 0.8f));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.04f, 0.04f, 0.07f, 0.85f));
          ImGui::PushStyleColor(ImGuiCol_Border,
                                ImVec4(0.25f, 0.22f, 0.14f, 0.55f));
        }

        bool enabled = (classCodes[i] == CLASS_DK || classCodes[i] == CLASS_DW);
        if (!enabled) ImGui::BeginDisabled();

        char btnId[32];
        snprintf(btnId, sizeof(btnId), "%s##cls%d", classNames[i], i);
        if (ImGui::Button(btnId, ImVec2(cbW, cbH))) {
          s_createClass = classCodes[i];
        }

        if (!enabled) ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        yAccum += cbH + cbGap;
      }
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(2);
    }

    // ── Form container background (below model) ──
    cdl->AddRectFilledMultiColor(ImVec2(panelX, formY),
                       ImVec2(panelX + panelW, panelY + panelH),
                       IM_COL32(10, 10, 20, 200), IM_COL32(10, 10, 20, 200),
                       IM_COL32(16, 16, 30, 195), IM_COL32(16, 16, 30, 195));
    cdl->AddRect(ImVec2(panelX, formY),
                 ImVec2(panelX + panelW, panelY + panelH),
                 IM_COL32(5, 5, 10, 220), 2.0f, 0, 2.0f);
    cdl->AddRect(ImVec2(panelX + 2, formY + 2),
                 ImVec2(panelX + panelW - 2, panelY + panelH - 2),
                 IM_COL32(80, 70, 45, 120));
    // Divider line between model and form
    cdl->AddLine(ImVec2(panelX + 3, formY),
                 ImVec2(panelX + panelW - 3, formY),
                 IM_COL32(80, 70, 45, 160), 1.0f);

    // ── Name input (form left side) ──
    {
      float nameAbsX = panelX + 10 * uiScale;
      float nameAbsY = formY + 8 * uiScale;
      float nameW = panelW * 0.55f;

      float nameX = nameAbsX - (panelX - 5);
      float nameY = nameAbsY - (panelY - 5);

      ImGui::SetCursorPos(ImVec2(nameX, nameY));
      ImGui::PushStyleColor(ImGuiCol_FrameBg,
                            ImVec4(0.05f, 0.04f, 0.02f, 0.9f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                            ImVec4(0.12f, 0.09f, 0.05f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border,
                            ImVec4(0.45f, 0.38f, 0.22f, 0.7f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::SetNextItemWidth(nameW);
      ImGui::InputTextWithHint("##createName",
                               "Character name (4-10 chars)",
                               s_createName, 11);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
    }

    // ── OK / Cancel buttons (form right side) ──
    {
      float obW = 54.0f * uiScale;
      float obH = 26.0f * uiScale;
      float obGap = 6.0f * uiScale;

      float okAbsX = panelX + panelW - obW * 2 - obGap - 8 * uiScale;
      float okAbsY = formY + 8 * uiScale;

      float okX = okAbsX - (panelX - 5);
      float okY = okAbsY - (panelY - 5);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.14f, 0.13f, 0.10f, 0.95f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border,
                            ImVec4(0.31f, 0.27f, 0.18f, 0.7f));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0.86f, 0.84f, 0.78f, 0.94f));

      ImGui::SetCursorPos(ImVec2(okX, okY));
      if (ImGui::Button("OK##create", ImVec2(obW, obH))) {
        int nameLen = (int)strlen(s_createName);
        if (nameLen >= 4 && nameLen <= 10) {
          s_ctx.server->SendCharCreate(s_createName, s_createClass);
        } else {
          snprintf(s_statusMsg, sizeof(s_statusMsg),
                   "Name must be 4-10 characters");
          s_statusTimer = 2.0f;
        }
      }
      ImGui::SameLine(0.0f, obGap);
      if (ImGui::Button("Cancel##create", ImVec2(obW, obH))) {
        s_createOpen = false;
      }
      ImGui::PopStyleColor(5);
      ImGui::PopStyleVar(2);
    }

    // ── Description text (bottom of form) ──
    {
      const char *classDescs[] = {
          "The Dark Wizard commands powerful magic with high Energy.",
          "The Dark Knight excels in close combat with superior Strength.",
          "The Elf supports allies with healing and strikes from range.",
          "The Magic Gladiator combines Strength and Energy for versatility."};

      float descY = formY + 36 * uiScale;
      float descX = panelX + 10 * uiScale;
      cdl->AddText(ImVec2(descX + 1, descY + 1),
                   IM_COL32(0, 0, 0, 180), classDescs[classIdx]);
      cdl->AddText(ImVec2(descX, descY),
                   IM_COL32(200, 200, 200, 220), classDescs[classIdx]);
    }

    ImGui::End();
  }

  // ── Delete confirmation modal ──
  if (s_deleteConfirm && s_selectedSlot >= 0) {
    ImGui::SetNextWindowPos(ImVec2(cx - 150, windowHeight * 0.4f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 130), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.08f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.27f, 0.18f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.10f, 0.07f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("Delete Character", &s_deleteConfirm,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Delete '%s'?", s_slots[s_selectedSlot].name);
    ImGui::Text("This cannot be undone.");
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.13f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
    if (ImGui::Button("Yes, Delete", ImVec2(120, 30))) {
      s_ctx.server->SendCharDelete(
          s_selectedSlot, s_slots[s_selectedSlot].name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 30))) {
      s_deleteConfirm = false;
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
  }
}

void OnMouseClick(double screenX, double screenY, int windowWidth,
                  int windowHeight) {
  if (s_createOpen || s_deleteConfirm)
    return;

  // Convert screen position to a ray and test against character slot positions
  // Simple approach: project each slot world position to screen, find closest
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!s_slots[i].occupied)
      continue;

    auto &sp = SLOT_POSITIONS[i];
    float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ)
                                  : 0.0f;
    glm::vec4 worldPos(sp.worldX, slotY + 100.0f, sp.worldZ, 1.0f);
    glm::vec4 clipPos = s_projMatrix * s_viewMatrix * worldPos;
    if (clipPos.w <= 0)
      continue;

    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    float sx = (ndc.x * 0.5f + 0.5f) * windowWidth;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * windowHeight;

    float dist = sqrtf((float)(screenX - sx) * (float)(screenX - sx) +
                       (float)(screenY - sy) * (float)(screenY - sy));
    if (dist < 80.0f) { // Click radius in pixels
      s_selectedSlot = i;
      printf("[CharSelect] Selected slot %d: '%s'\n", i, s_slots[i].name);
      return;
    }
  }
}

void OnKeyPress(int key) {
  if (key == GLFW_KEY_ENTER && !s_createOpen) {
    // Enter = connect with selected character
    if (s_selectedSlot >= 0 && s_slots[s_selectedSlot].occupied && s_ctx.server) {
      s_ctx.server->SendCharSelect(s_slots[s_selectedSlot].name);
      if (s_ctx.onCharSelected)
        s_ctx.onCharSelected();
    }
  }
  if (key == GLFW_KEY_ESCAPE) {
    if (s_createOpen)
      s_createOpen = false;
    else if (s_deleteConfirm)
      s_deleteConfirm = false;
  }
  // Arrow keys: cycle through occupied character slots
  if ((key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) && !s_createOpen) {
    int dir = (key == GLFW_KEY_LEFT) ? -1 : 1;
    for (int j = 1; j < MAX_SLOTS; j++) {
      int idx = (s_selectedSlot + dir * j + MAX_SLOTS) % MAX_SLOTS;
      if (s_slots[idx].occupied) {
        s_selectedSlot = idx;
        break;
      }
    }
  }
}

void OnCharInput(unsigned int codepoint) {
  // ImGui handles text input for the create modal
}

bool IsCreateModalOpen() { return s_createOpen || s_deleteConfirm; }

} // namespace CharacterSelect
