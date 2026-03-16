#include "CharacterSelect.hpp"
#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "BoidManager.hpp"
#include "ChromeGlow.hpp"
#include "ClientTypes.hpp"
#include "GrassRenderer.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "ObjectRenderer.hpp"
#include "PacketDefs.hpp"
#include "Sky.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include "imgui.h"
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

// Sky dome
static Sky s_sky;

// Ambient creatures (birds, fish, falling leaves)
static BoidManager s_boidManager;

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
  bgfx::DynamicVertexBufferHandle vbo = BGFX_INVALID_HANDLE;
  int vertexCount = 0;
};

// Per-slot rendering data
static constexpr int ACTION_IDLE = 1;
// Per-class emotes: males=Salute(218), ELF=Greeting female(188)
static constexpr int CLASS_EMOTES[4] = {218, 218, 188, 218};

struct SlotRender {
  std::vector<MeshBuffers> meshes[PART_COUNT];
  std::vector<ShadowMesh> shadowMeshes[PART_COUNT];
  std::unique_ptr<BMDData> partOverrideBmd[PART_COUNT]; // null = use class default
  float animFrame = 0.0f;
  int action = ACTION_IDLE;        // Current animation action
  bool emotePlaying = false;       // True while emote is in progress
  float emoteBlend = 0.0f;        // 0=idle, 1=full emote (lerp factor)
  float idleFrame = 0.0f;         // Idle frame preserved during blend

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

  // Wings (attached to bone 47 — Main 5.2 RenderCharacterBackItem)
  std::unique_ptr<BMDData> wingBmd;
  std::vector<MeshBuffers> wingMeshes;
  std::vector<ShadowMesh> wingShadowMeshes;
  std::vector<BoneWorldMatrix> wingLocalBones;

  // BlendMesh glow indices (-1 = none)
  int weaponBlendMesh = -1;
  int shieldBlendMesh = -1;
  int wingBlendMesh = -1;

  // Wing item index (0-6) for per-wing rendering behavior
  int wingItemIndex = -1;

  // Wing animation frame (independent from character animation)
  float wingAnimFrame = 0.0f;

};
static SlotRender s_slotRender[MAX_SLOTS];

// Model shader (reuse existing model.vert/frag)
static std::unique_ptr<Shader> s_modelShader;
static std::unique_ptr<Shader> s_shadowShader;

// Shadow map state
static constexpr uint8_t CS_SHADOW_VIEW = 8;
static constexpr int CS_SHADOW_MAP_SIZE = 2048;
static bgfx::FrameBufferHandle s_shadowFB = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_shadowColorTex = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_shadowDepthTex = BGFX_INVALID_HANDLE;
static std::unique_ptr<Shader> s_depthShader;
static glm::mat4 s_lightMtx{1.0f};

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

// Selection spotlight — tall light cone around selected character
static TexHandle s_spotlightTex = kInvalidTex;
static bgfx::VertexBufferHandle s_spotVBO = BGFX_INVALID_HANDLE;
static bgfx::IndexBufferHandle s_spotEBO = BGFX_INVALID_HANDLE;
static int s_spotlightIndexCount = 0;
static std::unique_ptr<Shader> s_spotlightShader;
static constexpr int CONE_SEGMENTS = 32;
static constexpr float CONE_HEIGHT = 800.0f;
static constexpr float CONE_RADIUS = 55.0f;

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

// Face screen rect (set by UI code, used by BGFX render to draw face directly)
static bool  s_faceScreenValid = false;
static uint16_t s_faceScreenX = 0, s_faceScreenY = 0;
static uint16_t s_faceScreenW = 0, s_faceScreenH = 0;

// FBO for rendering face portrait to texture
static bgfx::FrameBufferHandle s_faceFB = BGFX_INVALID_HANDLE;
static constexpr uint8_t FACE_VIEW_ID = 10; // Dedicated view for face FBO rendering
static TexHandle s_faceColorTex = kInvalidTex;
static constexpr int FACE_TEX_W = 410;
static constexpr int FACE_TEX_H = 500;

// Per-class face rendering parameters (Z rotation for 3/4 view angle)
struct FaceRenderParams {
  float angleZ; // Z-axis rotation for 3/4 view
};
static constexpr FaceRenderParams FACE_PARAMS[4] = {
    {-18.0f},  // DW — 3/4 left turn
    {-30.0f},  // DK — 3/4 left turn (matches original)
    {  8.0f},  // ELF — slight right turn
    {-18.0f},  // MG — 3/4 left turn
};

// Character slot positions (world coordinates) — customize these
struct SlotPos {
  float worldX, worldZ, facingDeg;
};
// Character slots: 5 positions on the road
// Camera at (21743, 668, 13133), yaw=297.3° pitch=-14.8°
// Scene center ~700 units forward from camera
static constexpr float SCENE_CX = 22065.0f;
static constexpr float SCENE_CZ = 12511.0f;
static constexpr float FWD_X = 0.459f;
static constexpr float FWD_Z = -0.889f;
static constexpr float RIGHT_X = 0.889f;
static constexpr float RIGHT_Z = 0.459f;
static constexpr float FACE_DEG = 62.7f;
// 4 slots, 320 units apart, centered on scene
// Positive RIGHT = screen-left, Negative RIGHT = screen-right
// Reversed: slot 0 (first char = DK) on screen-left
static constexpr SlotPos SLOT_POSITIONS[MAX_SLOTS] = {
    {SCENE_CX + RIGHT_X * -480 + FWD_X * 510, SCENE_CZ + RIGHT_Z * -480 + FWD_Z * 510, FACE_DEG},
    {SCENE_CX + RIGHT_X * -160 + FWD_X * 490, SCENE_CZ + RIGHT_Z * -160 + FWD_Z * 490, FACE_DEG},
    {SCENE_CX + RIGHT_X * 160 + FWD_X * 490, SCENE_CZ + RIGHT_Z * 160 + FWD_Z * 490, FACE_DEG},
    {SCENE_CX + RIGHT_X * 480 + FWD_X * 510, SCENE_CZ + RIGHT_Z * 480 + FWD_Z * 510, FACE_DEG},
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
  const uint64_t tsFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  bgfx::TextureHandle faceTex[] = {
    bgfx::createTexture2D(FACE_TEX_W, FACE_TEX_H, false, 1,
                          bgfx::TextureFormat::RGBA8, tsFlags),
    bgfx::createTexture2D(FACE_TEX_W, FACE_TEX_H, false, 1,
                          bgfx::TextureFormat::D24S8, tsFlags),
  };
  s_faceFB = bgfx::createFrameBuffer(2, faceTex, true);
  // Get color attachment handle for ImGui display
  s_faceColorTex = bgfx::getTexture(s_faceFB, 0);
  printf("[CharSelect] BGFX Face FBO created (%dx%d)\n", FACE_TEX_W, FACE_TEX_H);
}

static void CleanupFaceFBO() {
  if (bgfx::isValid(s_faceFB)) {
    bgfx::destroy(s_faceFB);
    s_faceFB = BGFX_INVALID_HANDLE;
  }
  // Color texture is owned by the framebuffer (destroyTextures=true), so
  // just invalidate our handle
  s_faceColorTex = kInvalidTex;
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

// Render face model directly to backbuffer using a restricted viewport.
// This avoids the FBO alpha compositing issue on Metal.
static constexpr uint8_t FACE_DIRECT_VIEW = 31; // AFTER ImGui (30), face on top of container fill
static void RenderFaceToBackbuffer() {
  if (s_faceLoadedClass < 0 || s_faceMeshes.empty() || !s_modelShader ||
      !s_faceScreenValid || s_faceScreenW == 0 || s_faceScreenH == 0)
    return;

  auto &fp = FACE_PARAMS[s_faceLoadedClass];

  // Auto-frame: compute camera from AABB
  float angleZ = glm::radians(fp.angleZ);
  float height = s_faceAABBMax.z - s_faceAABBMin.z;
  float width = glm::length(glm::vec2(s_faceAABBMax.x - s_faceAABBMin.x,
                                       s_faceAABBMax.y - s_faceAABBMin.y));

  float aspect = (float)s_faceScreenW / (float)s_faceScreenH;
  float fovDeg = 10.0f;
  float fov = glm::radians(fovDeg);

  float margin = 0.92f;
  float fitHeight = height * margin;
  float fitWidth = width * margin;
  float distH = (fitHeight * 0.5f) / tanf(fov * 0.5f);
  float distW = (fitWidth * 0.5f) / (tanf(fov * 0.5f) * aspect);
  float camDist = std::max(distH, distW);

  float modelCenter = (s_faceAABBMin.z + s_faceAABBMax.z) * 0.5f;
  float pushDown = height * 0.12f;
  float targetZ = modelCenter + pushDown;

  glm::vec3 camP(0.0f, -camDist, targetZ);
  glm::vec3 target(0.0f, 0.0f, targetZ);
  glm::mat4 faceView = glm::lookAt(camP, target, glm::vec3(0.0f, 0.0f, 1.0f));
  glm::mat4 faceProj = glm::perspective(fov, aspect, 1.0f, camDist * 3.0f);

  // Model matrix: per-class Z rotation
  glm::mat4 model(1.0f);
  model = glm::rotate(model, angleZ, glm::vec3(0, 0, 1));

  // Setup BGFX view: render to backbuffer with restricted viewport
  bgfx::setViewName(FACE_DIRECT_VIEW, "FaceDirect");
  bgfx::setViewRect(FACE_DIRECT_VIEW, s_faceScreenX, s_faceScreenY,
                     s_faceScreenW, s_faceScreenH);
  // Only clear depth in this viewport area (keep scene color intact)
  bgfx::setViewClear(FACE_DIRECT_VIEW, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
  bgfx::setViewFrameBuffer(FACE_DIRECT_VIEW, BGFX_INVALID_HANDLE); // backbuffer
  bgfx::setViewTransform(FACE_DIRECT_VIEW, glm::value_ptr(faceView),
                          glm::value_ptr(faceProj));
  bgfx::touch(FACE_DIRECT_VIEW);

  // Uniforms: simple flat lighting, no fog, no point lights
  s_modelShader->setVec4("u_params", glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
  s_modelShader->setVec4("u_params2", glm::vec4(1.0f, 0, 0, 0));
  s_modelShader->setVec4("u_viewPos", glm::vec4(camP, 0));
  s_modelShader->setVec4("u_lightPos", glm::vec4(20.0f, -300.0f, 100.0f, 0));
  s_modelShader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0));
  s_modelShader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0));
  s_modelShader->setVec4("u_glowColor", glm::vec4(0));
  s_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 0));
  s_modelShader->setVec4("u_texCoordOffset", glm::vec4(0));
  s_modelShader->setVec4("u_fogParams", glm::vec4(0));
  s_modelShader->setVec4("u_fogColor", glm::vec4(0));
  s_modelShader->setVec4("u_shadowParams", glm::vec4(0.0f));
  s_modelShader->uploadPointLights(0, (CSPointLight *)nullptr);

  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                 | BGFX_STATE_DEPTH_TEST_LESS;

  for (auto &mb : s_faceMeshes) {
    if (mb.indexCount == 0)
      continue;
    bgfx::setTransform(glm::value_ptr(model));
    if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
    else bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);
    s_modelShader->setTexture(0, "s_texColor", mb.texture);
    bgfx::setState(state);
    bgfx::submit(FACE_DIRECT_VIEW, s_modelShader->program);
  }
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
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    sm.vbo = bgfx::createDynamicVertexBuffer(count, layout,
                                              BGFX_BUFFER_ALLOW_RESIZE);
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

  // --- Wings (slot 7, category 12, items 0-6) ---
  auto &we = s_slots[slot].wingEquip;
  if (we.category == 12 && we.itemIndex <= 6) {
    int16_t defIdx = (int16_t)we.category * 32 + (int16_t)we.itemIndex;
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
        AABB wingAABB;
        for (auto &mesh : bmd->Meshes) {
          UploadMeshWithBones(mesh, texDirItem, localBones,
                              s_slotRender[slot].wingMeshes, wingAABB, true);
        }
        s_slotRender[slot].wingShadowMeshes = CreateShadowMeshes(bmd.get());
        s_slotRender[slot].wingLocalBones = std::move(localBones);
        // Main 5.2: Wing05/06 (biped, >60 bones) use standard RENDER_TEXTURE —
        // no additive blending. Only standalone JPEG wings (01-04) need additive
        // to hide black backgrounds. TGA wings (Wing07) have proper alpha.
        bool isBipedWingModel = ((int)bmd->Bones.size() > 60);
        if (!isBipedWingModel) {
          for (auto &mb : s_slotRender[slot].wingMeshes) {
            if (!mb.hasAlpha)
              mb.bright = true;
          }
        }
        s_slotRender[slot].wingBmd = std::move(bmd);
        s_slotRender[slot].wingBlendMesh =
            ItemModelManager::GetItemBlendMesh(we.category, we.itemIndex);
        s_slotRender[slot].wingItemIndex = we.itemIndex;
        printf("[CharSelect] Slot %d: wings loaded (%d meshes, blendMesh=%d, biped=%d)\n", slot,
               (int)s_slotRender[slot].wingMeshes.size(),
               s_slotRender[slot].wingBlendMesh, isBipedWingModel);
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
    if (mb.isDynamic && bgfx::isValid(mb.dynVbo)) {
      const bgfx::Memory *mem =
          bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(ViewerVertex)));
      bgfx::update(mb.dynVbo, 0, mem);
    }
  }
}

static void ReskinSlot(int slot) {
  if (!s_slots[slot].occupied || !s_playerSkeleton)
    return;

  int ci = ClassToIndex(s_slots[slot].classCode);
  if (!s_classParts[ci].loaded)
    return;

  // Compute bones with idle↔emote blending
  int act = s_slotRender[slot].action;
  if (act < 0 || act >= (int)s_playerSkeleton->Actions.size())
    act = ACTION_IDLE;

  std::vector<BoneWorldMatrix> bones;
  float blend = s_slotRender[slot].emoteBlend;
  if (blend > 0.001f && blend < 0.999f && act != ACTION_IDLE) {
    // Lerp between idle and emote bone matrices
    auto idleBones = ComputeBoneMatricesInterpolated(
        s_playerSkeleton.get(), ACTION_IDLE, s_slotRender[slot].idleFrame);
    auto emoteBones = ComputeBoneMatricesInterpolated(
        s_playerSkeleton.get(), act, s_slotRender[slot].animFrame);
    bones.resize(idleBones.size());
    for (size_t b = 0; b < bones.size(); ++b) {
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          bones[b][r][c] = idleBones[b][r][c] * (1.0f - blend) +
                           emoteBones[b][r][c] * blend;
    }
  } else {
    int useAct = (blend >= 0.999f) ? act : ACTION_IDLE;
    float useFrame = (blend >= 0.999f) ? s_slotRender[slot].animFrame
                                       : s_slotRender[slot].idleFrame;
    bones = ComputeBoneMatricesInterpolated(s_playerSkeleton.get(), useAct, useFrame);
  }

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

  // Re-skin wings — biped wings (05/06, >60 bones) use player bones directly,
  // standalone wings (01-04, 07) attach to bone 47 with offset.
  if (s_slotRender[slot].wingBmd && !s_slotRender[slot].wingMeshes.empty()) {
    auto *wingBmd = s_slotRender[slot].wingBmd.get();
    float wingFrame = s_slotRender[slot].wingAnimFrame;
    static constexpr int PLAYER_BONE_COUNT = 60;
    bool isBipedWing = ((int)wingBmd->Bones.size() > PLAYER_BONE_COUNT);

    if (isBipedWing) {
      // Hybrid bone approach: biped wing BMDs (75-80 bones) share the player
      // skeleton layout for bones 0-59 and add wing-specific bones at 60+.
      // Bones 0-59: use player's animated bones (prevents body clipping).
      // Bones 60+: compute from wing BMD's own animation, parented to the
      // appropriate bone in wingBones (player bone if parent < 60).
      int wingBoneCount = (int)wingBmd->Bones.size();
      std::vector<BoneWorldMatrix> wingBones(wingBoneCount);

      // Copy player bones for body range (0-59)
      int copyCount = std::min(PLAYER_BONE_COUNT, wingBoneCount);
      for (int bi = 0; bi < copyCount && bi < (int)bones.size(); ++bi) {
        wingBones[bi] = bones[bi];
      }

      // Compute wing-specific bones (60+) from wing animation
      for (int bi = PLAYER_BONE_COUNT; bi < wingBoneCount; ++bi) {
        glm::vec3 pos;
        glm::vec4 q;
        if (!GetInterpolatedBoneData(wingBmd, 0, wingFrame, bi, pos, q))
          continue;
        float quat[4] = {q.x, q.y, q.z, q.w};
        float local[3][4];
        MuMath::QuaternionMatrix(quat, local);
        local[0][3] = pos.x;
        local[1][3] = pos.y;
        local[2][3] = pos.z;

        int parent = wingBmd->Bones[bi].Parent;
        if (parent >= 0 && parent < (int)wingBones.size()) {
          MuMath::ConcatTransforms(
              (const float(*)[4])wingBones[parent].data(), local,
              (float(*)[4])wingBones[bi].data());
        } else {
          memcpy(wingBones[bi].data(), local, sizeof(float) * 12);
        }
      }

      for (int mi = 0; mi < (int)s_slotRender[slot].wingMeshes.size() &&
                       mi < (int)wingBmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(wingBmd->Meshes[mi], wingBones,
                                 s_slotRender[slot].wingMeshes[mi]);
      }
    } else {
      // Standalone wing: recompute animated wing bones each frame
      auto &localBones = s_slotRender[slot].wingLocalBones;
      if (!wingBmd->Actions.empty() &&
          wingBmd->Actions[0].NumAnimationKeys > 1) {
        localBones = ComputeBoneMatricesInterpolated(wingBmd, 0, wingFrame);
      }
      ReskinAttachedItem(bones, wingBmd, localBones,
                         s_slotRender[slot].wingMeshes,
                         glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 15.f));
    }
  }
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
    s_terrain.SetFogColor(glm::vec3(0.04f, 0.04f, 0.04f));
    s_terrain.SetFogRange(1800.0f, 5500.0f);
    s_terrainLoaded = true;
    printf("[CharSelect] Lorencia terrain loaded\n");
  } else {
    printf("[CharSelect] WARNING: Failed to load Lorencia terrain\n");
  }

  // Load Lorencia objects within visible scene radius
  if (s_terrainLoaded && !s_terrainData.objects.empty()) {
    const float CULL_RADIUS = 3500.0f; // Only load objects near the scene
    std::vector<ObjectData> visibleObjects;
    for (auto &obj : s_terrainData.objects) {
      float dx = obj.position.x - SCENE_CX;
      float dz = obj.position.z - SCENE_CZ;
      if (dx * dx + dz * dz < CULL_RADIUS * CULL_RADIUS)
        visibleObjects.push_back(obj);
    }

    s_objectRenderer.Init();
    s_objectRenderer.SetTerrainLightmap(s_terrainData.lightmap);
    s_objectRenderer.SetTerrainMapping(&s_terrainData.mapping);
    s_objectRenderer.SetTerrainHeightmap(s_terrainData.heightmap);
    s_objectRenderer.SetFogEnabled(true);
    s_objectRenderer.SetFogColor(glm::vec3(0.04f, 0.04f, 0.04f));
    s_objectRenderer.SetFogRange(1800.0f, 5500.0f);
    std::string objectDir = s_ctx.dataPath + "/Object1";
    s_objectRenderer.LoadObjects(visibleObjects, objectDir);
    s_objectRenderer.SetLuminosity(CS_LUMINOSITY);
    printf("[CharSelect] Lorencia objects loaded: %d/%d instances, %d models\n",
           s_objectRenderer.GetInstanceCount(),
           (int)s_terrainData.objects.size(),
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
    // Add ambient warm lights near each character slot (simulated lanterns)
    for (int i = 0; i < MAX_SLOTS; i++) {
      auto &sp = SLOT_POSITIONS[i];
      CSPointLight lantern;
      // Warm torch light slightly above and behind each character
      lantern.position = glm::vec3(sp.worldX + FWD_X * 300.0f,
                                    350.0f,
                                    sp.worldZ + FWD_Z * 300.0f);
      lantern.color = glm::vec3(1.4f, 0.85f, 0.4f); // Warm orange torch
      lantern.range = 600.0f;
      lantern.objectType = 50; // fire type
      s_pointLights.push_back(lantern);
    }
    // Background tree-line ambient lights (warm torches behind characters)
    {
      struct TreeLineLight { float offRight, offFwd, height; };
      static constexpr TreeLineLight bgLights[] = {
          {-600.0f, 700.0f, 250.0f},   // Far left behind trees
          {-200.0f, 650.0f, 300.0f},   // Left-center
          { 200.0f, 650.0f, 280.0f},   // Right-center
          { 600.0f, 700.0f, 250.0f},   // Far right behind trees
      };
      for (auto &bg : bgLights) {
        CSPointLight tl;
        tl.position = glm::vec3(
            SCENE_CX + RIGHT_X * bg.offRight + FWD_X * bg.offFwd,
            bg.height,
            SCENE_CZ + RIGHT_Z * bg.offRight + FWD_Z * bg.offFwd);
        tl.color = glm::vec3(1.3f, 0.7f, 0.3f);  // Warm amber torch
        tl.range = 700.0f;
        tl.objectType = 50;  // fire type for flickering
        s_pointLights.push_back(tl);
      }
    }
    if ((int)s_pointLights.size() > CS_MAX_POINT_LIGHTS)
      s_pointLights.resize(CS_MAX_POINT_LIGHTS);
    printf("[CharSelect] Collected %d point lights (incl. slot + tree-line)\n",
           (int)s_pointLights.size());
  }

  // Load grass billboards
  if (s_terrainLoaded) {
    s_grassRenderer.Init();
    s_grassRenderer.Load(s_terrainData, 1, s_ctx.dataPath);
    s_grassRenderer.SetFogColor(glm::vec3(0.04f, 0.04f, 0.04f));
    s_grassRenderer.SetFogRange(1500.0f, 5000.0f);
    printf("[CharSelect] Grass loaded\n");
  }

  // Sky dome (renders behind everything)
  s_sky.Init(s_ctx.dataPath + "/");
  printf("[CharSelect] Sky dome initialized\n");

  // Ambient creatures: birds + falling leaves (Lorencia = map 0)
  s_boidManager.Init(s_ctx.dataPath);
  if (s_terrainLoaded) {
    s_boidManager.SetTerrainData(&s_terrainData);
    s_boidManager.SetTerrainLightmap(s_terrainData.lightmap);
    s_boidManager.SetMapId(0); // Lorencia
    s_boidManager.SetLuminosity(CS_LUMINOSITY);
    // Pass point lights for bird/leaf lighting
    std::vector<PointLight> boidLights;
    for (auto &pl : s_pointLights) {
      PointLight bpl;
      bpl.position = pl.position;
      bpl.color = pl.color;
      bpl.range = pl.range;
      bpl.objectType = pl.objectType;
      boidLights.push_back(bpl);
    }
    s_boidManager.SetPointLights(boidLights);
    printf("[CharSelect] Boid system initialized (birds + leaves)\n");
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
  // Camera pulled back to fit wider character spacing with side margins
  s_camPos = glm::vec3(21743.4f, 667.7f, 13132.7f);
  s_camYaw = 297.3f;
  s_camPitch = -14.8f;

  // Compute target from yaw/pitch
  float yR = glm::radians(s_camYaw), pR = glm::radians(s_camPitch);
  glm::vec3 fwd(cosf(pR) * cosf(yR), sinf(pR), cosf(pR) * sinf(yR));
  s_camTarget = s_camPos + fwd * 1000.0f;
  s_window = ctx.window;

  // Sun position: from camera side (front of characters), high above
  // This makes character shading brighter on front, shadow falls behind
  s_sunLightPos = glm::vec3(SCENE_CX - FWD_X * 2000.0f, 3000.0f,
                             SCENE_CZ - FWD_Z * 2000.0f);

  s_viewMatrix =
      glm::lookAt(s_camPos, s_camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  printf("[CharSelect] Camera: pos=(%.1f, %.1f, %.1f) yaw=%.1f pitch=%.1f\n",
         s_camPos.x, s_camPos.y, s_camPos.z, s_camYaw, s_camPitch);

  // Load model shader (check shaders/ first, fall back to ../shaders/)
  {
    try {
      s_modelShader = Shader::Load("vs_model.bin", "fs_model.bin");
      printf("[CharSelect] Model shader loaded (BGFX)\n");
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load model shader\n");
    }
    try {
      s_shadowShader = Shader::Load("vs_shadow.bin", "fs_shadow.bin");
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load shadow shader\n");
    }
    // Shadow map FBO + depth shader
    try {
      s_depthShader = Shader::Load("vs_depth.bin", "fs_depth.bin");
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load depth shader\n");
    }
    {
      s_shadowColorTex = bgfx::createTexture2D(
          CS_SHADOW_MAP_SIZE, CS_SHADOW_MAP_SIZE, false, 1,
          bgfx::TextureFormat::BGRA8,
          BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
      s_shadowDepthTex = bgfx::createTexture2D(
          CS_SHADOW_MAP_SIZE, CS_SHADOW_MAP_SIZE, false, 1,
          bgfx::TextureFormat::D16, BGFX_TEXTURE_RT_WRITE_ONLY);
      bgfx::TextureHandle atts[] = { s_shadowColorTex, s_shadowDepthTex };
      s_shadowFB = bgfx::createFrameBuffer(2, atts, false);
      if (s_depthShader && bgfx::isValid(s_shadowFB))
        printf("[CharSelect] Shadow map initialized %dx%d\n",
               CS_SHADOW_MAP_SIZE, CS_SHADOW_MAP_SIZE);
    }
  }

  // Selection spotlight — ground glow circle
  {
    try {
      s_spotlightShader = Shader::Load("vs_leaf.bin", "fs_leaf.bin");
    } catch (...) {
      printf("[CharSelect] WARNING: Failed to load spotlight shader (BGFX)\n");
    }
    s_spotlightTex = TextureLoader::LoadOZJ(s_ctx.dataPath + "/Effect/Flare01.OZJ");

    // Build cylinder mesh: same radius top and bottom
    // Vertex layout: pos(x,y,z) + uv(u,v) = 5 floats
    // Vertices 0..SEGS-1 = top ring, SEGS..2*SEGS-1 = bottom ring
    std::vector<float> verts;
    std::vector<unsigned int> indices;

    // Top ring — UV mapped to flare center (bright)
    for (int i = 0; i < CONE_SEGMENTS; ++i) {
      float a = (float)i / CONE_SEGMENTS * 2.0f * 3.14159f;
      float cx = cosf(a), cz = sinf(a);
      verts.insert(verts.end(), {
        cx * CONE_RADIUS, CONE_HEIGHT, cz * CONE_RADIUS,
        0.5f + cx * 0.15f, 0.5f + cz * 0.15f  // Near center = bright
      });
    }

    // Bottom ring — UV mapped to flare edge (fades out)
    for (int i = 0; i < CONE_SEGMENTS; ++i) {
      float a = (float)i / CONE_SEGMENTS * 2.0f * 3.14159f;
      float cx = cosf(a), cz = sinf(a);
      verts.insert(verts.end(), {
        cx * CONE_RADIUS, 0.0f, cz * CONE_RADIUS,
        0.5f + cx * 0.48f, 0.5f + cz * 0.48f  // Edge = dim
      });
    }

    // Side quads (two triangles each): top[i]→bottom[i]→bottom[next], top[i]→bottom[next]→top[next]
    for (int i = 0; i < CONE_SEGMENTS; ++i) {
      int next = (i + 1) % CONE_SEGMENTS;
      unsigned t0 = i, t1 = next;                          // Top ring
      unsigned b0 = CONE_SEGMENTS + i, b1 = CONE_SEGMENTS + next; // Bottom ring
      indices.insert(indices.end(), {t0, b0, b1});
      indices.insert(indices.end(), {t0, b1, t1});
    }

    // Top cap: triangle fan
    int topCenter = (int)(verts.size() / 5);
    verts.insert(verts.end(), {0.0f, CONE_HEIGHT, 0.0f,  0.5f, 0.5f});
    for (int i = 0; i < CONE_SEGMENTS; ++i) {
      int next = (i + 1) % CONE_SEGMENTS;
      indices.insert(indices.end(), {(unsigned)topCenter, (unsigned)i, (unsigned)next});
    }

    // Bottom cap: triangle fan
    int botCenter = (int)(verts.size() / 5);
    verts.insert(verts.end(), {0.0f, 0.5f, 0.0f,  0.5f, 0.5f});
    for (int i = 0; i < CONE_SEGMENTS; ++i) {
      int next = (i + 1) % CONE_SEGMENTS;
      indices.insert(indices.end(), {(unsigned)botCenter, (unsigned)(CONE_SEGMENTS + next), (unsigned)(CONE_SEGMENTS + i)});
    }

    s_spotlightIndexCount = (int)indices.size();

    bgfx::VertexLayout spotLayout;
    spotLayout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
    s_spotVBO = bgfx::createVertexBuffer(
      bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(float))),
      spotLayout);
    s_spotEBO = bgfx::createIndexBuffer(
      bgfx::copy(indices.data(), (uint32_t)(indices.size() * sizeof(uint32_t))),
      BGFX_BUFFER_INDEX32);
    printf("[CharSelect] Spotlight cylinder initialized (%d segments, h=%.0f, r=%.0f)\n",
           CONE_SEGMENTS, CONE_HEIGHT, CONE_RADIUS);
  }

  // Load Player.bmd skeleton + class body parts (same as main game)
  LoadPlayerModels();

  // Load NewFace BMD models for character creation portrait
  LoadFaceModels();
  SetupFaceFBO();

  printf("[CharSelect] Initialized\n");
}

void Shutdown() {
  // Spotlight cleanup
  if (bgfx::isValid(s_spotVBO)) { bgfx::destroy(s_spotVBO); s_spotVBO = BGFX_INVALID_HANDLE; }
  if (bgfx::isValid(s_spotEBO)) { bgfx::destroy(s_spotEBO); s_spotEBO = BGFX_INVALID_HANDLE; }
  if (TexValid(s_spotlightTex)) { TexDestroy(s_spotlightTex); s_spotlightTex = kInvalidTex; }
  s_spotlightShader.reset();

  s_boidManager.Cleanup();
  s_sky.Cleanup();
  s_grassRenderer.Cleanup();
  s_objectRenderer.Cleanup();
  for (int i = 0; i < MAX_SLOTS; i++) {
    for (int p = 0; p < PART_COUNT; p++) {
      CleanupMeshBuffers(s_slotRender[i].meshes[p]);
      for (auto &sm : s_slotRender[i].shadowMeshes[p]) {
        if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
      }
      s_slotRender[i].shadowMeshes[p].clear();
      s_slotRender[i].partOverrideBmd[p].reset();
    }
    // Weapon cleanup
    CleanupMeshBuffers(s_slotRender[i].weaponMeshes);
    for (auto &sm : s_slotRender[i].weaponShadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    s_slotRender[i].weaponShadowMeshes.clear();
    s_slotRender[i].weaponLocalBones.clear();
    s_slotRender[i].weaponBmd.reset();
    // Base head cleanup
    CleanupMeshBuffers(s_slotRender[i].baseHeadMeshes);
    for (auto &sm : s_slotRender[i].baseHeadShadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    s_slotRender[i].baseHeadShadowMeshes.clear();
    s_slotRender[i].showBaseHead = false;
    // Shield cleanup
    CleanupMeshBuffers(s_slotRender[i].shieldMeshes);
    for (auto &sm : s_slotRender[i].shieldShadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    s_slotRender[i].shieldShadowMeshes.clear();
    s_slotRender[i].shieldLocalBones.clear();
    s_slotRender[i].shieldBmd.reset();
    // Wing cleanup
    CleanupMeshBuffers(s_slotRender[i].wingMeshes);
    for (auto &sm : s_slotRender[i].wingShadowMeshes) {
      if (bgfx::isValid(sm.vbo)) bgfx::destroy(sm.vbo);
    }
    s_slotRender[i].wingShadowMeshes.clear();
    s_slotRender[i].wingLocalBones.clear();
    s_slotRender[i].wingBmd.reset();
  }
  s_playerSkeleton.reset();
  for (int i = 0; i < 4; i++)
    for (int p = 0; p < PART_COUNT; p++)
      s_classParts[i].bmd[p].reset();
  s_modelShader.reset();
  s_shadowShader.reset();
  s_depthShader.reset();
  if (bgfx::isValid(s_shadowFB)) bgfx::destroy(s_shadowFB);
  s_shadowFB = BGFX_INVALID_HANDLE;
  if (bgfx::isValid(s_shadowColorTex)) bgfx::destroy(s_shadowColorTex);
  s_shadowColorTex = BGFX_INVALID_HANDLE;
  if (bgfx::isValid(s_shadowDepthTex)) bgfx::destroy(s_shadowDepthTex);
  s_shadowDepthTex = BGFX_INVALID_HANDLE;

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
    CleanupMeshBuffers(s_slotRender[i].wingMeshes);
    s_slotRender[i].wingBmd.reset();
    s_slotRender[i].wingLocalBones.clear();
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
  const float animOffsets[MAX_SLOTS] = {0.0f, 7.3f, 14.1f, 4.8f};
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

  // Animate character models with idle↔emote blending
  if (s_playerSkeleton && (int)s_playerSkeleton->Actions.size() > 218) {
    float idleMax = (float)s_playerSkeleton->Actions[ACTION_IDLE].NumAnimationKeys;
    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied)
        continue;

      // Always advance idle frame (for blending back smoothly)
      s_slotRender[i].idleFrame += dt * 5.0f;
      if (s_slotRender[i].idleFrame >= idleMax)
        s_slotRender[i].idleFrame -= idleMax;

      if (s_slotRender[i].emotePlaying) {
        // Blend in: ramp emoteBlend toward 1.0
        s_slotRender[i].emoteBlend = std::min(1.0f, s_slotRender[i].emoteBlend + dt * 4.0f);

        // Advance emote frame
        int act = s_slotRender[i].action;
        if (act >= 0 && act < (int)s_playerSkeleton->Actions.size()) {
          float emoteMax = (float)s_playerSkeleton->Actions[act].NumAnimationKeys;
          s_slotRender[i].animFrame += dt * 8.0f;
          if (s_slotRender[i].animFrame >= emoteMax) {
            // Emote finished — start blending out
            s_slotRender[i].emotePlaying = false;
          }
        }
      } else if (s_slotRender[i].emoteBlend > 0.0f) {
        // Blend out: ramp emoteBlend back to 0
        s_slotRender[i].emoteBlend = std::max(0.0f, s_slotRender[i].emoteBlend - dt * 3.0f);
        if (s_slotRender[i].emoteBlend <= 0.0f) {
          s_slotRender[i].action = ACTION_IDLE;
          s_slotRender[i].animFrame = s_slotRender[i].idleFrame;
        }
      } else {
        // Pure idle
        s_slotRender[i].animFrame = s_slotRender[i].idleFrame;
      }

      // Advance wing animation frame (PlaySpeed 0.25 * 25fps tick rate)
      if (s_slotRender[i].wingBmd && !s_slotRender[i].wingBmd->Actions.empty()) {
        int wingKeys = s_slotRender[i].wingBmd->Actions[0].NumAnimationKeys;
        if (wingKeys > 1) {
          s_slotRender[i].wingAnimFrame += dt * 0.25f * 25.0f;
          if (s_slotRender[i].wingAnimFrame >= (float)wingKeys)
            s_slotRender[i].wingAnimFrame =
                std::fmod(s_slotRender[i].wingAnimFrame, (float)wingKeys);
        }
      }

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

  // Update ambient creatures (birds, falling leaves)
  // Use scene center as "hero position" for boid spawning reference
  glm::vec3 boidCenter(SCENE_CX, 0.0f, SCENE_CZ);
  s_boidManager.Update(dt, boidCenter, 0, s_time);
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

  {
    static int frameCount = 0;
    frameCount++;
    if (frameCount == 1) {
      std::cout << "[CharSelect] Render() first frame (" << windowWidth << "x"
                << windowHeight << ")" << std::endl;
    }
  }

  // Setup projection
  float aspect = (float)windowWidth / (float)std::max(windowHeight, 1);
  s_projMatrix = glm::perspective(glm::radians(35.0f), aspect, 10.0f, 50000.0f);

  // Camera is locked — no controls

  glm::vec3 camPos = s_camPos;
  s_viewMatrix = glm::lookAt(camPos, s_camTarget, glm::vec3(0.0f, 1.0f, 0.0f));

  // ── BGFX rendering path ──
  // Ensure shadow view (8) renders before main view (0)
  bgfx::ViewId order[] = { CS_SHADOW_VIEW, 0 };
  bgfx::setViewOrder(0, 2, order);

  bgfx::setViewTransform(0, glm::value_ptr(s_viewMatrix),
                         glm::value_ptr(s_projMatrix));

  // ── Shadow map pass: render character depth from directional light ──
  if (s_depthShader && bgfx::isValid(s_shadowFB) && s_playerSkeleton) {
    // Light direction matching sun position (diagonal from front-above)
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.15f));
    float sceneCenterY = s_terrainLoaded ? s_terrain.GetHeight(SCENE_CX, SCENE_CZ) : 0.0f;
    glm::vec3 sceneCenter(SCENE_CX, sceneCenterY, SCENE_CZ);
    glm::vec3 lightPos = sceneCenter - lightDir * 2000.0f;
    glm::vec3 up(0.0f, 0.0f, 1.0f);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f) up = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);
    // Metal uses [0,1] Z clip range; OpenGL uses [-1,1]
    glm::mat4 lightProj = bgfx::getCaps()->homogeneousDepth
      ? glm::ortho(-1500.0f, 1500.0f, -1500.0f, 1500.0f, 100.0f, 5000.0f)
      : glm::orthoRH_ZO(-1500.0f, 1500.0f, -1500.0f, 1500.0f, 100.0f, 5000.0f);
    s_lightMtx = lightProj * lightView;

    bgfx::setViewName(CS_SHADOW_VIEW, "CS_ShadowMap");
    bgfx::setViewRect(CS_SHADOW_VIEW, 0, 0, CS_SHADOW_MAP_SIZE, CS_SHADOW_MAP_SIZE);
    bgfx::setViewClear(CS_SHADOW_VIEW, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFFFFFFFF, 1.0f, 0);
    bgfx::setViewFrameBuffer(CS_SHADOW_VIEW, s_shadowFB);
    bgfx::setViewTransform(CS_SHADOW_VIEW, glm::value_ptr(lightView),
                            glm::value_ptr(lightProj));
    bgfx::touch(CS_SHADOW_VIEW);

    // No culling: body part meshes are open (non-closed), culling may discard geometry
    uint64_t depthState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                        | BGFX_STATE_DEPTH_TEST_LESS;

    int shadowDraws = 0;
    // Submit all character slot meshes to shadow map
    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied) continue;
      int ci = ClassToIndex(s_slots[i].classCode);
      if (!s_classParts[ci].loaded) continue;
      auto &sp = SLOT_POSITIONS[i];
      float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ) : 0.0f;
      float facing = sp.facingDeg * (3.14159f / 180.0f);

      glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                         glm::vec3(sp.worldX, slotY, sp.worldZ));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, facing, glm::vec3(0, 0, 1));

      auto submitMeshes = [&](const std::vector<MeshBuffers> &meshes) {
        for (auto &mb : meshes) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          if (mb.isDynamic && !bgfx::isValid(mb.dynVbo)) continue;
          if (!mb.isDynamic && !bgfx::isValid(mb.vbo)) continue;
          bgfx::setTransform(glm::value_ptr(model));
          if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
          else bgfx::setVertexBuffer(0, mb.vbo);
          bgfx::setIndexBuffer(mb.ebo);
          bgfx::setState(depthState);
          bgfx::submit(CS_SHADOW_VIEW, s_depthShader->program);
          shadowDraws++;
        }
      };

      // Body parts
      for (int p = 0; p < PART_COUNT; p++)
        submitMeshes(s_slotRender[i].meshes[p]);
      // Base head
      if (s_slotRender[i].showBaseHead)
        submitMeshes(s_slotRender[i].baseHeadMeshes);
      // Weapon + shield
      submitMeshes(s_slotRender[i].weaponMeshes);
      submitMeshes(s_slotRender[i].shieldMeshes);
    }

    // One-time debug log
    { static bool logged = false;
      if (!logged && shadowDraws > 0) {
        printf("[CharSelect] Shadow pass: %d draws, center=(%.0f,%.1f,%.0f)\n",
               shadowDraws, sceneCenter.x, sceneCenter.y, sceneCenter.z);
        logged = true;
      }
    }

    // Pass shadow map to terrain
    s_terrain.SetShadowMap(s_shadowColorTex, s_lightMtx);
    s_terrain.SetShadowDebug(false);
  }

  // Environment (subsystem calls — already BGFX-aware)
  s_sky.Render(s_viewMatrix, s_projMatrix, camPos, CS_LUMINOSITY);
  if (s_terrainLoaded)
    s_terrain.Render(s_viewMatrix, s_projMatrix, s_time, camPos);
  if (s_objectRenderer.GetInstanceCount() > 0)
    s_objectRenderer.Render(s_viewMatrix, s_projMatrix, camPos, s_time);
  s_grassRenderer.Render(s_viewMatrix, s_projMatrix, s_time, camPos);
  s_boidManager.Render(s_viewMatrix, s_projMatrix, camPos);
  s_boidManager.RenderLeaves(s_viewMatrix, s_projMatrix);

  // Shadows are now handled by shadow mapping (depth pass on view 8, set up above).

  // ── Spotlight ──
  if (s_spotlightShader && TexValid(s_spotlightTex) && bgfx::isValid(s_spotVBO) &&
      s_selectedSlot >= 0 && s_slots[s_selectedSlot].occupied && !s_createOpen) {
    auto &sp = SLOT_POSITIONS[s_selectedSlot];
    float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ) : 0.0f;

    glm::vec3 camRight(s_viewMatrix[0][0], s_viewMatrix[1][0], s_viewMatrix[2][0]);
    glm::vec3 camUp(s_viewMatrix[0][1], s_viewMatrix[1][1], s_viewMatrix[2][1]);
    glm::vec3 camFwd(s_viewMatrix[0][2], s_viewMatrix[1][2], s_viewMatrix[2][2]);
    glm::mat4 orient(1.0f);
    orient[0] = glm::vec4(camRight, 0.0f);
    orient[1] = glm::vec4(camUp, 0.0f);
    orient[2] = glm::vec4(camFwd, 0.0f);

    glm::mat4 spotModel = glm::translate(glm::mat4(1.0f),
                                          glm::vec3(sp.worldX, slotY, sp.worldZ));
    spotModel = spotModel * orient;

    uint64_t spotState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD;

    // Outer cylinder (subtle beam)
    s_spotlightShader->setVec4("u_leafAlpha", glm::vec4(0.25f, 0, 0, 0));
    bgfx::setTransform(glm::value_ptr(spotModel));
    bgfx::setVertexBuffer(0, s_spotVBO);
    bgfx::setIndexBuffer(s_spotEBO);
    s_spotlightShader->setTexture(0, "s_texColor", s_spotlightTex);
    bgfx::setState(spotState);
    bgfx::submit(0, s_spotlightShader->program);

    // Inner cylinder (brighter core, scaled 0.6)
    glm::mat4 innerModel = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(sp.worldX, slotY, sp.worldZ));
    innerModel = innerModel * orient;
    innerModel = glm::scale(innerModel, glm::vec3(0.6f, 1.0f, 0.6f));
    s_spotlightShader->setVec4("u_leafAlpha", glm::vec4(0.12f, 0, 0, 0));
    bgfx::setTransform(glm::value_ptr(innerModel));
    bgfx::setVertexBuffer(0, s_spotVBO);
    bgfx::setIndexBuffer(s_spotEBO);
    s_spotlightShader->setTexture(0, "s_texColor", s_spotlightTex);
    bgfx::setState(spotState);
    bgfx::submit(0, s_spotlightShader->program);
  }

  // ── Character models ──
  if (s_modelShader) {
    int basePL = (int)s_pointLights.size();

    uint64_t stAlpha = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                     | BGFX_STATE_DEPTH_TEST_LESS
                     | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                             BGFX_STATE_BLEND_INV_SRC_ALPHA);
    uint64_t stAdd = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ADD;

    for (int i = 0; i < MAX_SLOTS; i++) {
      if (!s_slots[i].occupied) continue;
      auto &sp = SLOT_POSITIONS[i];
      float facing = sp.facingDeg * (3.14159f / 180.0f);
      float slotY = s_terrainLoaded ? s_terrain.GetHeight(sp.worldX, sp.worldZ) : 0.0f;

      bool isSelected = (i == s_selectedSlot);

      // Point lights + spotlight for selected character
      if (isSelected && basePL < CS_MAX_POINT_LIGHTS - 2) {
        CSPointLight spot;
        spot.position = glm::vec3(sp.worldX, slotY + 200.0f, sp.worldZ);
        spot.color = glm::vec3(1.4f, 1.3f, 1.1f);
        spot.range = 500.0f;
        spot.objectType = 0;
        s_pointLights.push_back(spot);
        CSPointLight rim;
        rim.position = glm::vec3(sp.worldX + FWD_X * 120.0f, slotY + 120.0f,
                                  sp.worldZ + FWD_Z * 120.0f);
        rim.color = glm::vec3(0.5f, 0.6f, 0.8f);
        rim.range = 300.0f;
        rim.objectType = 0;
        s_pointLights.push_back(rim);
      }
      int plCount = (int)s_pointLights.size();

      glm::vec3 tLight = SampleTerrainLight(sp.worldX, sp.worldZ);
      if (isSelected) tLight = glm::max(tLight, glm::vec3(0.4f));
      float brightness = isSelected ? 1.5f : 0.55f;
      glm::vec3 lightColor = CS_SUN_COLOR * brightness;

      glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                        glm::vec3(sp.worldX, slotY, sp.worldZ));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 0, 1));
      model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(0, 1, 0));
      model = glm::rotate(model, facing, glm::vec3(0, 0, 1));

      // Uniform setter lambda
      auto setCSUniforms = [&](float bml, float chromeMode, float chromeTime,
                               const glm::vec3 &glowColor) {
        s_modelShader->setVec4("u_params", glm::vec4(1.0f, bml, chromeMode, chromeTime));
        s_modelShader->setVec4("u_params2", glm::vec4(CS_LUMINOSITY, 0, 0, 0));
        s_modelShader->setVec4("u_viewPos", glm::vec4(camPos, 0));
        s_modelShader->setVec4("u_lightPos", glm::vec4(s_sunLightPos, 0));
        s_modelShader->setVec4("u_lightColor", glm::vec4(lightColor, 0));
        s_modelShader->setVec4("u_terrainLight", glm::vec4(tLight, 0));
        s_modelShader->setVec4("u_glowColor", glm::vec4(glowColor, 0));
        s_modelShader->setVec4("u_baseTint", glm::vec4(1, 1, 1, 0));
        s_modelShader->setVec4("u_texCoordOffset", glm::vec4(0));
        s_modelShader->setVec4("u_fogParams", glm::vec4(0, 0, 0, 0));
        s_modelShader->setVec4("u_fogColor", glm::vec4(0));
        // Shadow map
        float shadowOn = bgfx::isValid(s_shadowColorTex) ? 1.0f : 0.0f;
        s_modelShader->setVec4("u_shadowParams", glm::vec4(shadowOn, 0, 0, 0));
        if (shadowOn > 0.5f)
          s_modelShader->setMat4("u_lightMtx", s_lightMtx);
        s_modelShader->uploadPointLights(plCount, s_pointLights.data());
      };

      // Draw helper lambda
      auto bgfxDraw = [&](MeshBuffers &mb, uint64_t state) {
        bgfx::setTransform(glm::value_ptr(model));
        if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
        else bgfx::setVertexBuffer(0, mb.vbo);
        bgfx::setIndexBuffer(mb.ebo);
        s_modelShader->setTexture(0, "s_texColor", mb.texture);
        if (bgfx::isValid(s_shadowColorTex))
          s_modelShader->setTexture(1, "s_shadowMap", s_shadowColorTex);
        bgfx::setState(state);
        bgfx::submit(0, s_modelShader->program);
      };

      // Body parts
      for (int p = 0; p < PART_COUNT; p++) {
        for (auto &mb : s_slotRender[i].meshes[p]) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          setCSUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
          bgfxDraw(mb, mb.bright ? stAdd : stAlpha);
        }
      }
      // Base head
      if (s_slotRender[i].showBaseHead) {
        for (auto &mb : s_slotRender[i].baseHeadMeshes) {
          if (mb.indexCount == 0 || mb.hidden) continue;
          setCSUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
          bgfxDraw(mb, stAlpha);
        }
      }

      // Weapon
      {
        int blendIdx = s_slotRender[i].weaponBlendMesh;
        int mi = 0;
        for (auto &mb : s_slotRender[i].weaponMeshes) {
          if (mb.indexCount == 0) { mi++; continue; }
          if (blendIdx >= 0 && mi == blendIdx) {
            float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
            setCSUniforms(pulseLight, 0.0f, 0.0f, glm::vec3(0.0f));
            bgfxDraw(mb, stAdd);
          } else {
            setCSUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
            bgfxDraw(mb, stAlpha);
          }
          mi++;
        }
      }

      // Shield
      {
        int blendIdx = s_slotRender[i].shieldBlendMesh;
        int mi = 0;
        for (auto &mb : s_slotRender[i].shieldMeshes) {
          if (mb.indexCount == 0) { mi++; continue; }
          if (blendIdx >= 0 && mi == blendIdx) {
            float pulseLight = sinf((float)glfwGetTime() * 4.0f) * 0.3f + 0.7f;
            setCSUniforms(pulseLight, 0.0f, 0.0f, glm::vec3(0.0f));
            bgfxDraw(mb, stAdd);
          } else {
            setCSUniforms(1.0f, 0.0f, 0.0f, glm::vec3(0.0f));
            bgfxDraw(mb, stAlpha);
          }
          mi++;
        }
      }

      // Wings — flat white lighting via glowColor
      {
        for (auto &mb : s_slotRender[i].wingMeshes) {
          if (mb.indexCount == 0) continue;
          setCSUniforms(1.0f, 0.0f, 0.0f, glm::vec3(1.0f));
          bgfxDraw(mb, mb.bright ? stAdd : stAlpha);
        }
      }

      // ChromeGlow (+7/+9/+11/+13 item glow)
      {
        bool anyGlow = false;
        for (int p = 0; p < PART_COUNT; p++) {
          if (s_slots[i].equip[2 + p].itemLevel >= 7) { anyGlow = true; break; }
        }
        if (s_slots[i].equip[0].itemLevel >= 7) anyGlow = true;

        if (anyGlow && TexValid(ChromeGlow::GetTextures().chrome1)) {
          float t = (float)glfwGetTime();

          // Armor body parts glow
          for (int p = 0; p < PART_COUNT; p++) {
            uint8_t lvl = s_slots[i].equip[2 + p].itemLevel;
            if (lvl < 7) continue;
            ChromeGlow::GlowPass passes[3];
            int n = ChromeGlow::GetGlowPasses(lvl, 7 + p,
                                               s_slots[i].equip[2 + p].itemIndex, passes);
            for (int gp = 0; gp < n; ++gp) {
              setCSUniforms(1.0f, (float)passes[gp].chromeMode, t, passes[gp].color);
              for (auto &mb : s_slotRender[i].meshes[p]) {
                if (mb.indexCount == 0 || mb.hidden) continue;
                s_modelShader->setTexture(0, "s_texColor", passes[gp].texture);
                bgfx::setTransform(glm::value_ptr(model));
                if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
                else bgfx::setVertexBuffer(0, mb.vbo);
                bgfx::setIndexBuffer(mb.ebo);
                bgfx::setState(stAdd);
                bgfx::submit(0, s_modelShader->program);
              }
            }
          }

          // Weapon glow
          uint8_t wlv = s_slots[i].equip[0].itemLevel;
          if (wlv >= 7) {
            ChromeGlow::GlowPass passes[3];
            int n = ChromeGlow::GetGlowPasses(wlv, s_slots[i].equip[0].category,
                                               s_slots[i].equip[0].itemIndex, passes);
            int wBlend = s_slotRender[i].weaponBlendMesh;
            for (int gp = 0; gp < n; ++gp) {
              float wScale = 0.7f / (float)n;
              setCSUniforms(1.0f, (float)passes[gp].chromeMode, t,
                            passes[gp].color * wScale);
              int wmi = 0;
              for (auto &mb : s_slotRender[i].weaponMeshes) {
                if (mb.indexCount == 0 || (wBlend >= 0 && wmi == wBlend)) { wmi++; continue; }
                s_modelShader->setTexture(0, "s_texColor", passes[gp].texture);
                bgfx::setTransform(glm::value_ptr(model));
                if (mb.isDynamic) bgfx::setVertexBuffer(0, mb.dynVbo);
                else bgfx::setVertexBuffer(0, mb.vbo);
                bgfx::setIndexBuffer(mb.ebo);
                bgfx::setState(stAdd);
                bgfx::submit(0, s_modelShader->program);
                wmi++;
              }
            }
          }
        }
      }

      // Pop temp point lights
      if (isSelected && basePL < CS_MAX_POINT_LIGHTS - 2) {
        s_pointLights.pop_back();
        s_pointLights.pop_back();
      }
    }
  }

  // Render face portrait directly to backbuffer (if create window is open)
  if (s_createOpen && s_faceLoadedClass >= 0) {
    int wantIdx = ClassToIndex(s_createClass);
    if (wantIdx != s_faceLoadedClass) {
      RebuildFaceMeshes(wantIdx);
    }
    RenderFaceToBackbuffer();
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

  // Camera HUD removed — position locked

  // Title (hidden during character creation)
  if (!s_createOpen) {
    const char *title = "MU Online Remaster";
    ImVec2 tsz = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(cx - tsz.x * 0.5f, 30), IM_COL32(220, 200, 160, 255),
                title);
  }

  // Character name plates (floating labels above each character, hidden during creation)
  for (int i = 0; i < MAX_SLOTS && !s_createOpen; i++) {
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

  // Left buttons: [Create] [Exit]
  ImGui::SetNextWindowPos(ImVec2(10, btnY - 5));
  ImGui::SetNextWindowSize(ImVec2(btnW * 2 + btnGap + 20, btnH + 10));
  ImGui::Begin("##CSBtnLeft", nullptr, kBtnFlags);
  if (ImGui::Button("Create", ImVec2(btnW, btnH))) {
    SoundManager::Play(SOUND_CLICK01);
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
      SoundManager::Play(SOUND_ERROR01);
      snprintf(s_statusMsg, sizeof(s_statusMsg), "Maximum 5 characters");
      s_statusTimer = 2.0f;
    }
  }
  ImGui::SameLine(0, btnGap);
  if (ImGui::Button("Exit", ImVec2(btnW, btnH))) {
    SoundManager::Play(SOUND_CLICK01);
    if (s_ctx.onExit)
      s_ctx.onExit();
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
      SoundManager::Play(SOUND_MENU01);
      if (s_ctx.server) {
        s_ctx.server->SendCharSelect(s_slots[s_selectedSlot].name);
        if (s_ctx.onCharSelected)
          s_ctx.onCharSelected();
      }
    }
    ImGui::SameLine(0, btnGap);
    if (ImGui::Button("Delete", ImVec2(btnW, btnH))) {
      SoundManager::Play(SOUND_CLICK01);
      s_deleteConfirm = true;
    }
    ImGui::End();
  }

  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
  } // end if (!s_createOpen) bottom bar

  // ── Character slot click areas (invisible buttons over each slot) ──
  // We handle this via OnMouseClick instead of ImGui

  // ── Create character panel (original layout with direct backbuffer face) ──
  if (!s_createOpen) s_faceScreenValid = false;
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
    float uiScale = std::min(W / 640.0f, H / 480.0f);
    uiScale = std::max(uiScale, 1.0f);
    float panelW = 454.0f * uiScale;
    panelW = std::min(panelW, W * 0.85f);

    float formH = 90.0f * uiScale;
    float statOverlayW = panelW * (130.0f / 454.0f);
    float statOverlayX = 0.0f;

    // Layout: model container height then form below
    float modelAreaW = panelW - statOverlayW;
    float modelH = H * 0.50f;
    float panelH = modelH + formH;
    panelH = std::min(panelH, H * 0.92f);
    modelH = panelH - formH;

    float panelX = (W - panelW) * 0.5f;
    float panelY = (H - panelH) * 0.45f;
    statOverlayX = panelX + panelW - statOverlayW;
    float formY = panelY + modelH;

    // Face viewport — shifted left so face is more on the left side
    float faceX = panelX - panelW * 0.14f;
    float faceY = panelY;
    float faceDispW = panelW;
    float faceDispH = modelH;

    // Compute face screen rect for direct backbuffer rendering (BGFX)
    {
      ImGuiIO &io = ImGui::GetIO();
      float fbScaleX = io.DisplayFramebufferScale.x;
      float fbScaleY = io.DisplayFramebufferScale.y;
      s_faceScreenX = (uint16_t)(faceX * fbScaleX);
      s_faceScreenY = (uint16_t)(faceY * fbScaleY);
      s_faceScreenW = (uint16_t)(faceDispW * fbScaleX);
      s_faceScreenH = (uint16_t)(faceDispH * fbScaleY);
      s_faceScreenValid = true;
    }

    // ImGui window for interactive controls (NoBackground — scene shows through)
    ImGui::SetNextWindowPos(ImVec2(panelX - 5, panelY - 5));
    ImGui::SetNextWindowSize(ImVec2(panelW + 10, panelH + 10));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##CreatePanel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);

    ImDrawList *cdl = ImGui::GetWindowDrawList();

    // ── Model container — full background with opacity, face renders on top (view 31) ──
    cdl->AddRectFilled(ImVec2(panelX, panelY),
                       ImVec2(panelX + panelW, formY),
                       IM_COL32(10, 10, 20, 160), 2.0f);
    cdl->AddRect(ImVec2(panelX, panelY),
                 ImVec2(panelX + panelW, formY),
                 IM_COL32(80, 70, 45, 100), 2.0f);

    // ── Stats panel (overlaid, bottom-right of model zone) ──
    float cbH_pre = 26.0f * uiScale;
    float cbTotalH = 4.5f * cbH_pre;
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

    // ── Class selection buttons (overlaid, right side) ──
    {
      float cbAbsX = statOverlayX;
      float cbAbsY = formY - bottomMargin - cbTotalH;
      float cbW = statOverlayW - 4 * uiScale;
      float cbH = cbH_pre;

      float cbX = cbAbsX - (panelX - 5);
      float cbY = cbAbsY - (panelY - 5);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.04f, 0.07f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.13f, 0.10f, 0.92f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.22f, 0.14f, 0.55f));

      float yAccum = 0.0f;
      for (int i = 0; i < numClasses; i++) {
        if (i == 3) yAccum += cbH * 0.5f;
        ImGui::SetCursorPos(ImVec2(cbX, cbY + yAccum));

        bool isSelected = (classCodes[i] == s_createClass);
        if (isSelected) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.14f, 0.08f, 0.92f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.55f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.70f, 0.63f, 0.35f, 0.8f));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.04f, 0.07f, 0.85f));
          ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.22f, 0.14f, 0.55f));
        }

        bool enabled = true; // all classes selectable
        if (!enabled) ImGui::BeginDisabled();
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "%s##cls%d", classNames[i], i);
        if (ImGui::Button(btnId, ImVec2(cbW, cbH))) {
          SoundManager::Play(SOUND_CLICK01);
          s_createClass = classCodes[i];
        }
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        yAccum += cbH;
      }
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(2);
    }

    // ── Form container background (below model) ──
    cdl->AddRectFilled(ImVec2(panelX, formY),
                       ImVec2(panelX + panelW, panelY + panelH),
                       IM_COL32(10, 10, 20, 200), 0.0f);
    cdl->AddRect(ImVec2(panelX, formY),
                 ImVec2(panelX + panelW, panelY + panelH),
                 IM_COL32(80, 70, 45, 120));
    cdl->AddLine(ImVec2(panelX + 3, formY),
                 ImVec2(panelX + panelW - 3, formY),
                 IM_COL32(80, 70, 45, 160), 1.0f);

    // ── Class description ──
    {
      const char *classDescs[] = {
          "Master of the arcane arts. Wields\npowerful magic fueled by Energy\nto devastate foes from afar.",
          "A fearless warrior clad in heavy\narmor. Uses Strength and AG to\ndeliver devastating melee strikes.",
          "A versatile archer and healer.\nSupports allies with healing magic\nand strikes from range.",
          "Combines swordplay and sorcery.\nDraws on both Strength and Energy\nfor a versatile fighting style."};
      float descY = formY + 6 * uiScale;
      float descX = panelX + 10 * uiScale;
      cdl->AddText(ImVec2(descX + 1, descY + 1), IM_COL32(0,0,0,180), classDescs[classIdx]);
      cdl->AddText(ImVec2(descX, descY), IM_COL32(190,185,170,220), classDescs[classIdx]);
    }

    // ── Name input + Create/Back buttons (bottom row) ──
    {
      float rowAbsY = panelY + panelH - 34 * uiScale;
      float nameAbsX = panelX + 10 * uiScale;
      float nameW = panelW * 0.42f;
      float nameX = nameAbsX - (panelX - 5);
      float nameY = rowAbsY - (panelY - 5);

      ImGui::SetCursorPos(ImVec2(nameX, nameY));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.04f, 0.02f, 0.9f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.09f, 0.05f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.45f, 0.38f, 0.22f, 0.7f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::SetNextItemWidth(nameW);
      ImGui::InputTextWithHint("##createName", "Character name (4-10)", s_createName, 11);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);

      float createW = 110.0f * uiScale;
      float backW = 60.0f * uiScale;
      float obH = 26.0f * uiScale;
      float obGap = 6.0f * uiScale;
      float createAbsX = panelX + panelW - createW - backW - obGap - 10 * uiScale;
      float createX = createAbsX - (panelX - 5);

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.12f, 0.06f, 0.92f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.08f, 0.95f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.24f, 0.10f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.48f, 0.25f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.55f, 1.0f));
      ImGui::SetCursorPos(ImVec2(createX, nameY));
      if (ImGui::Button("Create Character", ImVec2(createW, obH))) {
        SoundManager::Play(SOUND_MENU01);
        int nameLen = (int)strlen(s_createName);
        if (nameLen >= 4 && nameLen <= 10) {
          s_ctx.server->SendCharCreate(s_createName, s_createClass);
        } else {
          SoundManager::Play(SOUND_ERROR01);
          snprintf(s_statusMsg, sizeof(s_statusMsg), "Name must be 4-10 characters");
          s_statusTimer = 2.0f;
        }
      }
      ImGui::PopStyleColor(5);

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.13f, 0.10f, 0.95f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.18f, 0.12f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.27f, 0.18f, 0.7f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.84f, 0.78f, 0.94f));
      ImGui::SameLine(0.0f, obGap);
      if (ImGui::Button("Back", ImVec2(backW, obH))) {
        SoundManager::Play(SOUND_CLICK01);
        s_createOpen = false;
      }
      ImGui::PopStyleColor(5);
      ImGui::PopStyleVar(2);
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
      SoundManager::Play(SOUND_CLICK01);
      s_ctx.server->SendCharDelete(
          s_selectedSlot, s_slots[s_selectedSlot].name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 30))) {
      SoundManager::Play(SOUND_CLICK01);
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
      SoundManager::Play(SOUND_CLICK01);
      if (s_selectedSlot != i) {
        s_selectedSlot = i;
        int ci = ClassToIndex(s_slots[i].classCode);
        s_slotRender[i].action = CLASS_EMOTES[ci];
        s_slotRender[i].emotePlaying = true;
        s_slotRender[i].emoteBlend = 0.0f;
        s_slotRender[i].animFrame = 0.0f;
      }
      printf("[CharSelect] Selected slot %d: '%s'\n", i, s_slots[i].name);
      return;
    }
  }
}

void OnKeyPress(int key) {
  if (key == GLFW_KEY_ENTER && !s_createOpen) {
    // Enter = connect with selected character
    if (s_selectedSlot >= 0 && s_slots[s_selectedSlot].occupied && s_ctx.server) {
      SoundManager::Play(SOUND_CLICK01);
      s_ctx.server->SendCharSelect(s_slots[s_selectedSlot].name);
      if (s_ctx.onCharSelected)
        s_ctx.onCharSelected();
    }
  }
  if (key == GLFW_KEY_ESCAPE) {
    SoundManager::Play(SOUND_CLICK01);
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
        SoundManager::Play(SOUND_CLICK01);
        s_selectedSlot = idx;
        int ci = ClassToIndex(s_slots[idx].classCode);
        s_slotRender[idx].action = CLASS_EMOTES[ci];
        s_slotRender[idx].emotePlaying = true;
        s_slotRender[idx].emoteBlend = 0.0f;
        s_slotRender[idx].animFrame = 0.0f;
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
