#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "FireEffect.hpp"
#include "MeshBuffers.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
#include <filesystem>
#include <fstream>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_bgfx.h"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <vector>

// Extract object type from "ObjectNN.bmd" filename. Returns -1 if not that pattern.
static int GetObjectTypeFromFilename(const std::string &filename) {
  if (filename.size() >= 11 && filename.compare(0, 6, "Object") == 0) {
    std::string numStr;
    for (size_t i = 6; i < filename.size(); ++i) {
      if (std::isdigit(filename[i])) numStr += filename[i];
      else break;
    }
    if (!numStr.empty()) return std::stoi(numStr) - 1; // Object01=type0
  }
  return -1;
}

// BlendMesh texture ID lookup — map-aware for ObjectNN.bmd files
// Source: Main 5.2 ZzzObject.cpp CreateObject / MoveObject per map
static int GetBlendMeshTexIdFromFilename(const std::string &filename, int mapIdx) {
  // Named objects (Lorencia / shared)
  if (mapIdx == 0) {
    if (filename == "House03.bmd") return 4;
    if (filename == "House04.bmd") return 8;
    if (filename == "House05.bmd") return 2;
    if (filename == "HouseWall02.bmd") return 4;
    if (filename == "Bonfire01.bmd") return 1;
    if (filename == "StreetLight01.bmd") return 1;
    if (filename == "Candle01.bmd") return 1;
    if (filename == "Carriage01.bmd") return 2;
    if (filename == "Waterspout01.bmd") return 3;
  }

  // ObjectNN.bmd → type-based lookup
  int type = GetObjectTypeFromFilename(filename);
  if (type < 0) return -1;

  if (mapIdx == 1) { // Dungeon
    if (type == 41) return 1; // DungeonGate02 torch
    if (type == 42) return 1; // DungeonGate03 torch
  }
  if (mapIdx == 2) { // Devias
    if (type == 19 || type == 92 || type == 93) return 0; // Aurora
  }
  if (mapIdx == 3) { // Noria (Main 5.2 WD_3NORIA)
    switch (type) {
    case 1: return 1;
    case 9: return 3;
    case 17: return 0;
    case 18: return 2;
    case 19: return 0;
    case 37: return 0;
    case 39: return 1;
    case 41: return 0;
    }
  }
  return -1;
}

// UV scroll animation for specific models
static bool HasUVScrollAnimation(const std::string &filename, int mapIdx) {
  if (mapIdx == 0)
    return filename == "House04.bmd" || filename == "House05.bmd" ||
           filename == "Waterspout01.bmd";
  if (mapIdx == 3) {
    int type = GetObjectTypeFromFilename(filename);
    return type == 18 || type == 41 || type == 42 || type == 43;
  }
  return false;
}

namespace fs = std::filesystem;

static const std::string EFFECT_PATH = "Data/Effect";
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

// Whitelist of files actually used in our game (not all Main 5.2 assets)
#include <unordered_set>
static const std::unordered_set<std::string> GAME_MONSTERS = {
  "Monster01.bmd","Monster02.bmd","Monster03.bmd","Monster04.bmd","Monster05.bmd",
  "Monster06.bmd","Monster07.bmd","Monster08.bmd","Monster09.bmd","Monster10.bmd",
  "Monster11.bmd","Monster12.bmd","Monster13.bmd","Monster14.bmd","Monster15.bmd",
  "Monster16.bmd","Monster17.bmd","Monster18.bmd","Monster19.bmd","Monster20.bmd",
  "Monster21.bmd","Monster22.bmd","Monster23.bmd","Monster24.bmd","Monster25.bmd",
  "Monster26.bmd","Monster27.bmd","Monster28.bmd","Monster29.bmd","Monster30.bmd",
  "Monster31.bmd","Monster33.bmd",
};
static const std::unordered_set<std::string> GAME_SKILLS = {
  "Arrow01.bmd","BigStone01.bmd","Blast01.bmd","Bone01.bmd","Fire01.bmd",
  "Ice01.bmd","Ice02.bmd","Inferno01.bmd","Laser01.bmd","Magic02.bmd",
  "Poison01.bmd","Storm01.bmd","Stone01.bmd","Stone02.bmd",
  "Rider01.bmd","Rider02.bmd",
  "EarthQuake01.bmd","EarthQuake02.bmd","EarthQuake03.bmd","EarthQuake04.bmd",
  "EarthQuake05.bmd","EarthQuake06.bmd","EarthQuake07.bmd","EarthQuake08.bmd",
};
static const std::unordered_set<std::string> GAME_NPCS = {
  "Smith01.bmd","Wizard01.bmd","Storage01.bmd","Man01.bmd","Girl01.bmd",
  "Female01.bmd","ElfWizard01.bmd","ElfMerchant01.bmd","MixNpc01.bmd",
};

// Item filter: only show items with known game prefixes
static bool isGameItem(const std::string &fname) {
  static const char *prefixes[] = {
    "Sword","Axe","Mace","Spear","Staff","Bow","CrossBow","Shield",
    "Helm","Armor","Pant","Glove","Boot","Wing","Potion","Jewel","Gem",
    "Ring","Helper","Gold","Scroll","Arrows","Beer","Drink",
    "Covenant","SummonBook","SpiritBill","Saint","MagicBox","SpecialPotion",
    "Necklace","Suho","Devil0","Event","Book",
  };
  for (const char *p : prefixes)
    if (fname.compare(0, strlen(p), p) == 0) return true;
  return false;
}

// Category + sub-category definitions for the model browser
struct SubDef { const char *label; const char *dir; };
struct CatDef {
  const char *label;
  const SubDef *subs;
  int numSubs;
};

static const SubDef OBJECT_SUBS[] = {
    {"Lorencia", "Data/Object1/"}, {"Dungeon", "Data/Object2/"},
    {"Devias",   "Data/Object3/"}, {"Noria",   "Data/Object4/"},
    {"Lost Tower","Data/Object5/"},
};
static const SubDef MONSTER_SUBS[] = {{"All Monsters", "Data/Monster/"}};
static const SubDef ITEM_SUBS[]    = {{"All Items", "Data/Item/"}};
static const SubDef PLAYER_SUBS[]  = {{"Player Models", "Data/Player/"}};
static const SubDef SKILL_SUBS[]   = {{"Skill Effects", "Data/Skill/"}};
static const SubDef NPC_SUBS[]     = {{"NPC Models", "Data/NPC/"}};

static const CatDef CATEGORIES[] = {
    {"Objects",  OBJECT_SUBS,  5},
    {"Monsters", MONSTER_SUBS, 1},
    {"Items",    ITEM_SUBS,    1},
    {"Player",   PLAYER_SUBS,  1},
    {"Skills",   SKILL_SUBS,   1},
    {"NPCs",     NPC_SUBS,     1},
};
static const int NUM_CATS = sizeof(CATEGORIES) / sizeof(CATEGORIES[0]);

// Legacy compat
static const SubDef *MAP_DEFS = OBJECT_SUBS;
static const int NUM_MAPS = 5;

class ObjectBrowser {
public:
  void Run() {
    if (!InitWindow())
      return;

    ActivateMacOSApp();

    InitImGuiBgfx();
    ScanDirectory();

    if (bmdFiles.empty()) {
      std::cerr << "[ObjectBrowser] No BMD files found in " << dataPath
                << std::endl;
      // Don't exit — user can switch maps via UI
    }

    shader = Shader::Load("vs_model.bin", "fs_model.bin");
    if (!shader) {
      std::cerr << "[ObjectBrowser] Failed to load BGFX shaders" << std::endl;
      ShutdownImGuiBgfx();
      bgfx::shutdown();
      glfwDestroyWindow(window);
      glfwTerminate();
      return;
    }

    fireEffect.Init(EFFECT_PATH);
    if (!bmdFiles.empty())
      LoadObject(0);

    while (!glfwWindowShouldClose(window)) {
      float currentFrame = glfwGetTime();
      deltaTime = currentFrame - lastFrame;
      lastFrame = currentFrame;

      glfwPollEvents();
      RenderScene();

      Screenshot::TickRecording(window);

      RenderUI();
      bgfx::frame();
    }

    UnloadObject();
    fireEffect.Cleanup();
    shader->destroy();
    ShutdownImGuiBgfx();
    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  GLFWwindow *window = nullptr;

  // Category + sub-category selection
  int currentCatIdx = 0;
  int currentSubIdx = 0;
  int currentMapIdx = 0; // legacy alias for sub-index when cat==Objects
  std::string dataPath = CATEGORIES[0].subs[0].dir;

  // File list
  std::vector<std::string> bmdFiles;
  int currentIndex = 0;

  // Currently loaded model
  std::unique_ptr<BMDData> currentBMD;
  std::vector<MeshBuffers> meshBuffers;
  AABB currentAABB;

  // Bone world matrices for current model
  std::vector<BoneWorldMatrix> boneMatrices;

  // Orbit camera (shared struct)
  OrbitCamera camera;

  // Shader
  std::unique_ptr<Shader> shader;

  // Mouse state
  bool dragging = false;
  double lastMouseX = 0, lastMouseY = 0;

  // Timing
  float deltaTime = 0.0f;
  float lastFrame = 0.0f;

  // ImGui filter
  char filterBuf[128] = "";

  // Fire effects
  FireEffect fireEffect;

  // BlendMesh: per-model window light state
  int blendMeshTexId = -1;
  bool hasUVScroll = false;

  // Animation state
  bool currentIsAnimated = false;
  int currentNumKeys = 0;
  float currentAnimFrame = 0.0f;
  bool animationEnabled = true;
  float animSpeed = 4.0f; // keyframes/sec (reference: 0.16 * 25fps)

  // GIF recording
  int gifFrameTarget = 72;
  float gifScaleSetting = 0.5f;
  int gifFpsSetting = 12;
  int gifSkipSetting = 1;

  // Auto-frame helper: track axis scale for debug display
  float axisLength = 50.0f;

  // --- Initialization ---

  bool InitWindow() {
    if (!glfwInit())
      return false;

    // BGFX manages its own graphics context — tell GLFW not to create one
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "MU Model Viewer",
                              nullptr, nullptr);
    if (!window)
      return false;

    // Initialize BGFX (single-threaded Metal on macOS)
    bgfx::renderFrame(); // Single-threaded mode: must call before bgfx::init

    bgfx::Init init;
    init.type = bgfx::RendererType::Metal;
#ifdef __APPLE__
    init.platformData.nwh = glfwGetCocoaWindow(window);
#endif
    int initW, initH;
    glfwGetFramebufferSize(window, &initW, &initH);
    init.resolution.width = initW;
    init.resolution.height = initH;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
      std::cerr << "[ObjectBrowser] bgfx::init() failed" << std::endl;
      glfwDestroyWindow(window);
      glfwTerminate();
      return false;
    }

    // Clear color: dark blue-gray (0.15, 0.18, 0.22) = 0x262E38FF
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x262E38FF,
                        1.0f, 0);
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);

    return true;
  }

  void InitImGuiBgfx() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOther(window, false); // false = don't install chaining callbacks (we forward manually)

    // Resolve shader directory (shaders/ or ../shaders/)
    std::ifstream test("shaders/vs_imgui.bin");
    std::string shaderDir = test.good() ? "shaders" : "../shaders";
    ImGui_ImplBgfx_Init(30, shaderDir.c_str()); // view ID 30 for ImGui
  }

  void ShutdownImGuiBgfx() {
    ImGui_ImplBgfx_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  // --- Directory scanning ---

  void ScanDirectory() {
    bmdFiles.clear();
    if (!fs::exists(dataPath)) {
      std::cerr << "[ModelViewer] Directory not found: " << dataPath << std::endl;
      return;
    }

    // Pick whitelist based on category (nullptr = show all)
    const std::unordered_set<std::string> *whitelist = nullptr;
    if (currentCatIdx == 1) whitelist = &GAME_MONSTERS;
    else if (currentCatIdx == 4) whitelist = &GAME_SKILLS;
    else if (currentCatIdx == 5) whitelist = &GAME_NPCS;
    // Items (cat 2) and Player (cat 3): use DB-driven list loaded at startup
    // Objects (cat 0): show all (map-specific dirs are already curated)

    for (auto &entry : fs::directory_iterator(dataPath)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".bmd") {
          std::string fname = entry.path().filename().string();
          if (whitelist && whitelist->find(fname) == whitelist->end())
            continue; // Not in game — skip
          if (currentCatIdx == 2 && !isGameItem(fname))
            continue; // Item not used in game — skip
          bmdFiles.push_back(fname);
        }
      }
    }
    std::sort(bmdFiles.begin(), bmdFiles.end());
    std::cout << "[ModelViewer] Found " << bmdFiles.size() << " BMD files in "
              << dataPath << std::endl;
  }

  // --- Object loading / unloading ---

  void UnloadObject() {
    CleanupMeshBuffers(meshBuffers);
    currentBMD.reset();
    boneMatrices.clear();
  }

  void LoadObject(int index) {
    UnloadObject();
    currentIndex = index;

    std::string fullPath = dataPath + bmdFiles[index];
    currentBMD = BMDParser::Parse(fullPath);

    if (!currentBMD) {
      std::cerr << "[ObjectBrowser] Failed to parse: " << bmdFiles[index]
                << std::endl;
      std::string title = "MU Model Viewer - FAILED: " + bmdFiles[index];
      glfwSetWindowTitle(window, title.c_str());
      return;
    }

    // Compute bone world matrices for rest pose
    boneMatrices = ComputeBoneMatrices(currentBMD.get());

    // Detect animated models (>1 keyframe in first action)
    currentIsAnimated = false;
    currentNumKeys = 0;
    currentAnimFrame = 0.0f;
    if (!currentBMD->Actions.empty() &&
        currentBMD->Actions[0].NumAnimationKeys > 1) {
      currentIsAnimated = true;
      currentNumKeys = currentBMD->Actions[0].NumAnimationKeys;
    }

    // Upload meshes with bone-transformed vertices
    currentAABB = AABB{};
    for (auto &mesh : currentBMD->Meshes) {
      UploadMeshWithBones(mesh, dataPath, boneMatrices, meshBuffers,
                          currentAABB, currentIsAnimated);
    }

    // Resolve BlendMesh for this model
    blendMeshTexId = GetBlendMeshTexIdFromFilename(bmdFiles[index], currentMapIdx);
    hasUVScroll = HasUVScrollAnimation(bmdFiles[index], currentMapIdx);
    if (blendMeshTexId >= 0) {
      for (auto &mb : meshBuffers) {
        if (mb.bmdTextureId == blendMeshTexId) {
          mb.isWindowLight = true;
        }
      }
    }

    AutoFrame();

    // Register fire emitters if this is a fire-type model
    fireEffect.ClearEmitters();
    int fireType = GetFireTypeFromFilename(bmdFiles[index]);
    if (fireType >= 0) {
      glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                       glm::vec3(1.0f, 0.0f, 0.0f));
      auto &offsets = GetFireOffsets(fireType, 0);
      for (auto &off : offsets) {
        glm::vec3 worldPos = glm::vec3(modelMat * glm::vec4(off, 1.0f));
        fireEffect.AddEmitter(worldPos);
      }
    }

    std::string title = "MU Model Viewer - " + bmdFiles[index] + " (" +
                        std::to_string(index + 1) + "/" +
                        std::to_string(bmdFiles.size()) + ")";
    glfwSetWindowTitle(window, title.c_str());
  }

  void SwitchSource(int catIdx, int subIdx) {
    UnloadObject();
    fireEffect.ClearEmitters();
    currentCatIdx = catIdx;
    currentSubIdx = subIdx;
    currentMapIdx = (catIdx == 0) ? subIdx : 0;
    dataPath = CATEGORIES[catIdx].subs[subIdx].dir;
    filterBuf[0] = '\0';
    ScanDirectory();
    if (!bmdFiles.empty()) {
      LoadObject(0);
    } else {
      std::string title = std::string("MU Model Viewer - ") +
                          CATEGORIES[catIdx].label + " (empty)";
      glfwSetWindowTitle(window, title.c_str());
    }
  }

  // Legacy wrapper
  void SwitchMap(int mapIdx) { SwitchSource(0, mapIdx); }

  void AutoFrame() {
    glm::vec3 c = currentAABB.center();
    // Apply the same Z-up -> Y-up rotation (-90 deg around X): (x, y, z) -> (x, z, -y)
    camera.center = glm::vec3(c.x, c.z, -c.y);
    float radius = currentAABB.radius();
    if (radius < 0.001f)
      radius = 100.0f;

    camera.distance = radius * 2.6f;
    camera.yaw = 45.0f;
    camera.pitch = -25.0f;

    axisLength = radius * 0.5f;
  }

  // --- Set uniforms for the model shader ---

  void SetModelUniforms(const glm::vec3 &eye, float blendMeshLight,
                        float objectAlpha, const glm::vec2 &texCoordOffset,
                        float luminosity) {
    // u_params: x=objectAlpha, y=blendMeshLight, z=chromeMode, w=chromeTime
    shader->setVec4("u_params",
                    glm::vec4(objectAlpha, blendMeshLight, 0.0f, 0.0f));
    // u_params2: x=luminosity
    shader->setVec4("u_params2", glm::vec4(luminosity, 0.0f, 0.0f, 0.0f));

    shader->setVec4("u_lightPos",
                    glm::vec4(eye + glm::vec3(0, 200, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));

    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));

    // No fog
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));

    // No glow
    shader->setVec4("u_glowColor", glm::vec4(0.0f));

    // No shadows
    shader->setVec4("u_shadowParams", glm::vec4(0.0f));

    // Tex coord offset (for UV scroll animation)
    shader->setVec2("u_texCoordOffset", texCoordOffset);

    // No point lights
    shader->uploadPointLights(0, (glm::vec3 *)nullptr, (glm::vec3 *)nullptr,
                              (float *)nullptr);
  }

  // --- Rendering ---

  void RenderScene() {
    if (meshBuffers.empty()) {
      // Still need to touch view 0 so bgfx clears the screen
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
      bgfx::touch(0);
      return;
    }

    // Advance skeletal animation if model is animated
    if (currentIsAnimated && animationEnabled && currentBMD) {
      currentAnimFrame += animSpeed * deltaTime;
      if (currentAnimFrame >= (float)currentNumKeys)
        currentAnimFrame = std::fmod(currentAnimFrame, (float)currentNumKeys);

      auto bones =
          ComputeBoneMatricesInterpolated(currentBMD.get(), 0, currentAnimFrame);
      for (int mi = 0;
           mi < (int)meshBuffers.size() && mi < (int)currentBMD->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(currentBMD->Meshes[mi], bones,
                                 meshBuffers[mi]);
      }
    }

    int fbWidth, fbHeight, winW_, winH_;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glfwGetWindowSize(window, &winW_, &winH_);
    float dpiScale = (winW_ > 0) ? (float)fbWidth / (float)winW_ : 1.0f;
    int panelPx = (int)(250.0f * dpiScale);
    int sceneW = fbWidth - panelPx;
    if (sceneW < 1) sceneW = 1;

    // Set up view 0 for the 3D scene (offset by ImGui panel width)
    bgfx::setViewRect(0, uint16_t(panelPx), 0, uint16_t(sceneW),
                       uint16_t(fbHeight));

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)sceneW / (float)fbHeight, 0.1f, 100000.0f);
    glm::mat4 view = camera.GetViewMatrix();

    bgfx::setViewTransform(0, glm::value_ptr(view),
                           glm::value_ptr(projection));
    bgfx::touch(0);

    // MU Online uses Z-up; rotate -90 deg around X to convert to Y-up
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                  glm::vec3(1.0f, 0.0f, 0.0f));

    glm::vec3 eye = camera.GetEyePosition();

    // BlendMesh animation state
    float currentTime = (float)glfwGetTime();
    float flickerBase =
        0.55f + 0.15f * std::sin(currentTime * 7.3f) *
                    std::sin(currentTime * 11.1f + 2.0f);
    float uvScroll = -std::fmod(currentTime, 1.0f);

    // Two-pass: opaque first, then alpha (so transparent meshes see depth)
    auto renderMesh = [&](MeshBuffers &mb) {
      if (mb.indexCount == 0 || mb.hidden)
        return;

      // Set model transform for this draw call
      bgfx::setTransform(glm::value_ptr(model));

      // Set vertex and index buffers
      if (mb.isDynamic)
        bgfx::setVertexBuffer(0, mb.dynVbo);
      else
        bgfx::setVertexBuffer(0, mb.vbo);
      bgfx::setIndexBuffer(mb.ebo);

      // Bind texture
      shader->setTexture(0, "s_texColor", mb.texture);

      if (mb.isWindowLight && blendMeshTexId >= 0) {
        // BlendMesh: additive blend, no depth write
        glm::vec2 uvOff = hasUVScroll ? glm::vec2(0.0f, uvScroll)
                                      : glm::vec2(0.0f);
        SetModelUniforms(eye, flickerBase, 1.0f, uvOff, 1.0f);

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                         BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                               BGFX_STATE_BLEND_ONE);
        // No BGFX_STATE_WRITE_Z = no depth write
        bgfx::setState(state);
        bgfx::submit(0, shader->program);

      } else if (mb.noneBlend) {
        // Opaque: no blending
        SetModelUniforms(eye, 1.0f, 1.0f, glm::vec2(0.0f), 1.0f);

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                         BGFX_STATE_MSAA;
        bgfx::setState(state);
        bgfx::submit(0, shader->program);

      } else if (mb.bright) {
        // Bright/additive: additive blend, no depth write
        SetModelUniforms(eye, 1.0f, 1.0f, glm::vec2(0.0f), 1.0f);

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                         BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                               BGFX_STATE_BLEND_ONE);
        bgfx::setState(state);
        bgfx::submit(0, shader->program);

      } else if (mb.hasAlpha) {
        // Alpha mesh: blend, no depth write, no backface cull
        SetModelUniforms(eye, 1.0f, 1.0f, glm::vec2(0.0f), 1.0f);

        uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                  BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(state);
        bgfx::submit(0, shader->program);

      } else {
        // Opaque mesh: depth write, backface cull, no blend
        SetModelUniforms(eye, 1.0f, 1.0f, glm::vec2(0.0f), 1.0f);

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                         BGFX_STATE_MSAA | BGFX_STATE_CULL_CW;
        bgfx::setState(state);
        bgfx::submit(0, shader->program);
      }
    };

    // Pass 1: opaque meshes (write depth)
    for (auto &mb : meshBuffers)
      if (!mb.hasAlpha && !mb.isWindowLight && !mb.bright)
        renderMesh(mb);
    // Pass 2: alpha/additive meshes (read depth, don't write)
    for (auto &mb : meshBuffers)
      if (mb.hasAlpha || mb.isWindowLight || mb.bright)
        renderMesh(mb);

    // Update and render fire effects
    fireEffect.Update(deltaTime);
    fireEffect.Render(view, projection);
  }

  void RenderUI() {
    ImGui_ImplBgfx_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(250, (float)winH));
    ImGui::Begin("Browser", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    // Category selector
    if (ImGui::BeginCombo("Category", CATEGORIES[currentCatIdx].label)) {
      for (int i = 0; i < NUM_CATS; ++i) {
        if (ImGui::Selectable(CATEGORIES[i].label, i == currentCatIdx)) {
          if (i != currentCatIdx) SwitchSource(i, 0);
        }
      }
      ImGui::EndCombo();
    }

    // Sub-category selector (only show if > 1 sub)
    const auto &cat = CATEGORIES[currentCatIdx];
    if (cat.numSubs > 1) {
      if (ImGui::BeginCombo("Sub", cat.subs[currentSubIdx].label)) {
        for (int i = 0; i < cat.numSubs; ++i) {
          if (ImGui::Selectable(cat.subs[i].label, i == currentSubIdx)) {
            if (i != currentSubIdx) SwitchSource(currentCatIdx, i);
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));

    std::string filterStr(filterBuf);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(),
                   ::tolower);

    ImGui::BeginChild("FileList", ImVec2(0, (float)winH * 0.5f), true);
    for (int i = 0; i < (int)bmdFiles.size(); ++i) {
      std::string lower = bmdFiles[i];
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (!filterStr.empty() && lower.find(filterStr) == std::string::npos)
        continue;

      bool selected = (i == currentIndex);
      if (ImGui::Selectable(bmdFiles[i].c_str(), selected)) {
        LoadObject(i);
      }
      if (selected && ImGui::IsWindowAppearing()) {
        ImGui::SetScrollHereY(0.5f);
      }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (currentBMD) {
      ImGui::Text("Name: %s", currentBMD->Name.c_str());
      ImGui::Text("Meshes: %d", (int)currentBMD->Meshes.size());
      int totalVerts = 0, totalTris = 0;
      for (auto &m : currentBMD->Meshes) {
        totalVerts += m.NumVertices;
        totalTris += m.NumTriangles;
      }
      ImGui::Text("Vertices: %d", totalVerts);
      ImGui::Text("Triangles: %d", totalTris);
      ImGui::Text("Bones: %d", (int)currentBMD->Bones.size());
      ImGui::Text("Actions: %d", (int)currentBMD->Actions.size());

      ImGui::Separator();
      ImGui::Text("Textures:");
      for (auto &m : currentBMD->Meshes) {
        ImGui::BulletText("%s", m.TextureName.c_str());
      }
    } else {
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load");
    }

    // Animation controls
    if (currentIsAnimated) {
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Animated");
      ImGui::Text("Keyframes: %d", currentNumKeys);
      ImGui::Checkbox("Play", &animationEnabled);
      ImGui::SliderFloat("Speed", &animSpeed, 0.5f, 20.0f, "%.1f k/s");
      float frameVal = currentAnimFrame;
      if (ImGui::SliderFloat("Frame", &frameVal, 0.0f,
                              (float)(currentNumKeys - 1), "%.1f")) {
        currentAnimFrame = frameVal;
      }
    }

    ImGui::Separator();
    ImGui::Text("GIF Recording:");
    ImGui::SliderFloat("Scale", &gifScaleSetting, 0.1f, 1.0f, "%.2f");
    ImGui::SliderInt("FPS", &gifFpsSetting, 5, 25);
    ImGui::SliderInt("Frames", &gifFrameTarget, 10, 200);

    if (Screenshot::IsRecording()) {
      float progress = Screenshot::GetProgress();
      const char *label =
          Screenshot::IsWarmingUp() ? "Warming up..." : "Recording...";
      ImGui::ProgressBar(progress, ImVec2(-1, 0), label);
    } else {
      if (ImGui::Button("Capture GIF", ImVec2(-1, 0))) {
        gifSkipSetting =
            25 / gifFpsSetting; // Assume 25fps render for skip calculation
        Screenshot::StartRecording(window, "screenshots/capture.gif",
                                   gifFrameTarget, 100 / gifFpsSetting,
                                   gifScaleSetting, gifSkipSetting - 1);
      }
    }

    ImGui::Separator();
    ImGui::TextWrapped("LMB drag: Rotate\nScroll: Zoom\nArrows: Prev/Next");

    ImGui::End();
    ImGui::Render();
    ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());
  }

  // --- GLFW Callbacks ---

  static void ScrollCallback(GLFWwindow *w, double xoff, double yoff) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
    if (ImGui::GetIO().WantCaptureMouse)
      return;

    self->camera.distance -= (float)yoff * self->camera.distance * 0.15f;
    self->camera.distance = glm::clamp(self->camera.distance, 1.0f, 50000.0f);
  }

  static void MouseButtonCallback(GLFWwindow *w, int button, int action,
                                  int mods) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        self->dragging = true;
        glfwGetCursorPos(w, &self->lastMouseX, &self->lastMouseY);
      } else if (action == GLFW_RELEASE) {
        self->dragging = false;
      }
    }
  }

  static void CursorPosCallback(GLFWwindow *w, double x, double y) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);

    if (self->dragging && !ImGui::GetIO().WantCaptureMouse) {
      float dx = (float)(x - self->lastMouseX);
      float dy = (float)(y - self->lastMouseY);
      self->lastMouseX = x;
      self->lastMouseY = y;

      self->camera.yaw += dx * 0.3f;
      self->camera.pitch += dy * 0.3f;
      self->camera.pitch = glm::clamp(self->camera.pitch, -89.0f, -5.0f);
    }
  }

  static void KeyCallback(GLFWwindow *w, int key, int scancode, int action,
                          int mods) {
    auto *self = static_cast<ObjectBrowser *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
      if (!self->bmdFiles.empty()) {
        if (key == GLFW_KEY_LEFT || key == GLFW_KEY_UP) {
          int newIdx = (self->currentIndex - 1 + (int)self->bmdFiles.size()) %
                       (int)self->bmdFiles.size();
          self->LoadObject(newIdx);
        }
        if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_DOWN) {
          int newIdx = (self->currentIndex + 1) % (int)self->bmdFiles.size();
          self->LoadObject(newIdx);
        }
      }
      if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(w, true);
      }
    }
  }

  static void CharCallback(GLFWwindow *w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
  }
};

int main(int argc, char **argv) {
#ifdef __APPLE__
  { // Fix CWD when launched via 'open' or Finder
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
      auto dir = std::filesystem::path(buf).parent_path();
      if (!dir.empty())
        std::filesystem::current_path(dir);
    }
  }
#endif
  ObjectBrowser browser;
  browser.Run();
  return 0;
}
