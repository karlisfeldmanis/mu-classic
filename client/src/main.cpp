#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "BoidManager.hpp"
#include "Camera.hpp"
#include "CharacterSelect.hpp"
#include "ClickEffect.hpp"
#include "ClientPacketHandler.hpp"
#include "ClientTypes.hpp"
#include "FireEffect.hpp"
#include "GrassRenderer.hpp"
#include "GroundItemRenderer.hpp"
#include "HeroCharacter.hpp"
#include "InputHandler.hpp"
#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "MockData.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "ObjectRenderer.hpp"
#include "RayPicker.hpp"
#include "Screenshot.hpp"
#include "ServerConnection.hpp"
#include "Shader.hpp"
#include "Sky.hpp"
#include "SoundManager.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "UICoords.hpp"
#include "UITexture.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <streambuf>
#include <turbojpeg.h>
#include <unistd.h>

// Tee streambuf: writes to both a file and the original stream
class TeeStreambuf : public std::streambuf {
public:
  TeeStreambuf(std::streambuf *orig, std::streambuf *file)
      : original(orig), fileBuf(file) {}

protected:
  int overflow(int c) override {
    if (c == EOF)
      return !EOF;
    int r1 = original->sputc(c);
    int r2 = fileBuf->sputc(c);
    return (r1 == EOF || r2 == EOF) ? EOF : c;
  }
  int sync() override {
    original->pubsync();
    fileBuf->pubsync();
    return 0;
  }

private:
  std::streambuf *original;
  std::streambuf *fileBuf;
};

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
static void activateMacOSApp() {
  id app =
      ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
                                     sel_registerName("sharedApplication"));
  ((void (*)(id, SEL, long))objc_msgSend)(
      app, sel_registerName("setActivationPolicy:"),
      0); // NSApplicationActivationPolicyRegular
  ((void (*)(id, SEL, BOOL))objc_msgSend)(
      app, sel_registerName("activateIgnoringOtherApps:"), YES);
}
#endif

// GL error checking utility — call after critical GL operations
static void checkGLError(const char *label) {
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR) {
    const char *errStr = "UNKNOWN";
    switch (err) {
    case GL_INVALID_ENUM:
      errStr = "INVALID_ENUM";
      break;
    case GL_INVALID_VALUE:
      errStr = "INVALID_VALUE";
      break;
    case GL_INVALID_OPERATION:
      errStr = "INVALID_OP";
      break;
    case GL_OUT_OF_MEMORY:
      errStr = "OUT_OF_MEMORY";
      break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      errStr = "INVALID_FBO";
      break;
    }
    std::cerr << "[GL ERROR] " << errStr << " (0x" << std::hex << err
              << std::dec << ") at " << label << std::endl;
  }
}

// OpenGL debug callback (ARB_debug_output) — logs all GL warnings/errors
static void GLAPIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id,
                                       GLenum severity, GLsizei /*length*/,
                                       const GLchar *message,
                                       const void * /*userParam*/) {
  // Skip notifications (very noisy)
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    return;
  const char *sevStr = "???";
  switch (severity) {
  case GL_DEBUG_SEVERITY_HIGH:
    sevStr = "HIGH";
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    sevStr = "MED";
    break;
  case GL_DEBUG_SEVERITY_LOW:
    sevStr = "LOW";
    break;
  }
  const char *typeStr = "other";
  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    typeStr = "ERROR";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    typeStr = "DEPRECATED";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    typeStr = "UNDEFINED";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    typeStr = "PERF";
    break;
  }
  std::cerr << "[GL " << sevStr << "/" << typeStr << "] " << message
            << std::endl;
}

Camera g_camera(glm::vec3(12800.0f, 0.0f, 12800.0f));
Terrain g_terrain;
ObjectRenderer g_objectRenderer;
FireEffect g_fireEffect;
Sky g_sky;

GrassRenderer g_grass;
VFXManager g_vfxManager;
BoidManager g_boidManager;

// Point lights collected from light-emitting world objects
static const int MAX_POINT_LIGHTS = 64;
static std::vector<PointLight> g_pointLights;

// Hero character and click-to-move effect
static HeroCharacter g_hero;
static ClickEffect g_clickEffect;
static NpcManager g_npcManager;
static MonsterManager g_monsterManager;
static ServerConnection g_server;

// NPC interaction state
static int g_hoveredNpc = -1;        // Index of NPC under mouse cursor
static int g_hoveredMonster = -1;    // Index of Monster under mouse cursor
static int g_hoveredGroundItem = -1; // Index of Ground Item under mouse cursor
static int g_selectedNpc = -1; // Index of NPC that was clicked (dialog open)

// Client-side item definitions (owned by ItemDatabase, reference here)
static auto &g_itemDefs = ItemDatabase::GetItemDefs();

// ── Floating damage numbers (type in GroundItemRenderer.hpp) ──
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// Ground item drops (type in ClientTypes.hpp)
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath = "Data";

// Server-received character stats for HUD
static int g_serverLevel = 1;
static int g_serverHP = 110, g_serverMaxHP = 110;
static int g_serverMP = 20, g_serverMaxMP = 20;
static int g_serverAG = 20, g_serverMaxAG = 20;
static int g_serverStr = 28, g_serverDex = 20, g_serverVit = 25,
           g_serverEne = 10;
static int g_serverLevelUpPoints = 0;
static int64_t g_serverXP = 0;
static int g_serverDefense = 0, g_serverAttackSpeed = 0, g_serverMagicSpeed = 0;
static int g_heroCharacterId = 0;
static char g_characterName[32] = "RealPlayer";

// Inventory & UI state
static bool g_showCharInfo = false;
static bool g_showInventory = false;
static bool g_showSkillWindow = false;

// Learned skills (synced from server via 0x41)
static std::vector<uint8_t> g_learnedSkills;

// Quick slot assignments
static int16_t g_potionBar[4] = {850, 851, 852, -1}; // Apple, SmallHP, MedHP, (empty)
static int8_t g_skillBar[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static float g_potionCooldown = 0.0f;  // Potion cooldown timer (seconds)
static constexpr float POTION_COOLDOWN_TIME = 30.0f;
static bool g_shopOpen = false;
static bool g_showGameMenu = false;
static std::vector<ShopItem> g_shopItems;

// Skill learning state
static bool g_isLearningSkill = false;
static float g_learnSkillTimer = 0.0f;
static uint8_t g_learningSkillId = 0;
static float g_autoSaveTimer = 0.0f;
static constexpr float AUTOSAVE_INTERVAL = 60.0f; // Save quickslots every 60s
static constexpr float LEARN_SKILL_DURATION = 3.0f; // Seconds of heal anim

// RMC (Right Mouse Click) skill slot
static int8_t g_rmcSkillId = -1;
static bool g_rightMouseHeld = false;

// Town teleport state
static bool g_teleportingToTown = false;
static float g_teleportTimer = 0.0f;
static constexpr float TELEPORT_CAST_TIME = 2.5f; // Seconds of heal anim

// Client-side inventory (synced from server via 0x36)
// ClientInventoryItem defined in ClientTypes.hpp
static ClientInventoryItem g_inventory[INVENTORY_SLOTS] = {};
static uint32_t g_zen = 0;
static bool g_syncDone =
    false; // Safeguard: don't send updates until initial sync done

// Equipment display (type in ClientTypes.hpp)
static ClientEquipSlot g_equipSlots[12] = {};

// UI fonts
static ImFont *g_fontDefault = nullptr;
static ImFont *g_fontBold = nullptr;

static UICoords g_hudCoords; // File-scope for mouse callback access

// ServerEquipSlot defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// ServerData defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// (see src/ClientPacketHandler.cpp)

// Delegated to ClientPacketHandler::HandleGamePacket
// (see src/ClientPacketHandler.cpp)

static const TerrainData *g_terrainDataPtr = nullptr;

// Roof hiding: types 125 (HouseWall05) and 126 (HouseWall06) fade when
// hero stands on layer1 tile == 4 (building interior). Original:
// ZzzObject.cpp:3744
static std::unordered_map<int, float> g_typeAlpha = {{125, 1.0f}, {126, 1.0f}};
static std::unordered_map<int, float> g_typeAlphaTarget = {{125, 1.0f},
                                                           {126, 1.0f}};

// ── Game state machine ──
enum class GameState {
  CONNECTING,  // TCP connect in progress
  CHAR_SELECT, // Character select scene active
  LOADING,     // Selected character, loading world data
  INGAME       // Normal gameplay
};
static GameState g_gameState = GameState::CONNECTING;
static bool g_worldInitialized = false; // True once game world is set up
static int g_loadingFrames = 0;         // Frames spent in LOADING state
static GLuint g_loadingTex = 0;         // Loading screen texture

struct LightTemplate {
  glm::vec3 color;
  float range;
  float heightOffset; // Y offset above object base for emission point
};

// Returns light properties for a given object type, or nullptr if not a light
static const LightTemplate *GetLightProperties(int type) {
  static const LightTemplate fireLightProps = {glm::vec3(1.5f, 0.9f, 0.5f),
                                               800.0f, 150.0f};
  static const LightTemplate bonfireProps = {glm::vec3(1.5f, 0.75f, 0.3f),
                                             1000.0f, 100.0f};
  static const LightTemplate gateProps = {glm::vec3(1.5f, 0.9f, 0.5f), 800.0f,
                                          200.0f};
  static const LightTemplate bridgeProps = {glm::vec3(1.2f, 0.7f, 0.4f), 700.0f,
                                            50.0f};
  static const LightTemplate streetLightProps = {glm::vec3(1.5f, 1.2f, 0.75f),
                                                 800.0f, 250.0f};
  static const LightTemplate candleProps = {glm::vec3(1.2f, 0.7f, 0.3f), 600.0f,
                                            80.0f};
  static const LightTemplate lightFixtureProps = {glm::vec3(1.2f, 0.85f, 0.5f),
                                                  700.0f, 150.0f};

  switch (type) {
  case 50:
  case 51:
    return &fireLightProps;
  case 52:
    return &bonfireProps;
  case 55:
    return &gateProps;
  case 80:
    return &bridgeProps;
  case 90:
    return &streetLightProps;
  case 130:
  case 131:
  case 132:
    return &lightFixtureProps;
  case 150:
    return &candleProps;
  default:
    return nullptr;
  }
}

// ── Game world initialization (called after character select) ──
// Forward declared, defined after main() helpers
static void InitGameWorld(ServerData &serverData);

// Input handling (mouse, keyboard, click-to-move, processInput) delegated
// to InputHandler module (see src/InputHandler.cpp)

// Panel rendering, click handling, drag/drop, tooltip, and item layout
// all delegated to InventoryUI module (see src/InventoryUI.cpp)

int main(int argc, char **argv) {
  // Open client.log — tee all cout/cerr to both console and file
  std::ofstream logFile("client.log", std::ios::trunc);
  TeeStreambuf *coutTee = nullptr, *cerrTee = nullptr;
  std::streambuf *origCout = nullptr, *origCerr = nullptr;
  if (logFile.is_open()) {
    // Log header with timestamp
    std::time_t now = std::time(nullptr);
    logFile << "=== MuRemaster client.log === " << std::ctime(&now)
            << std::endl;
    logFile.flush();

    origCout = std::cout.rdbuf();
    origCerr = std::cerr.rdbuf();
    coutTee = new TeeStreambuf(origCout, logFile.rdbuf());
    cerrTee = new TeeStreambuf(origCerr, logFile.rdbuf());
    std::cout.rdbuf(coutTee);
    std::cerr.rdbuf(cerrTee);
  }

  struct StreamRedirector {
    std::streambuf *origCout, *origCerr;
    TeeStreambuf *coutTee, *cerrTee;
    StreamRedirector(std::streambuf *oc, std::streambuf *oce, TeeStreambuf *ct,
                     TeeStreambuf *cet)
        : origCout(oc), origCerr(oce), coutTee(ct), cerrTee(cet) {}
    ~StreamRedirector() {
      if (origCout)
        std::cout.rdbuf(origCout);
      if (origCerr)
        std::cerr.rdbuf(origCerr);
      delete coutTee;
      delete cerrTee;
    }
  } redirector(origCout, origCerr, coutTee, cerrTee);

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return -1;
  }

  // GL 3.3 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_STENCIL_BITS, 8);

  GLFWwindow *window = glfwCreateWindow(
      1366, 768, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

#ifdef __APPLE__
  activateMacOSApp();
#endif

  if (glewInit() != GLEW_OK) {
    std::cerr << "Failed to initialize GLEW" << std::endl;
    return -1;
  }
  ItemDatabase::Init();

  // Enable OpenGL debug output if available (ARB_debug_output)
  if (GLEW_ARB_debug_output) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallbackARB(glDebugCallback, nullptr);
    std::cout << "[GL] Debug output enabled" << std::endl;
  } else {
    std::cout << "[GL] Debug output not available — using manual checks"
              << std::endl;
  }
  std::cout << "[GL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
  std::cout << "[GL] Version: " << glGetString(GL_VERSION) << std::endl;

  g_terrain.Init(); // Initialize OpenGL resources for terrain
  checkGLError("terrain init");

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, false);
  // GLFW input callbacks registered later via InputHandler::RegisterCallbacks
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load fonts for high-fidelity UI
  float contentScale = 1.0f;
  {
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    contentScale = xscale;
  }
  {
    ImFontConfig cfg;
    const char *fontPath = "external/imgui/misc/fonts/ProggyClean.ttf";
    // Check if font file exists before calling AddFontFromFileTTF
    // (imgui asserts internally on missing file before returning null)
    FILE *ftest = fopen(fontPath, "rb");
    if (ftest) {
      fclose(ftest);
      g_fontDefault =
          io.Fonts->AddFontFromFileTTF(fontPath, 13.0f * contentScale);
      g_fontBold = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f * contentScale);
    }
    if (!g_fontDefault)
      g_fontDefault = io.Fonts->AddFontDefault(&cfg);
    if (!g_fontBold)
      g_fontBold = g_fontDefault;

    io.Fonts->Build();
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

  MockData hudData = MockData::CreateDK50();

  // Load Terrain for testing
  std::string data_path = g_dataPath;
  TerrainData terrainData = TerrainParser::LoadWorld(1, data_path);

  // Reconstruct TW_NOGROUND for bridge cells.
  // The .att file for this data version lacks these flags (verified: 0
  // cells). Original engine reads them from .att (ZzzLodTerrain.cpp:1665). We
  // reconstruct from bridge objects (type 80) with orientation awareness.
  {
    const int S = TerrainParser::TERRAIN_SIZE;
    // Guard: skip bridge reconstruction if terrain mapping data is missing
    if ((int)terrainData.mapping.attributes.size() < S * S ||
        (int)terrainData.mapping.layer1.size() < S * S) {
      std::cerr << "[Terrain] Warning: mapping data missing, skipping bridge "
                   "reconstruction"
                << std::endl;
    } else {
      int count = 0;
      for (const auto &obj : terrainData.objects) {
        if (obj.type != 80)
          continue;
        int gz = (int)(obj.position.x / 100.0f);
        int gx = (int)(obj.position.z / 100.0f);
        float angZ =
            std::abs(std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
        bool spanAlongGZ = (std::abs(angZ - 90.0f) < 45.0f);
        // +1 buffer for bilinear neighbor coverage in shader
        int rGZ = spanAlongGZ ? 4 : 2;
        int rGX = spanAlongGZ ? 2 : 4;
        for (int dz = -rGZ; dz <= rGZ; ++dz) {
          for (int dx = -rGX; dx <= rGX; ++dx) {
            int cz = gz + dz, cx = gx + dx;
            if (cz >= 0 && cz < S && cx >= 0 && cx < S) {
              terrainData.mapping.attributes[cz * S + cx] |= 0x08;
              count++;
            }
          }
        }
      }
      // Expand TW_NOGROUND to adjacent water cells so bilinear sampling in the
      // shader never mixes unmarked water into bridge road tiles.
      std::vector<uint8_t> expanded = terrainData.mapping.attributes;
      for (int z = 0; z < S; ++z) {
        for (int x = 0; x < S; ++x) {
          if (!(terrainData.mapping.attributes[z * S + x] & 0x08))
            continue;
          for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
              int nz = z + dz, nx = x + dx;
              if (nz >= 0 && nz < S && nx >= 0 && nx < S) {
                if (terrainData.mapping.layer1[nz * S + nx] == 5)
                  expanded[nz * S + nx] |= 0x08;
              }
            }
          }
        }
      }
      terrainData.mapping.attributes = expanded;

      int finalCount = 0;
      for (int idx = 0; idx < S * S; ++idx)
        if (terrainData.mapping.attributes[idx] & 0x08)
          finalCount++;
      std::cout << "[Terrain] Marked " << finalCount
                << " bridge cells as TW_NOGROUND (" << count
                << " from objects + expansion)" << std::endl;
    } // else (has mapping data)
  }

  // Make terrain data accessible for movement/height
  g_terrainDataPtr = &terrainData;
  RayPicker::Init(&terrainData, &g_camera, &g_npcManager, &g_monsterManager,
                  g_groundItems, MAX_GROUND_ITEMS);

  g_terrain.Load(terrainData, 1, data_path);
  std::cout << "Loaded Map 1 (Lorencia): " << terrainData.heightmap.size()
            << " height samples, " << terrainData.objects.size() << " objects"
            << std::endl;

  // Load world objects
  g_objectRenderer.Init();
  g_objectRenderer.SetTerrainLightmap(terrainData.lightmap);
  g_objectRenderer.SetTerrainMapping(&terrainData.mapping);
  g_objectRenderer.SetTerrainHeightmap(terrainData.heightmap);
  std::string object1_path = data_path + "/Object1";
  g_objectRenderer.LoadObjects(terrainData.objects, object1_path);
  checkGLError("object renderer load");
  std::cout << "[ObjectRenderer] Loaded " << terrainData.objects.size()
            << " object instances, " << g_objectRenderer.GetModelCount()
            << " unique models" << std::endl;
  g_grass.Init();
  g_grass.Load(terrainData, 1, data_path);
  checkGLError("grass load");

  // Initialize sky
  g_sky.Init(data_path + "/");
  checkGLError("sky init");

  // Initialize fire effects and register emitters from fire-type objects
  g_fireEffect.Init(data_path + "/Effect");
  g_vfxManager.Init(data_path);
  g_vfxManager.SetTerrainHeightFunc(
      [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });
  g_boidManager.Init(data_path);
  g_boidManager.SetTerrainData(&terrainData);
  checkGLError("fire init");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type);
    for (auto &off : offsets) {
      // Extract rotation without scale (original CreateFire only rotates, no
      // scale)
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddEmitter(worldPos + rot * off);
    }
  }
  // Register smoke emitters for torch smoke objects (types 131, 132)
  // Main 5.2: CreateFire(1/2) on MODEL_LIGHT01+1/+2
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &smokeOffsets = GetSmokeOffsets(inst.type);
    for (auto &off : smokeOffsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddSmokeEmitter(worldPos + rot * off);
    }
    // Waterspout mist (Main 5.2: BITMAP_SMOKE from bones 1 & 4)
    // Two spray points: upper and lower — blue water tint, not fire smoke
    if (inst.type == 105) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      g_fireEffect.AddWaterSmokeEmitter(worldPos +
                                        glm::vec3(0.0f, 180.0f, 0.0f));
      g_fireEffect.AddWaterSmokeEmitter(worldPos +
                                        glm::vec3(0.0f, 120.0f, 0.0f));
    }
  }
  std::cout << "[FireEffect] Registered " << g_fireEffect.GetEmitterCount()
            << " fire+smoke emitters" << std::endl;

  // Collect point lights from light-emitting objects
  g_pointLights.clear();
  for (auto &inst : g_objectRenderer.GetInstances()) {
    const LightTemplate *props = GetLightProperties(inst.type);
    if (!props)
      continue;
    // Extract world position from model matrix translation column
    glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
    PointLight light;
    light.position = worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f);
    light.color = props->color;
    light.range = props->range;
    light.objectType = inst.type;
    g_pointLights.push_back(light);
  }
  // Cap at shader maximum
  if ((int)g_pointLights.size() > MAX_POINT_LIGHTS)
    g_pointLights.resize(MAX_POINT_LIGHTS);
  std::cout << "[Lights] Collected " << g_pointLights.size()
            << " point lights from world objects" << std::endl;

  // Initialize hero character and click effect
  g_hero.Init(data_path);
  g_hero.SetTerrainData(&terrainData);
  g_hero.SetVFXManager(&g_vfxManager);

  // Starting character initialization: placeholder stats (server overrides)
  g_hero.LoadStats(1, 28, 20, 25, 10, 0, 0, 110, 110, 20, 20, 50, 50, 16);
  g_hero.SetTerrainLightmap(terrainData.lightmap);
  g_hero.SetPointLights(g_pointLights);
  ItemModelManager::Init(g_hero.GetShader(), g_dataPath);
  g_hero.SnapToTerrain();

  g_clickEffect.Init();
  InventoryUI::LoadSlotBackgrounds(g_dataPath);

  // Initialize InventoryUI with shared state pointers
  {
    InventoryUIContext ctx;
    ctx.characterName = g_characterName;
    ctx.heroCharacterId = &g_heroCharacterId;
    ctx.inventory = g_inventory;
    ctx.equipSlots = g_equipSlots;
    ctx.zen = &g_zen;
    ctx.syncDone = &g_syncDone;
    ctx.showCharInfo = &g_showCharInfo;
    ctx.showInventory = &g_showInventory;
    ctx.showSkillWindow = &g_showSkillWindow;
    ctx.learnedSkills = &g_learnedSkills;
    ctx.potionBar = g_potionBar;
    ctx.skillBar = g_skillBar;
    ctx.potionCooldown = &g_potionCooldown;
    ctx.shopOpen = &g_shopOpen;
    ctx.shopItems = &g_shopItems;
    ctx.isLearningSkill = &g_isLearningSkill;
    ctx.learnSkillTimer = &g_learnSkillTimer;
    ctx.learningSkillId = &g_learningSkillId;
    ctx.rmcSkillId = &g_rmcSkillId;
    ctx.serverLevel = &g_serverLevel;
    ctx.serverStr = &g_serverStr;
    ctx.serverDex = &g_serverDex;
    ctx.serverVit = &g_serverVit;
    ctx.serverEne = &g_serverEne;
    ctx.serverLevelUpPoints = &g_serverLevelUpPoints;
    ctx.serverDefense = &g_serverDefense;
    ctx.serverAttackSpeed = &g_serverAttackSpeed;
    ctx.serverMagicSpeed = &g_serverMagicSpeed;
    ctx.serverHP = &g_serverHP;
    ctx.serverMaxHP = &g_serverMaxHP;
    ctx.serverMP = &g_serverMP;
    ctx.serverMaxMP = &g_serverMaxMP;
    ctx.serverAG = &g_serverAG;
    ctx.serverXP = &g_serverXP;
    ctx.teleportingToTown = &g_teleportingToTown;
    ctx.teleportTimer = &g_teleportTimer;
    ctx.teleportCastTime = TELEPORT_CAST_TIME;
    ctx.hero = &g_hero;
    ctx.server = &g_server;
    ctx.hudCoords = &g_hudCoords;
    ctx.fontDefault = g_fontDefault;
    InventoryUI::Init(ctx);
  }

  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(&terrainData);
  checkGLError("hero init");

  // Initialize input handler with shared game state
  {
    InputContext inputCtx;
    inputCtx.hero = &g_hero;
    inputCtx.camera = &g_camera;
    inputCtx.clickEffect = &g_clickEffect;
    inputCtx.server = &g_server;
    inputCtx.monsterMgr = &g_monsterManager;
    inputCtx.npcMgr = &g_npcManager;
    inputCtx.groundItems = g_groundItems;
    inputCtx.maxGroundItems = MAX_GROUND_ITEMS;
    inputCtx.hudCoords = &g_hudCoords;
    inputCtx.showCharInfo = &g_showCharInfo;
    inputCtx.showInventory = &g_showInventory;
    inputCtx.showSkillWindow = &g_showSkillWindow;
    inputCtx.hoveredNpc = &g_hoveredNpc;
    inputCtx.hoveredMonster = &g_hoveredMonster;
    inputCtx.hoveredGroundItem = &g_hoveredGroundItem;
    inputCtx.selectedNpc = &g_selectedNpc;
    inputCtx.potionBar = g_potionBar;
    inputCtx.skillBar = g_skillBar;
    inputCtx.rmcSkillId = &g_rmcSkillId;
    inputCtx.serverMP = &g_serverMP;
    inputCtx.serverAG = &g_serverAG;
    inputCtx.shopOpen = &g_shopOpen;
    inputCtx.isLearningSkill = &g_isLearningSkill;
    inputCtx.learnedSkills = &g_learnedSkills;
    inputCtx.heroCharacterId = &g_heroCharacterId;
    inputCtx.rightMouseHeld = &g_rightMouseHeld;
    inputCtx.showGameMenu = &g_showGameMenu;
    InputHandler::Init(inputCtx);
    InputHandler::RegisterCallbacks(window);
  }

  // Connect to server via persistent ServerConnection
  g_npcManager.SetTerrainData(&terrainData);
  ServerData serverData;

  // Initialize ClientPacketHandler with game state context
  {
    static ClientGameState gameState;
    gameState.characterName = g_characterName;
    gameState.hero = &g_hero;
    gameState.monsterManager = &g_monsterManager;
    gameState.npcManager = &g_npcManager;
    gameState.vfxManager = &g_vfxManager;
    gameState.terrain = &g_terrain;
    gameState.inventory = g_inventory;
    gameState.equipSlots = g_equipSlots;
    gameState.groundItems = g_groundItems;
    gameState.itemDefs = &g_itemDefs;
    gameState.zen = &g_zen;
    gameState.syncDone = &g_syncDone;
    gameState.shopOpen = &g_shopOpen;
    gameState.shopItems = &g_shopItems;
    gameState.serverLevel = &g_serverLevel;
    gameState.serverHP = &g_serverHP;
    gameState.serverMaxHP = &g_serverMaxHP;
    gameState.serverMP = &g_serverMP;
    gameState.serverMaxMP = &g_serverMaxMP;
    gameState.serverAG = &g_serverAG;
    gameState.serverMaxAG = &g_serverMaxAG;
    gameState.serverStr = &g_serverStr;
    gameState.serverDex = &g_serverDex;
    gameState.serverVit = &g_serverVit;
    gameState.serverEne = &g_serverEne;
    gameState.serverLevelUpPoints = &g_serverLevelUpPoints;
    gameState.serverXP = &g_serverXP;
    gameState.serverDefense = &g_serverDefense;
    gameState.serverAttackSpeed = &g_serverAttackSpeed;
    gameState.serverMagicSpeed = &g_serverMagicSpeed;
    gameState.potionBar = g_potionBar;
    gameState.skillBar = g_skillBar;
    gameState.rmcSkillId = &g_rmcSkillId;
    gameState.heroCharacterId = &g_heroCharacterId;
    gameState.learnedSkills = &g_learnedSkills;
    gameState.spawnDamageNumber = [](const glm::vec3 &pos, int dmg,
                                     uint8_t type) {
      FloatingDamageRenderer::Spawn(pos, dmg, type, g_floatingDmg,
                                    MAX_FLOATING_DAMAGE);
    };
    gameState.getBodyPartIndex = ItemDatabase::GetBodyPartIndex;
    gameState.getBodyPartModelFile = ItemDatabase::GetBodyPartModelFile;
    gameState.getItemRestingAngle = [](int16_t defIdx, glm::vec3 &angle,
                                       float &scale) {
      GroundItemRenderer::GetItemRestingAngle(defIdx, angle, scale);
    };
    ClientPacketHandler::Init(&gameState);
  }

  // Set up unified packet handler — routes based on g_gameState
  g_server.onPacket = [&serverData](const uint8_t *pkt, int size) {
    if (g_gameState == GameState::CHAR_SELECT ||
        g_gameState == GameState::CONNECTING) {
      // Handle character select packets (F3 sub-codes)
      ClientPacketHandler::HandleCharSelectPacket(pkt, size);
    } else if (g_gameState == GameState::LOADING) {
      // Handle initial world data burst
      ClientPacketHandler::HandleInitialPacket(pkt, size, serverData);
    } else {
      // Normal game packets
      ClientPacketHandler::HandleGamePacket(pkt, size);
    }
  };

  // Auto-diagnostic mode: --diag flag captures all debug views and exits
  bool autoDiag = false;
  bool autoScreenshot = false;
  bool autoGif = false;
  int gifFrameCount = 72; // ~3 seconds at 24fps
  int gifDelay = 4;       // centiseconds between frames (4cs = 25fps)
  int objectDebugIdx = -1;
  std::string objectDebugName;
  bool hasCustomPos = false;
  float customX = 0, customY = 0, customZ = 0;
  std::string outputName;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--diag")
      autoDiag = true;
    if (std::string(argv[i]) == "--screenshot")
      autoScreenshot = true;
    if (std::string(argv[i]) == "--debug" && i + 1 < argc) {
      g_terrain.SetDebugMode(std::atoi(argv[i + 1]));
      ++i;
    }
    if (std::string(argv[i]) == "--gif")
      autoGif = true;
    if (std::string(argv[i]) == "--gif-frames" && i + 1 < argc) {
      gifFrameCount = std::atoi(argv[i + 1]);
      ++i;
    }
    if (std::string(argv[i]) == "--pos" && i + 3 < argc) {
      customX = std::atof(argv[i + 1]);
      customY = std::atof(argv[i + 2]);
      customZ = std::atof(argv[i + 3]);
      hasCustomPos = true;
      i += 3;
    }
    if (std::string(argv[i]) == "--output" && i + 1 < argc) {
      outputName = argv[i + 1];
      ++i;
    }
    if (std::string(argv[i]) == "--object-debug" && i + 1 < argc) {
      objectDebugIdx = std::atoi(argv[i + 1]);
      ++i;
    }
  }

  // Initialize CharacterSelect scene
  {
    CharacterSelect::Context csCtx;
    csCtx.server = &g_server;
    csCtx.dataPath = data_path;
    csCtx.window = window;
    csCtx.onCharSelected = [&]() {
      // Server will send world data burst after char select — switch to LOADING
      g_loadingFrames = 0;
      g_gameState = GameState::LOADING;
      // Load a random loading screen image
      if (!g_loadingTex) {
        int idx = (rand() % 3) + 1;
        char path[256];
        snprintf(path, sizeof(path), "%s/Logo/Loading%02d.OZJ",
                 data_path.c_str(), idx);
        g_loadingTex = TextureLoader::LoadOZJ(path);
        if (!g_loadingTex) {
          snprintf(path, sizeof(path), "%s/Local/loading%02d.ozj",
                   data_path.c_str(), idx);
          g_loadingTex = TextureLoader::LoadOZJ(path);
        }
      }
      std::cout << "[State] -> LOADING (waiting for world data)" << std::endl;
    };
    csCtx.onExit = [&]() { glfwSetWindowShouldClose(window, GLFW_TRUE); };
    CharacterSelect::Init(csCtx);
  }

  bool connected = false;
  for (int i = 0; i < 5; ++i) {
    if (g_server.Connect("127.0.0.1", 44405)) {
      connected = true;
      break;
    }
    std::cout << "[Net] Retrying connection in 1s..." << std::endl;
    sleep(1);
  }

  if (!connected) {
    std::cerr << "[Net] FATAL: Could not connect to MU Server. Ensure the "
                 "server is running at 127.0.0.1:44405."
              << std::endl;
    return 1;
  }

  serverData.connected = true;

  // Initialize sound engine before any playback calls
  SoundManager::Init(g_dataPath);

  g_gameState = GameState::CHAR_SELECT;
  SoundManager::PlayMusic(g_dataPath + "/Music/main_theme.mp3");
  std::cout << "[State] -> CHAR_SELECT (waiting for character list)"
            << std::endl;

  // Give server a moment to send character list
  for (int i = 0; i < 10; i++) {
    g_server.Poll();
    usleep(10000);
  }

  int diagFrame = 0;
  const char *diagNames[] = {"normal", "tileindex", "tileuv",
                             "alpha",  "lightmap",  "nolightmap"};

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  ImVec4 clear_color = ImVec4(
      0.0f, 0.0f, 0.0f, 1.00f); // Black: matches edge fog at map boundaries
  float lastFrame = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    glfwPollEvents();

    // Poll persistent network connection for server packets
    g_server.Poll();
    g_server.Flush();

    // ── LOADING state: show loading screen, then process burst ──
    if (g_gameState == GameState::LOADING && !g_worldInitialized) {
      g_loadingFrames++;
      // Render loading screen for a few frames before doing the heavy burst
      if (g_loadingFrames <= 3) {
        // Just poll lightly and continue to render loading screen below
        g_server.Poll();
      } else {
        // Poll aggressively to receive all world data
        for (int burst = 0; burst < 50; burst++) {
          g_server.Poll();
          usleep(10000);
        }
        // Switch packet handler to game mode before initializing
        g_gameState = GameState::INGAME;
        InitGameWorld(serverData);
        g_worldInitialized = true;
        // Cleanup loading texture
        if (g_loadingTex) {
          glDeleteTextures(1, &g_loadingTex);
          g_loadingTex = 0;
        }
        std::cout << "[State] -> INGAME" << std::endl;

        // Apply command-line camera overrides
        if ((autoScreenshot || autoGif) && !hasCustomPos) {
          g_camera.SetPosition(glm::vec3(13000.0f, 350.0f, 13500.0f));
        }
        if (hasCustomPos) {
          g_hero.SetPosition(glm::vec3(customX, customY, customZ));
          g_hero.SnapToTerrain();
          g_camera.SetPosition(g_hero.GetPosition());
        }
        if (objectDebugIdx >= 0 &&
            objectDebugIdx < (int)terrainData.objects.size()) {
          auto &debugObj = terrainData.objects[objectDebugIdx];
          g_hero.SetPosition(debugObj.position);
          g_hero.SnapToTerrain();
          g_camera.SetPosition(g_hero.GetPosition());
          objectDebugName = "obj_type" + std::to_string(debugObj.type) + "_idx" +
                            std::to_string(objectDebugIdx);
          if (!autoGif)
            autoScreenshot = true;
        }
      }
    }

    // ── CHAR_SELECT state: update and render character select scene ──
    if (g_gameState == GameState::CHAR_SELECT ||
        g_gameState == GameState::CONNECTING) {
      // Poll mouse clicks for character slot selection
      {
        static bool prevMouseDown = false;
        bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouseDown && !prevMouseDown && !ImGui::GetIO().WantCaptureMouse) {
          double mx, my;
          glfwGetCursorPos(window, &mx, &my);
          int ww, wh;
          glfwGetWindowSize(window, &ww, &wh);
          CharacterSelect::OnMouseClick(mx, my, ww, wh);
        }
        prevMouseDown = mouseDown;
      }

      CharacterSelect::Update(deltaTime);

      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      glViewport(0, 0, fbW, fbH);

      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);

      // ImGui frame for CharSelect UI
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      CharacterSelect::Render(winW, winH);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      glfwSwapBuffers(window);
      continue; // Skip game world rendering
    }

    // ── LOADING state: show loading screen ──
    if (g_gameState == GameState::LOADING) {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);

      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // Draw loading screen image (centered, aspect-fit)
      if (g_loadingTex) {
        float imgW = 640.0f, imgH = 480.0f; // OZJ loading images are 640x480
        float scale = std::min((float)winW / imgW, (float)winH / imgH);
        float dispW = imgW * scale;
        float dispH = imgH * scale;
        float x0 = (winW - dispW) * 0.5f;
        float y0 = (winH - dispH) * 0.5f;
        dl->AddImage((ImTextureID)(intptr_t)g_loadingTex,
                     ImVec2(x0, y0), ImVec2(x0 + dispW, y0 + dispH));
      }

      // Loading text overlay at bottom
      const char *loadText = "Loading...";
      ImVec2 tsz = ImGui::CalcTextSize(loadText);
      dl->AddText(ImVec2(winW * 0.5f - tsz.x * 0.5f, winH * 0.85f),
                  IM_COL32(220, 200, 160, 255), loadText);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
      continue;
    }

    // ═══════════════════════════════════════════════
    // INGAME state: normal game world update + render
    // ═══════════════════════════════════════════════
    InputHandler::ProcessInput(window, deltaTime);
    g_camera.Update(deltaTime);

    // Send player position to server periodically (~4Hz)
    {
      // Tick potion cooldown
      if (g_potionCooldown > 0.0f)
        g_potionCooldown = std::max(0.0f, g_potionCooldown - deltaTime);

      static float posTimer = 0.0f;
      static int lastGridX = -1, lastGridY = -1;
      posTimer += deltaTime;
      if (posTimer >= 0.25f) {
        posTimer = 0.0f;
        glm::vec3 hp = g_hero.GetPosition();
        g_server.SendPrecisePosition(hp.x, hp.z);

        // Also send grid move when grid cell changes (for DB persistence)
        int gx = (int)(hp.z / 100.0f);
        int gy = (int)(hp.x / 100.0f);
        if (gx != lastGridX || gy != lastGridY) {
          g_server.SendGridMove((uint8_t)gx, (uint8_t)gy);
          lastGridX = gx;
          lastGridY = gy;
        }
      }
    }

    // Update 3D audio listener to hero position
    {
      glm::vec3 lp = g_hero.GetPosition();
      SoundManager::UpdateListener(lp.x, lp.y, lp.z);
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
    g_monsterManager.Update(deltaTime);

    // Hero combat: update attack state machine, send attack packet on hit
    // Block all combat in safe zone — but don't stop movement
    {
      bool nowInSafe = g_hero.IsInSafeZone();
      static bool wasInSafe = false;
      if (nowInSafe) {
        // On transition INTO safe zone: cancel any active attack once
        if (!wasInSafe &&
            (g_hero.GetAttackTarget() >= 0 || g_hero.IsAttacking())) {
          g_hero.CancelAttack();
        }
        // Don't update attack/state while in safe zone
      } else {
        g_hero.UpdateAttack(deltaTime);
        g_hero.UpdateState(deltaTime);
        if (g_hero.CheckAttackHit()) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx >= 0 &&
              targetIdx < g_monsterManager.GetMonsterCount()) {
            uint16_t serverIdx = g_monsterManager.GetServerIndex(targetIdx);
            uint8_t skillId = g_hero.GetActiveSkillId();
            if (skillId > 0) {
              // Re-check resource before sending — may have been spent since
              // the initial right-click
              int cost = InventoryUI::GetSkillResourceCost(skillId);
              bool isDK = (g_hero.GetClass() == 16);
              int curResource = isDK ? g_serverAG : g_serverMP;
              if (curResource >= cost) {
                std::cout << "[Skill] HIT! SendSkillAttack monIdx=" << serverIdx
                          << " skillId=" << (int)skillId << std::endl;
                if (skillId == 12) {
                  // Flash: delay damage until beam spawns at frame 7.0
                  g_hero.SetPendingAquaPacket(serverIdx, skillId, 0.0f, 0.0f);
                } else {
                  g_server.SendSkillAttack(serverIdx, skillId);
                }
              } else {
                InventoryUI::ShowNotification(isDK ? "Not enough AG!" : "Not enough Mana!");
              }
            } else {
              g_server.SendAttack(serverIdx);
            }
          }
        }
        // Flash: send delayed damage packet when beam spawns at frame 7.0
        {
          uint16_t aqTarget; uint8_t aqSkill; float aqX, aqZ;
          if (g_hero.PopPendingAquaPacket(aqTarget, aqSkill, aqX, aqZ)) {
            g_server.SendSkillAttack(aqTarget, aqSkill, aqX, aqZ);
          }
        }
        // Auto-attack: re-engage after cooldown if target still alive
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() >= 0) {
          int targetIdx = g_hero.GetAttackTarget();
          if (targetIdx < g_monsterManager.GetMonsterCount()) {
            MonsterInfo mi = g_monsterManager.GetMonsterInfo(targetIdx);
            bool targetDead = (mi.state == MonsterState::DYING ||
                               mi.state == MonsterState::DEAD || mi.hp <= 0);
            if (targetDead && g_rightMouseHeld && g_rmcSkillId >= 0) {
              // RMC held + target died — switch to hovered monster if any
              if (g_hoveredMonster >= 0 &&
                  g_hoveredMonster < g_monsterManager.GetMonsterCount()) {
                MonsterInfo hmi =
                    g_monsterManager.GetMonsterInfo(g_hoveredMonster);
                if (hmi.state != MonsterState::DYING &&
                    hmi.state != MonsterState::DEAD && hmi.hp > 0) {
                  uint8_t skillId = (uint8_t)g_rmcSkillId;
                  int cost = InventoryUI::GetSkillResourceCost(skillId);
                  bool isDK = (g_hero.GetClass() == 16);
                  int curResource = isDK ? g_serverAG : g_serverMP;
                  if (curResource >= cost) {
                    g_hero.SkillAttackMonster(g_hoveredMonster, hmi.position,
                                              skillId);
                  } else {
                    InventoryUI::ShowNotification(
                        isDK ? "Not enough AG!" : "Not enough Mana!");
                    g_hero.CancelAttack();
                  }
                } else {
                  g_hero.CancelAttack();
                }
              } else {
                g_hero.CancelAttack();
              }
            } else if (targetDead) {
              // Target died, no RMC — clear
              g_hero.CancelAttack();
            } else if (g_rightMouseHeld && g_rmcSkillId >= 0) {
              // RMC held + target alive — check if hovered a different monster
              int nextTarget = targetIdx;
              glm::vec3 nextPos = mi.position;
              if (g_hoveredMonster >= 0 && g_hoveredMonster != targetIdx &&
                  g_hoveredMonster < g_monsterManager.GetMonsterCount()) {
                MonsterInfo hmi =
                    g_monsterManager.GetMonsterInfo(g_hoveredMonster);
                if (hmi.state != MonsterState::DYING &&
                    hmi.state != MonsterState::DEAD && hmi.hp > 0) {
                  nextTarget = g_hoveredMonster;
                  nextPos = hmi.position;
                }
              }
              uint8_t skillId = (uint8_t)g_rmcSkillId;
              int cost = InventoryUI::GetSkillResourceCost(skillId);
              bool isDK = (g_hero.GetClass() == 16);
              int curResource = isDK ? g_serverAG : g_serverMP;
              if (curResource >= cost) {
                g_hero.SkillAttackMonster(nextTarget, nextPos, skillId);
              } else {
                InventoryUI::ShowNotification(
                    isDK ? "Not enough AG!" : "Not enough Mana!");
                g_hero.CancelAttack();
              }
            } else if (g_hero.GetActiveSkillId() == 0) {
              // Normal attack auto-re-engage
              g_hero.AttackMonster(targetIdx, mi.position);
            }
          }
        }

        // Self-AoE continuous casting (Twister/Evil Spirit/Hellfire): re-cast when GCD expires + RMB held
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() < 0 && g_rightMouseHeld &&
            (g_rmcSkillId == 8 || g_rmcSkillId == 9 || g_rmcSkillId == 10) &&
            g_hero.GetGlobalCooldown() <= 0.0f) {
          uint8_t skillId = (uint8_t)g_rmcSkillId;
          int cost = InventoryUI::GetSkillResourceCost(skillId);
          bool isDK = (g_hero.GetClass() == 16);
          int curResource = isDK ? g_serverAG : g_serverMP;
          if (curResource >= cost) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            glm::vec3 groundTarget;
            if (RayPicker::ScreenToTerrain(window, mx, my, groundTarget)) {
              g_hero.CastSelfAoE(skillId, groundTarget);
              // Evil Spirit: caster-centered AoE, send player position
              // Twister: directional, send click position for line path
              glm::vec3 heroPos = g_hero.GetPosition();
              // Evil Spirit/Hellfire: caster-centered, Twister: directional
              float atkX = (skillId == 9 || skillId == 10) ? heroPos.x : groundTarget.x;
              float atkZ = (skillId == 9 || skillId == 10) ? heroPos.z : groundTarget.z;
              if (skillId == 12) {
                g_hero.SetPendingAquaPacket(0xFFFF, skillId, atkX, atkZ);
              } else {
                g_server.SendSkillAttack(0xFFFF, skillId, atkX, atkZ);
              }
              // Optimistically deduct mana to prevent spam before server reply
              if (isDK) g_serverAG -= cost; else g_serverMP -= cost;
            }
          }
        }
      }
      wasInSafe = nowInSafe;
    }

    // Skill learning: play heal animation over 3 seconds, then return to idle
    if (g_isLearningSkill) {
      if (g_learnSkillTimer == 0.0f)
        SoundManager::Play(SOUND_JEWEL01); // eGem.wav on learn start
      g_learnSkillTimer += deltaTime;
      // Stop movement/attack only when needed (StopMoving resets action/frame)
      if (g_hero.IsMoving())
        g_hero.StopMoving();
      if (g_hero.IsAttacking())
        g_hero.CancelAttack();
      // Set heal animation AFTER stop (stop resets action to idle)
      g_hero.SetSlowAnimDuration(LEARN_SKILL_DURATION);
      g_hero.SetAction(HeroCharacter::ACTION_SKILL_VITALITY);
      if (g_learnSkillTimer >= LEARN_SKILL_DURATION) {
        g_isLearningSkill = false;
        g_learnSkillTimer = 0.0f;
        g_learningSkillId = 0;
        g_hero.SetSlowAnimDuration(0.0f);
        // In safe zone, always use normal idle (weapon on back)
        if (g_hero.IsInSafeZone() || !g_hero.HasWeapon())
          g_hero.SetAction(HeroCharacter::ACTION_STOP_MALE);
        else
          g_hero.SetAction(g_hero.weaponIdleAction());
      }
    }

    // Town teleport: play heal animation, then warp to Lorencia safe zone
    if (g_teleportingToTown) {
      g_teleportTimer -= deltaTime;
      g_hero.SetSlowAnimDuration(TELEPORT_CAST_TIME);
      g_hero.SetAction(HeroCharacter::ACTION_SKILL_VITALITY);
      if (g_teleportTimer <= 0.0f) {
        g_teleportingToTown = false;
        // Teleport to Lorencia safe zone (grid 125,125)
        const int S = TerrainParser::TERRAIN_SIZE;
        int startGX = 125, startGZ = 125;
        glm::vec3 spawnPos(12500.0f, 0.0f, 12500.0f);
        for (int radius = 0; radius < 30; radius++) {
          bool found = false;
          for (int dy = -radius; dy <= radius && !found; dy++) {
            for (int dx = -radius; dx <= radius && !found; dx++) {
              if (radius > 0 && std::abs(dx) != radius &&
                  std::abs(dy) != radius)
                continue;
              int cx = startGX + dx, cz = startGZ + dy;
              if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
                continue;
              uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
              if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
                spawnPos =
                    glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f);
                found = true;
              }
            }
          }
          if (found)
            break;
        }
        // Stop all sounds on teleport (prevents 3D bleed from old location)
        SoundManager::StopAll();
        g_hero.SetPosition(spawnPos);
        g_hero.SnapToTerrain();
        g_hero.SetSlowAnimDuration(0.0f);
        g_hero.SetAction(1); // Back to idle
        g_camera.SetPosition(g_hero.GetPosition());
        g_server.SendPrecisePosition(spawnPos.x, spawnPos.z);
      }
    }

    // Hero respawn: after death timer expires, respawn in Lorencia safe zone
    if (g_hero.ReadyToRespawn()) {
      // Find walkable safe zone tile (same spiral search as init)
      const int S = TerrainParser::TERRAIN_SIZE;
      int startGX = 125, startGZ = 125;
      glm::vec3 spawnPos(12500.0f, 0.0f, 12500.0f);
      for (int radius = 0; radius < 30; radius++) {
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              spawnPos =
                  glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f);
              found = true;
            }
          }
        }
        if (found)
          break;
      }
      g_hero.Respawn(spawnPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
      g_serverHP = g_serverMaxHP; // Reset HUD HP
      g_serverMP = g_serverMaxMP; // Reset AG/Mana on respawn

      // Notify server that player is alive (clears session.dead)
      g_server.SendCharSave(
          1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr,
          (uint16_t)g_serverDex, (uint16_t)g_serverVit, (uint16_t)g_serverEne,
          (uint16_t)g_serverMaxHP, (uint16_t)g_serverMaxHP,
          (uint16_t)g_serverMaxMP, (uint16_t)g_serverMaxMP,
          (uint16_t)g_serverMaxAG, (uint16_t)g_serverMaxAG,
          (uint16_t)g_serverLevelUpPoints, (uint64_t)g_serverXP, g_skillBar,
          g_potionBar, g_rmcSkillId);
    }

    // Periodic autosave (quickslots, stats) every 60 seconds
    g_autoSaveTimer += deltaTime;
    if (g_autoSaveTimer >= AUTOSAVE_INTERVAL && !g_hero.IsDead()) {
      g_autoSaveTimer = 0.0f;
      g_server.SendCharSave(
          1, (uint16_t)g_serverLevel, (uint16_t)g_serverStr,
          (uint16_t)g_serverDex, (uint16_t)g_serverVit, (uint16_t)g_serverEne,
          (uint16_t)g_serverHP, (uint16_t)g_serverMaxHP, (uint16_t)g_serverMP,
          (uint16_t)g_serverMaxMP, (uint16_t)g_serverAG,
          (uint16_t)g_serverMaxAG, (uint16_t)g_serverLevelUpPoints,
          (uint64_t)g_serverXP, g_skillBar, g_potionBar, g_rmcSkillId);
    }

    // Auto-pickup: walk near a ground item to pick it up
    {
      glm::vec3 heroPos = g_hero.GetPosition();
      for (auto &gi : g_groundItems) {
        if (!gi.active)
          continue;
        gi.timer += deltaTime;
        // Snap drop Y to terrain
        if (gi.position.y == 0.0f && g_terrainDataPtr) {
          float gx = gi.position.z / 100.0f;
          float gz = gi.position.x / 100.0f;
          int ix = (int)gx, iz = (int)gz;
          if (ix >= 0 && iz >= 0 && ix < 256 && iz < 256) {
            float h = g_terrainDataPtr->heightmap[iz * 256 + ix] * 1.5f;
            gi.position.y = h + 0.5f;
          }
        }
        float dist = glm::length(
            glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        // Auto-pickup Zen only (items require explicit click)
        if (gi.defIndex == -1 && dist < 120.0f && !g_hero.IsDead()) {
          g_server.SendPickup(gi.dropIndex);
          gi.active = false; // Optimistic remove (sound plays on server confirm)
        }
        // Despawn after 60s
        if (gi.timer > 60.0f)
          gi.active = false;
      }
    }

    // Roof hiding: read layer1 tile at hero position, fade types 125/126
    if (g_terrainDataPtr) {
      glm::vec3 heroPos = g_hero.GetPosition();
      const int S = TerrainParser::TERRAIN_SIZE;
      int gz = (int)(heroPos.x / 100.0f);
      int gx = (int)(heroPos.z / 100.0f);
      uint8_t heroTile = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroTile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
      // Original: HeroTile == 4 hides roof meshes
      float target = (heroTile == 4) ? 0.0f : 1.0f;
      g_typeAlphaTarget[125] = target;
      g_typeAlphaTarget[126] = target;
      // Fast fade — nearly instant (95%+ in 1-2 frames)
      float blend = 1.0f - std::exp(-20.0f * deltaTime);
      for (auto &[type, alpha] : g_typeAlpha) {
        alpha += (g_typeAlphaTarget[type] - alpha) * blend;
      }
      g_objectRenderer.SetTypeAlpha(g_typeAlpha);

      // SafeZone detection: attribute 0x01 = TW_SAFEZONE
      uint8_t heroAttr = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroAttr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
      bool wasInSafeZone = g_hero.IsInSafeZone();
      bool nowInSafeZone = (heroAttr & 0x01) != 0;
      g_hero.SetInSafeZone(nowInSafeZone);
      // Main 5.2: stop wind in safe zone, restart outside
      // Music: MuTheme in safe zone, stop outside
      if (nowInSafeZone && !wasInSafeZone) {
        SoundManager::Stop(SOUND_WIND01);
        SoundManager::PlayMusic(g_dataPath + "/Music/MuTheme.mp3");
      } else if (!nowInSafeZone && wasInSafeZone) {
        SoundManager::PlayLoop(SOUND_WIND01);
        SoundManager::StopMusic();
      }
    }

    // Auto-screenshot/diagnostic camera override
    if ((autoScreenshot || autoDiag) && diagFrame == 60) {
      glm::vec3 hPos = g_hero.GetPosition();
      std::cout << "[Screenshot] Overriding camera to hero at (" << hPos.x
                << ", " << hPos.y << ", " << hPos.z << ") for capture."
                << std::endl;
      g_camera.SetPosition(hPos);
    }

    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 0) {
        g_terrain.SetDebugMode(mode);
      }
    }

    // Use framebuffer size for viewport (Retina displays are 2x window size)
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    glm::mat4 projection =
        g_camera.GetProjectionMatrix((float)winW, (float)winH);
    glm::mat4 view = g_camera.GetViewMatrix();
    glm::vec3 camPos = g_camera.GetPosition();

    // Sky renders first (behind everything, no depth write)
    g_sky.Render(view, projection, camPos);

    // Main 5.2: AddTerrainLight — merge world point lights + spell projectile lights
    // Spell lights are transient (per-frame), so rebuild the full list each frame.
    {
      std::vector<glm::vec3> lightPos, lightCol;
      std::vector<float> lightRange;
      std::vector<int> lightObjTypes;
      // Static world lights (fires, streetlights, candles, etc.)
      for (auto &pl : g_pointLights) {
        lightPos.push_back(pl.position);
        lightCol.push_back(pl.color);
        lightRange.push_back(pl.range);
        lightObjTypes.push_back(pl.objectType);
      }
      // Dynamic spell lights from active projectiles
      g_vfxManager.GetActiveSpellLights(lightPos, lightCol, lightRange,
                                        lightObjTypes);
      // Update terrain (CPU lightmap) and object renderer (shader uniforms)
      g_terrain.SetPointLights(lightPos, lightCol, lightRange, lightObjTypes);
      g_objectRenderer.SetPointLights(lightPos, lightCol, lightRange);
      // Update character renderers with merged PointLight list
      std::vector<PointLight> mergedLights;
      mergedLights.reserve(lightPos.size());
      for (size_t i = 0; i < lightPos.size(); ++i) {
        mergedLights.push_back(
            {lightPos[i], lightCol[i], lightRange[i],
             i < lightObjTypes.size() ? lightObjTypes[i] : 0});
      }
      g_hero.SetPointLights(mergedLights);
      g_npcManager.SetPointLights(mergedLights);
      g_monsterManager.SetPointLights(mergedLights);
      g_boidManager.SetPointLights(mergedLights);
    }

    g_terrain.Render(view, projection, currentFrame, camPos);

    // Render world objects first (before grass, so tall grass billboards
    // don't block thin fence bar meshes via depth buffer)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_objectRenderer.Render(view, projection, g_camera.GetPosition(),
                            currentFrame);

    // Render grass billboards (after objects so grass doesn't occlude fences)
    {
      std::vector<GrassRenderer::PushSource> pushSources;
      pushSources.push_back({g_hero.GetPosition(), 100.0f});
      g_grass.Render(view, projection, currentFrame, camPos, pushSources);
    }

    // Main 5.2 level-up VFX: 15 BITMAP_FLARE joints in a ring
    if (g_hero.LeveledUpThisFrame()) {
      g_vfxManager.SpawnLevelUpEffect(g_hero.GetPosition());
      g_hero.ClearLevelUpFlag();
    }

    // Update effects (VFX rendered after characters for correct layering)
    g_fireEffect.Update(deltaTime);
    g_vfxManager.UpdateLevelUpCenter(g_hero.GetPosition());
    g_vfxManager.Update(deltaTime);

    // Twister proximity: apply StormTime spin when tornado VFX reaches a monster
    if (g_vfxManager.HasActiveTwisters()) {
      int monCount = g_monsterManager.GetMonsterCount();
      for (int mi = 0; mi < monCount; ++mi) {
        MonsterInfo info = g_monsterManager.GetMonsterInfo(mi);
        if (info.hp <= 0) continue;
        if (g_vfxManager.CheckTwisterHit(info.serverIndex, info.position))
          g_monsterManager.ApplyStormTime(info.serverIndex, 10);
      }
    }

    // Evil Spirit: StormTime is server-authoritative (applied in DAMAGE packet)
    // No client-side proximity check — beams travel farther than server AoE range

    g_boidManager.Update(deltaTime, g_hero.GetPosition(), 0, currentFrame);
    g_fireEffect.Render(view, projection);

    // Render ambient creatures (birds/fish) before characters
    g_boidManager.RenderShadows(view, projection);
    g_boidManager.Render(view, projection, camPos);
    g_boidManager.RenderLeaves(view, projection);

    // Render NPC characters with shadows
    g_npcManager.RenderShadows(view, projection);
    g_npcManager.Render(view, projection, camPos, deltaTime);

    // Render monsters with shadows
    g_monsterManager.RenderShadows(view, projection);
    g_monsterManager.Render(view, projection, camPos, deltaTime);

    // Silhouette outline on hovered NPC/monster (stencil-based)
    if (g_hoveredNpc >= 0)
      g_npcManager.RenderSilhouetteOutline(g_hoveredNpc, view, projection);
    if (g_hoveredMonster >= 0)
      g_monsterManager.RenderSilhouetteOutline(g_hoveredMonster, view,
                                               projection);

    // Render ground item shadows (before hero so items don't shadow-over hero)
    GroundItemRenderer::RenderShadows(g_groundItems, MAX_GROUND_ITEMS, view,
                                      projection);

    // Render hero shadow BEFORE hero model so character draws on top
    g_clickEffect.Render(view, projection, deltaTime, g_hero.GetShader());
    g_hero.RenderShadow(view, projection);
    g_hero.Render(view, projection, camPos, deltaTime);

    // Compute hero bone world positions for VFX bone-attached particles
    {
      const auto &bones = g_hero.GetCachedBones();
      float facing = g_hero.GetFacing();
      glm::vec3 heroPos = g_hero.GetPosition();
      float cosF = cosf(facing), sinF = sinf(facing);
      std::vector<glm::vec3> boneWorldPos(bones.size());
      for (int i = 0; i < (int)bones.size(); ++i) {
        // Translation column of 3x4 bone matrix (model-local space)
        float bx = bones[i][0][3];
        float by = bones[i][1][3];
        float bz = bones[i][2][3];
        // Apply facing rotation in MU space (same as shadow HeroCharacter.cpp:994)
        float rx = bx * cosF - by * sinF;
        float ry = bx * sinF + by * cosF;
        // Full model transform: translate * rotZ(-90) * rotY(-90) * rotZ(facing)
        // After facing rotation in MU space: (rx, ry, bz)
        // After rotY(-90): (-bz, ry, rx)
        // After rotZ(-90): (ry, bz, rx)
        boneWorldPos[i] = heroPos + glm::vec3(ry, bz, rx);
      }
      g_vfxManager.SetHeroBonePositions(boneWorldPos);
    }

    // Render VFX (after all characters so particles layer on top)
    g_vfxManager.Render(view, projection);

    // Auto-GIF: capture with warmup for fire particle buildup
    // Capture BEFORE ImGui rendering so debug overlay is not in the output
    if (autoGif && !Screenshot::IsRecording() && diagFrame == 0) {
      std::string gifPath =
          !outputName.empty()
              ? "screenshots/" + outputName + ".gif"
              : (objectDebugName.empty()
                     ? "screenshots/fire_effect.gif"
                     : "screenshots/" + objectDebugName + ".gif");
      Screenshot::StartRecording(window, gifPath, gifFrameCount, gifDelay);
      std::cout << "[GIF] Starting capture (" << gifFrameCount << " frames)"
                << std::endl;
    }
    if (Screenshot::TickRecording(window)) {
      break; // GIF saved, exit
    }

    // Auto-screenshot flag (capture happens after ImGui render to include
    // HUD)
    bool captureScreenshot = (autoScreenshot && diagFrame == 60);

    // Start the Dear ImGui frame
    InventoryUI::ClearRenderQueue();
    InventoryUI::ResetPendingTooltip(); // Reset deferred tooltip each frame
    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Simplified ImGui HUD
    {
      // Unified bottom HUD bar (HP, QWER, 1234, RMC, AG, XP)
      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // FPS counter (top-left)
      {
        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%.0f", 1.0f / std::max(deltaTime, 0.001f));
        dl->AddText(ImVec2(5, 4), IM_COL32(200, 200, 200, 160), fpsText);
      }

      InventoryUI::RenderQuickbar(dl, g_hudCoords);

      // ── Floating damage numbers ──
      FloatingDamageRenderer::UpdateAndRender(
          g_floatingDmg, MAX_FLOATING_DAMAGE, deltaTime, dl, g_fontDefault,
          view, projection, winW, winH);

      // ── Monster nameplates ──
      g_monsterManager.RenderNameplates(dl, g_fontDefault, view, projection,
                                        winW, winH, camPos, g_hoveredMonster,
                                        g_hero.GetAttackTarget());

      // ── Ground item 3D models + physics ──
      GroundItemRenderer::RenderModels(
          g_groundItems, MAX_GROUND_ITEMS, deltaTime, view, projection,
          [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });

      // ── Ground item labels + tooltips ──
      GroundItemRenderer::RenderLabels(
          g_groundItems, MAX_GROUND_ITEMS, dl, g_fontDefault, view, projection,
          winW, winH, camPos, g_hoveredGroundItem, g_itemDefs);
    }

    // NPC name labels
    g_npcManager.RenderLabels(ImGui::GetForegroundDrawList(), view, projection,
                              winW, winH, camPos, g_hoveredNpc);

    // NPC click interaction dialog has been replaced with direct shop opening
    // through InputHandler.cpp (SendShopOpen). Optionally we could keep
    // g_selectedNpc for highlighting purposes without rendering a dialog.
    if (g_selectedNpc >= 0) {
      // Close selection on Escape
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        g_selectedNpc = -1;
    }

    // ── Character Info and Inventory panels ──
    ImDrawList *panelDl = ImGui::GetForegroundDrawList();
    if (g_shopOpen)
      InventoryUI::RenderShopPanel(panelDl, g_hudCoords);
    if (g_showCharInfo)
      InventoryUI::RenderCharInfoPanel(panelDl, g_hudCoords);
    if (g_showSkillWindow)
      InventoryUI::RenderSkillPanel(panelDl, g_hudCoords);
    if (g_showInventory || g_shopOpen) {
      bool wasShowInvent = g_showInventory;
      g_showInventory = true; // force flag for proper layout parsing
      InventoryUI::RenderInventoryPanel(panelDl, g_hudCoords);
      g_showInventory = wasShowInvent;
    }

    // Skill drag cursor — always on top of everything
    InventoryUI::RenderSkillDragCursor(panelDl);

    // ── Game Menu (ESC menu) ──
    if (g_showGameMenu) {
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      ImVec2 dispSize = ImGui::GetIO().DisplaySize;

      // Full-screen semi-transparent dark overlay
      dl->AddRectFilled(ImVec2(0, 0), dispSize, IM_COL32(0, 0, 0, 160));

      // Centered panel
      float panelW = 250.0f, panelH = 180.0f;
      float px = (dispSize.x - panelW) * 0.5f;
      float py = (dispSize.y - panelH) * 0.5f;
      ImVec2 pMin(px, py), pMax(px + panelW, py + panelH);

      // Panel background and border (matches InventoryUI style)
      dl->AddRectFilled(pMin, pMax, IM_COL32(15, 15, 25, 240), 4.0f);
      dl->AddRect(pMin, pMax, IM_COL32(120, 100, 60, 255), 4.0f, 0, 1.5f);

      // Title: "Game Menu"
      const char *title = "Game Menu";
      ImVec2 titleSize = ImGui::CalcTextSize(title);
      float titleX = px + (panelW - titleSize.x) * 0.5f;
      dl->AddText(ImVec2(titleX, py + 14.0f), IM_COL32(255, 210, 80, 255), title);

      // Button dimensions and positions
      float btnW = 180.0f, btnH = 36.0f;
      float btnX = px + (panelW - btnW) * 0.5f;
      float btn1Y = py + 55.0f;  // "Switch Character"
      float btn2Y = py + 110.0f; // "Exit Game"

      ImVec2 mousePos = ImGui::GetIO().MousePos;
      bool mouseClicked = ImGui::GetIO().MouseClicked[0];

      // Helper lambda for buttons
      auto drawButton = [&](float bx, float by, float bw, float bh,
                            const char *label) -> bool {
        ImVec2 bMin(bx, by), bMax(bx + bw, by + bh);
        bool hovered = mousePos.x >= bx && mousePos.x <= bx + bw &&
                       mousePos.y >= by && mousePos.y <= by + bh;
        ImU32 bgCol = hovered ? IM_COL32(60, 50, 30, 255)
                              : IM_COL32(35, 30, 20, 255);
        ImU32 borderCol = hovered ? IM_COL32(200, 170, 60, 255)
                                  : IM_COL32(120, 100, 60, 200);
        dl->AddRectFilled(bMin, bMax, bgCol, 3.0f);
        dl->AddRect(bMin, bMax, borderCol, 3.0f, 0, 1.2f);
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float lx = bx + (bw - labelSize.x) * 0.5f;
        float ly = by + (bh - labelSize.y) * 0.5f;
        ImU32 textCol = hovered ? IM_COL32(255, 230, 150, 255)
                                : IM_COL32(200, 190, 160, 255);
        dl->AddText(ImVec2(lx, ly), textCol, label);
        return hovered && mouseClicked;
      };

      // "Switch Character" button
      if (drawButton(btnX, btn1Y, btnW, btnH, "Switch Character")) {
        SoundManager::Play(SOUND_CLICK01);
        SoundManager::StopAll();
        SoundManager::StopMusic();
        SoundManager::PlayMusic(g_dataPath + "/Music/main_theme.mp3");
        g_server.SendCharListRequest();
        g_showGameMenu = false;
        g_hero.StopMoving();
        InputHandler::ResetGameReady();
        g_gameState = GameState::CHAR_SELECT;
        g_worldInitialized = false;
        // Re-init character select scene
        CharacterSelect::Context csCtx;
        csCtx.server = &g_server;
        csCtx.dataPath = data_path;
        csCtx.window = window;
        csCtx.onCharSelected = [&]() {
          g_loadingFrames = 0;
          g_gameState = GameState::LOADING;
          if (!g_loadingTex) {
            int idx = (rand() % 3) + 1;
            char path[256];
            snprintf(path, sizeof(path), "%s/Logo/Loading%02d.OZJ",
                     data_path.c_str(), idx);
            g_loadingTex = TextureLoader::LoadOZJ(path);
            if (!g_loadingTex) {
              snprintf(path, sizeof(path), "%s/Local/loading%02d.ozj",
                       data_path.c_str(), idx);
              g_loadingTex = TextureLoader::LoadOZJ(path);
            }
          }
          std::cout << "[State] -> LOADING (waiting for world data)" << std::endl;
        };
        csCtx.onExit = [&]() { glfwSetWindowShouldClose(window, GLFW_TRUE); };
        CharacterSelect::Init(csCtx);
      }

      // "Exit Game" button
      if (drawButton(btnX, btn2Y, btnW, btnH, "Exit Game")) {
        SoundManager::Play(SOUND_CLICK01);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Flatten render queue (items on top of UI)
    // Scale logical pixel coords to physical framebuffer pixels (HiDPI/Retina
    // fix)
    {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      float scaleX = (float)fbW / ImGui::GetIO().DisplaySize.x;
      float scaleY = (float)fbH / ImGui::GetIO().DisplaySize.y;
      for (const auto &job : InventoryUI::GetRenderQueue()) {
        int px = (int)(job.x * scaleX);
        int py = (int)(job.y * scaleY);
        int pw = (int)(job.w * scaleX);
        int ph = (int)(job.h * scaleY);
        ItemModelManager::RenderItemUI(job.modelFile, job.defIndex, px, py, pw,
                                       ph, job.hovered);
      }
    }

    // Second ImGui pass: draw deferred tooltip and HUD overlays ON TOP of 3D
    // items
    if (InventoryUI::HasPendingTooltip() ||
        InventoryUI::HasDeferredOverlays() ||
        InventoryUI::HasDeferredCooldowns()) {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      if (InventoryUI::HasDeferredCooldowns()) {
        InventoryUI::FlushDeferredCooldowns();
      }

      if (InventoryUI::HasDeferredOverlays()) {
        InventoryUI::FlushDeferredOverlays();
      }

      if (InventoryUI::HasPendingTooltip()) {
        InventoryUI::FlushPendingTooltip();
      }

      InventoryUI::UpdateAndRenderNotification(deltaTime);

      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // Auto-screenshot: capture AFTER ImGui render (includes HUD overlay)
    if (captureScreenshot) {
      std::string ssPath;
      if (!outputName.empty()) {
        ssPath = "screenshots/" + outputName + ".jpg";
      } else if (!objectDebugName.empty()) {
        ssPath = "screenshots/" + objectDebugName + ".jpg";
      } else {
        ssPath =
            "screenshots/verif_" + std::to_string(std::time(nullptr)) + ".jpg";
      }
      int sw, sh;
      glfwGetFramebufferSize(window, &sw, &sh);
      std::vector<unsigned char> px(sw * sh * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      std::vector<unsigned char> flipped(sw * sh * 3);
      for (int y = 0; y < sh; ++y)
        memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
      tjhandle comp = tjInitCompress();
      unsigned char *jbuf = nullptr;
      unsigned long jsize = 0;
      tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                  TJSAMP_444, 95, TJFLAG_FASTDCT);
      std::filesystem::create_directories("screenshots");
      FILE *f = fopen(ssPath.c_str(), "wb");
      if (f) {
        fwrite(jbuf, 1, jsize, f);
        fclose(f);
      }
      tjFree(jbuf);
      tjDestroy(comp);
      std::cout << "[screenshot] Saved " << ssPath << std::endl;
      break;
    }

    // Auto-diagnostic: capture AFTER render, BEFORE swap (back buffer has
    // current frame)
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 1) {
        std::string diagPath =
            "screenshots/diag_" + std::string(diagNames[mode]) + ".jpg";
        int sw, sh;
        glfwGetFramebufferSize(window, &sw, &sh);
        std::vector<unsigned char> px(sw * sh * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, sw, sh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        std::vector<unsigned char> flipped(sw * sh * 3);
        for (int y = 0; y < sh; ++y)
          memcpy(&flipped[y * sw * 3], &px[(sh - 1 - y) * sw * 3], sw * 3);
        tjhandle comp = tjInitCompress();
        unsigned char *jbuf = nullptr;
        unsigned long jsize = 0;
        tjCompress2(comp, flipped.data(), sw, 0, sh, TJPF_RGB, &jbuf, &jsize,
                    TJSAMP_444, 95, TJFLAG_FASTDCT);
        std::filesystem::create_directories("screenshots");
        FILE *f = fopen(diagPath.c_str(), "wb");
        if (f) {
          fwrite(jbuf, 1, jsize, f);
          fclose(f);
        }
        tjFree(jbuf);
        tjDestroy(comp);
        std::cout << "[diag] Saved " << diagPath << std::endl;
      } else if (mode >= 6) {
        break;
      }
    }
    if (autoDiag || autoScreenshot || autoGif)
      diagFrame++;

    // Per-frame GL error check (only first 10 frames to avoid log spam)
    {
      static int frameNum = 0;
      if (frameNum < 10)
        checkGLError(("frame " + std::to_string(frameNum)).c_str());
      frameNum++;
    }

    glfwSwapBuffers(window);
  }

  // Save character stats to server before disconnecting
  if (g_worldInitialized) {
    g_server.SendCharSave(
        (uint16_t)g_heroCharacterId, (uint16_t)g_serverLevel,
        (uint16_t)g_serverStr, (uint16_t)g_serverDex, (uint16_t)g_serverVit,
        (uint16_t)g_serverEne, (uint16_t)g_serverHP, (uint16_t)g_serverMaxHP,
        (uint16_t)g_serverMP, (uint16_t)g_serverMaxMP, (uint16_t)g_serverAG,
        (uint16_t)g_serverMaxAG, (uint16_t)g_serverLevelUpPoints,
        (uint64_t)g_serverXP, g_skillBar, g_potionBar, g_rmcSkillId);
    g_server.Flush();
  }

  // Disconnect from server
  g_server.Disconnect();
  // Cleanup
  SoundManager::Shutdown();
  CharacterSelect::Shutdown();
  g_monsterManager.Cleanup();
  g_boidManager.Cleanup();
  g_npcManager.Cleanup();
  g_hero.Cleanup();
  g_clickEffect.Cleanup();
  g_sky.Cleanup();
  g_fireEffect.Cleanup();
  g_objectRenderer.Cleanup();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  // StreamRedirector handles restoration and deletion

  return 0;
}

// ═══════════════════════════════════════════════════════════════════
// InitGameWorld — called once after character select, when server
// has sent all initial world data (NPCs, monsters, equipment, stats)
// ═══════════════════════════════════════════════════════════════════

static void InitGameWorld(ServerData &serverData) {
  std::string data_path = g_dataPath;

  // Shut down character select scene (free World74 resources)
  CharacterSelect::Shutdown();

  if (serverData.connected && !serverData.npcs.empty()) {
    g_npcManager.InitModels(data_path);
    for (auto &npc : serverData.npcs) {
      g_npcManager.AddNpcByType(npc.type, npc.gridX, npc.gridY, npc.dir,
                                npc.serverIndex);
    }
    std::cout << "[NPC] Loaded " << serverData.npcs.size()
              << " NPCs from server" << std::endl;
  } else {
    std::cout << "[NPC] No server connection, using hardcoded NPCs"
              << std::endl;
    g_npcManager.Init(data_path);
  }

  // Equip weapon + shield + armor from server equipment data (DB-driven)
  for (auto &eq : serverData.equipment) {
    if (eq.slot == 0) {
      g_hero.EquipWeapon(eq.info);
    } else if (eq.slot == 1) {
      g_hero.EquipShield(eq.info);
    }
    int bodyPart = ItemDatabase::GetBodyPartIndex(eq.info.category);
    if (bodyPart >= 0) {
      std::string partModel = ItemDatabase::GetBodyPartModelFile(
          eq.info.category, eq.info.itemIndex);
      if (!partModel.empty())
        g_hero.EquipBodyPart(bodyPart, partModel);
    }
    std::cout << "[Equip] Slot " << (int)eq.slot << ": " << eq.info.modelFile
              << " cat=" << (int)eq.info.category << std::endl;
  }

  g_syncDone = true;
  // Stop char select music, start Lorencia town music + ambient wind
  SoundManager::StopMusic();
  SoundManager::PlayMusic(g_dataPath + "/Music/MuTheme.mp3");
  SoundManager::PlayLoop(SOUND_WIND01);
  g_npcManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_npcManager.SetVFXManager(&g_vfxManager);
  InventoryUI::RecalcEquipmentStats();
  g_npcManager.SetPointLights(g_pointLights);
  g_boidManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_boidManager.SetPointLights(g_pointLights);

  // Initialize monster manager and spawn monsters from server data
  g_monsterManager.InitModels(data_path);
  g_monsterManager.SetTerrainData(g_terrainDataPtr);
  g_monsterManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_monsterManager.SetPointLights(g_pointLights);
  g_monsterManager.SetVFXManager(&g_vfxManager);
  if (!serverData.monsters.empty()) {
    for (auto &mon : serverData.monsters) {
      g_monsterManager.AddMonster(mon.monsterType, mon.gridX, mon.gridY,
                                  mon.dir, mon.serverIndex, mon.hp, mon.maxHp,
                                  mon.state);
    }
    std::cout << "[Monster] Spawned " << serverData.monsters.size()
              << " monsters from server" << std::endl;
  }

  // Spawn at server-provided position (from CharInfo F3:03), fallback to town center
  if (serverData.hasSpawnPos) {
    float spawnX = (float)serverData.spawnGridY * 100.0f;
    float spawnZ = (float)serverData.spawnGridX * 100.0f;
    g_hero.SetPosition(glm::vec3(spawnX, 0.0f, spawnZ));
    std::cout << "[Spawn] Using server position: grid ("
              << (int)serverData.spawnGridX << ","
              << (int)serverData.spawnGridY << ") -> world ("
              << spawnX << "," << spawnZ << ")" << std::endl;
  } else {
    g_hero.SetPosition(glm::vec3(12750.0f, 0.0f, 13500.0f));
  }
  g_hero.SnapToTerrain();

  // Fix: if hero spawned on a non-walkable tile, find nearest walkable
  {
    glm::vec3 heroPos = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(heroPos.x / 100.0f);
    int gx = (int)(heroPos.z / 100.0f);
    bool walkable = (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
                    (g_terrainDataPtr->mapping.attributes[gz * S + gx] & 0x04) == 0;
    if (!walkable) {
      int startGX = gx > 0 ? gx : 125;
      int startGZ = gz > 0 ? gz : 135;
      bool found = false;
      for (int radius = 0; radius < 30 && !found; radius++) {
        for (int dy = -radius; dy <= radius && !found; dy++) {
          for (int dx = -radius; dx <= radius && !found; dx++) {
            if (radius > 0 && std::abs(dx) != radius && std::abs(dy) != radius)
              continue;
            int cx = startGX + dx, cz = startGZ + dy;
            if (cx < 1 || cz < 1 || cx >= S - 1 || cz >= S - 1)
              continue;
            uint8_t attr = g_terrainDataPtr->mapping.attributes[cz * S + cx];
            if ((attr & 0x04) == 0 && (attr & 0x08) == 0) {
              g_hero.SetPosition(
                  glm::vec3((float)cz * 100.0f, 0.0f, (float)cx * 100.0f));
              g_hero.SnapToTerrain();
              found = true;
            }
          }
        }
      }
      if (!found) {
        g_hero.SetPosition(glm::vec3(13000.0f, 0.0f, 13000.0f));
        g_hero.SnapToTerrain();
      }
    }
  }
  g_camera.SetPosition(g_hero.GetPosition());

  // Pass point lights to renderers
  {
    std::vector<glm::vec3> lightPos, lightCol;
    std::vector<float> lightRange;
    std::vector<int> lightObjTypes;
    for (auto &pl : g_pointLights) {
      lightPos.push_back(pl.position);
      lightCol.push_back(pl.color);
      lightRange.push_back(pl.range);
      lightObjTypes.push_back(pl.objectType);
    }
    g_objectRenderer.SetPointLights(lightPos, lightCol, lightRange);
    g_terrain.SetPointLights(lightPos, lightCol, lightRange, lightObjTypes);
  }

  std::cout << "[World] Game world initialized" << std::endl;
}
