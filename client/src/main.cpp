#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "BoidManager.hpp"
#include "Camera.hpp"
#include "CharacterSelect.hpp"
#include "ChromeGlow.hpp"
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
#include "SystemMessageLog.hpp"
#include "Terrain.hpp"
#include "TerrainParser.hpp"
#include "UICoords.hpp"
#include "UITexture.hpp"
#include "VFXManager.hpp"
#include "ViewerCommon.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_bgfx.h"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <GLFW/glfw3.h>

// Compatibility macros for the GL→BGFX migration.
// Routes ImGui backend calls to the correct implementation.
#define ImGui_BackendNewFrame()         ImGui_ImplBgfx_NewFrame()
#define ImGui_BackendRenderDrawData(dd) ImGui_ImplBgfx_RenderDrawData(dd)
#define ImGui_BackendSetViewId(id)      ImGui_ImplBgfx_SetViewId(id)
#define checkGLError(label)             do {} while(0)
// BGFX view IDs for layered UI rendering:
//   30  = main ImGui pass (HUD, panels, vendor grid)
//   31+ = item 3D models (one view per slot, rendered ON TOP of UI panels)
//   200 = overlay ImGui pass (tooltips, cooldowns, notifications)
//   201 = map transition fade overlay
static constexpr uint16_t IMGUI_VIEW_MAIN    = 30;
static constexpr uint16_t IMGUI_VIEW_OVERLAY = 200;
static constexpr uint16_t IMGUI_VIEW_TRANSITION = 201;
// BGFX view IDs for shadow mapping and post-processing bloom pipeline:
static constexpr bgfx::ViewId SHADOW_VIEW      = 8;
static constexpr bgfx::ViewId PP_VIEW_BRIGHT   = 2;
static constexpr bgfx::ViewId PP_VIEW_BLUR0    = 3;
static constexpr bgfx::ViewId PP_VIEW_BLUR1    = 4;
static constexpr bgfx::ViewId PP_VIEW_BLUR2    = 5;
static constexpr bgfx::ViewId PP_VIEW_BLUR3    = 6;
static constexpr bgfx::ViewId PP_VIEW_COMPOSITE = 9;
static constexpr uint16_t SHADOW_MAP_SIZE = 4096;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <streambuf>
#include <turbojpeg.h>
#include <unistd.h>

// BGFX screenshot callback — captures framebuffer via bgfx::requestScreenShot()
struct BgfxScreenshotCallback : public bgfx::CallbackI {
  bool hasCaptured = false;
  uint32_t capturedWidth = 0, capturedHeight = 0;
  std::string pendingPath; // set before requestScreenShot

  void fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code,
             const char *_str) override {
    fprintf(stderr, "[BGFX FATAL] %s:%d (0x%08x): %s\n", _filePath, _line,
            _code, _str);
  }
  void traceVargs(const char *, uint16_t, const char *, va_list) override {}
  void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
  void profilerBeginLiteral(const char *, uint32_t, const char *,
                            uint16_t) override {}
  void profilerEnd() override {}
  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t, void *, uint32_t) override { return false; }
  void cacheWrite(uint64_t, const void *, uint32_t) override {}
  void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum,
                    bool) override {}
  void captureEnd() override {}
  void captureFrame(const void *, uint32_t) override {}

  void screenShot(const char *_filePath, uint32_t _width, uint32_t _height,
                  uint32_t _pitch, bgfx::TextureFormat::Enum _format,
                  const void *_data, uint32_t _size, bool _yflip) override {
    (void)_filePath;
    (void)_format;
    (void)_size;
    capturedWidth = _width;
    capturedHeight = _height;

    // Convert BGRA to RGBA
    std::vector<uint8_t> rgba(_width * _height * 4);
    const uint8_t *src = (const uint8_t *)_data;
    for (uint32_t y = 0; y < _height; ++y) {
      uint32_t srcY = _yflip ? (_height - 1 - y) : y;
      const uint8_t *srcRow = src + srcY * _pitch;
      uint8_t *dstRow = rgba.data() + y * _width * 4;
      for (uint32_t x = 0; x < _width; ++x) {
        dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // R from B
        dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
        dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // B from R
        dstRow[x * 4 + 3] = 255;
      }
    }

    std::string outPath =
        pendingPath.empty() ? "screenshots/bgfx_screenshot.png" : pendingPath;
    std::filesystem::create_directories(
        std::filesystem::path(outPath).parent_path());
    stbi_write_png(outPath.c_str(), _width, _height, 4, rgba.data(),
                   _width * 4);
    std::cout << "[BGFX Screenshot] Saved: " << outPath << " (" << _width
              << "x" << _height << ")" << std::endl;
    hasCaptured = true;
  }
};

static BgfxScreenshotCallback g_bgfxCallback;

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

// Day/night cycle (Main 5.2: luminosity = sin(WorldTime*0.004)*0.15 + 0.6)
// WorldTime ticks at 25fps (40ms per tick) in original; we use real seconds *
// 25
static float g_worldTime = 0.0f;
static float g_luminosity = 1.0f;

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

// Auto-attack unreachable detection: if target HP unchanged after N re-engages, cancel
static int g_autoAttackLastHp = -1;
static int g_autoAttackSameHpCount = 0;

// Client-side item definitions (owned by ItemDatabase, reference here)
static auto &g_itemDefs = ItemDatabase::GetItemDefs();

// ── Floating damage numbers (type in GroundItemRenderer.hpp) ──
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// Ground item drops (type in ClientTypes.hpp)
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath = "Data";

// Current map ID (0=Lorencia, 1=Dungeon, 2=Devias). File world = mapId + 1.
static int g_currentMapId = 0;
static int g_currentFloor = 0; // 0 = no floor designation, 1+ = floor number

// Determine floor number from grid position using nearest gate anchor points
static int DetermineFloor(int mapId, int gx, int gy) {
  if (mapId == 4) { // Lost Tower — 7 floors
    struct Anchor { int x, y, floor; };
    static const Anchor anchors[] = {
        {208, 75, 1}, {190, 7, 1},     // LT1: Lorencia entry, return from LT2
        {245, 236, 2}, {168, 164, 2},   // LT2
        {86, 167, 3}, {136, 244, 3},    // LT3
        {87, 87, 4}, {136, 134, 4},     // LT4
        {129, 53, 5}, {131, 16, 5},     // LT5
        {53, 53, 6}, {6, 6, 6},         // LT6
        {8, 86, 7},                      // LT7
    };
    int best = 1, bestD = INT_MAX;
    for (auto &a : anchors) {
      int d = (gx - a.x) * (gx - a.x) + (gy - a.y) * (gy - a.y);
      if (d < bestD) { bestD = d; best = a.floor; }
    }
    return best;
  }
  if (mapId == 1) { // Dungeon — 3 floors
    struct Anchor { int x, y, floor; };
    static const Anchor anchors[] = {
        {108, 247, 1}, {240, 150, 1},   // D1: Lorencia entry, return from D2
        {232, 126, 2}, {4, 16, 2},      // D2
        {3, 84, 3}, {29, 125, 3}, {6, 32, 3}, // D3 (+ internal passages)
    };
    int best = 1, bestD = INT_MAX;
    for (auto &a : anchors) {
      int d = (gx - a.x) * (gx - a.x) + (gy - a.y) * (gy - a.y);
      if (d < bestD) { bestD = d; best = a.floor; }
    }
    return best;
  }
  return 0; // no floor designation for other maps
}

// Format floor-specific display name (e.g., "Lost Tower 3", "Dungeon 2")
static const char *GetFloorDisplayName() {
  static char buf[64];
  static const char *mapNames[] = {
      "Lorencia", "Dungeon", "Devias", "Noria", "Lost Tower",
      nullptr, nullptr, "Atlans"}; // indices 5,6 unused
  const char *base = (g_currentMapId >= 0 && g_currentMapId < 8 &&
                      mapNames[g_currentMapId])
                         ? mapNames[g_currentMapId]
                         : "Unknown";
  if (g_currentFloor > 1) {
    snprintf(buf, sizeof(buf), "%s %d", base, g_currentFloor);
    return buf;
  }
  return base; // floor 0 or 1 = just the base name
}
// Per-map clear color (Main 5.2 ZzzScene.cpp:2059)
static ImVec4 g_clearColor =
    ImVec4(10.0f / 256.0f, 20.0f / 256.0f, 14.0f / 256.0f, 1.0f);

// ═══════════════════════════════════════════════════════════════════
// MapConfig — centralized per-map configuration (replaces scattered if/else)
// ═══════════════════════════════════════════════════════════════════
struct MapConfig {
  uint8_t mapId;
  const char *regionName;

  // Atmosphere
  float clearR, clearG, clearB;
  float fogR, fogG, fogB;
  float fogNear, fogFar;
  float luminosity;

  // Post-processing
  float bloomIntensity, bloomThreshold, vignetteStrength;
  float tintR, tintG, tintB;

  // Feature flags
  bool hasSky, hasGrass, hasDoors, hasLeaves, hasWind;

  // Sound
  int ambientLoop; // SOUND_WIND01, SOUND_DUNGEON01, or 0
  const char *safeMusic;
  const char *wildMusic; // nullptr = same as safeMusic everywhere

  // Objects
  bool useNamedObjects; // true=Object1/ named mapping, false=ObjectN/ generic

  // Roof hiding (Main 5.2 ZzzObject.cpp indoor detection)
  int roofTypes[8];
  int roofTypeCount;
  uint8_t indoorTiles[4];
  int indoorTileCount;
  bool indoorAbove; // Also count tiles >= indoorThreshold as indoor
  uint8_t indoorThreshold;

  // Bridge types for TW_NOGROUND reconstruction
  int bridgeTypes[8];
  int bridgeTypeCount;
};

static const MapConfig MAP_CONFIGS[] = {
    {
        // Lorencia (mapId=0)
        0,
        "Lorencia",
        10.f / 256,
        20.f / 256,
        14.f / 256, // clearColor
        0.117f,
        0.078f,
        0.039f, // fogColor
        1500.f,
        3500.f,
        1.0f, // fogNear, fogFar, luminosity
        0.3f,
        0.45f,
        0.1f, // bloom, threshold, vignette
        1.02f,
        1.0f,
        0.96f, // colorTint (warm)
        true,
        true,
        false,
        true,
        true,  // sky, grass, doors, leaves, wind
        SOUND_WIND01,
        "Music/MuTheme.mp3",
        nullptr, // no music outside safe zone (wind only)
        true, // useNamedObjects
        {125, 126},
        2,
        {4},
        1,
        false,
        0, // roofHiding
        {80},
        1, // bridgeTypes
    },
    {
        // Dungeon (mapId=1)
        1,
        "Dungeon",
        0.f,
        0.f,
        0.f, // clearColor (black)
        0.f,
        0.f,
        0.f, // fogColor (black)
        800.f,
        2500.f,
        0.9f, // fogNear, fogFar, luminosity (dungeon is dark)
        0.25f,
        0.55f,
        0.25f, // bloom, threshold, vignette
        0.88f,
        0.93f,
        1.08f, // colorTint (cool blue)
        false,
        false,
        false,
        false,
        false, // sky, grass, doors, leaves, wind
        SOUND_DUNGEON01,
        "Music/Dungeon.mp3",
        nullptr,
        false, // useNamedObjects
        {0},
        0,
        {0},
        0,
        false,
        0, // roofHiding (none)
        {80, 85, 12, 13},
        4, // bridgeTypes (Bridge, BridgeStone, bridge planks/supports)
    },
    {
        // Devias (mapId=2)
        2,
        "Devias",
        0.f,
        0.f,
        10.f / 256, // clearColor (near-black blue)
        0.55f,
        0.65f,
        0.75f, // fogColor (cool blue)
        1500.f,
        4000.f,
        1.0f, // fogNear, fogFar, luminosity
        0.3f,
        0.45f,
        0.08f, // bloom, threshold, vignette
        0.92f,
        0.96f,
        1.08f, // colorTint (cool ice)
        false,
        true,
        true,
        true,
        true,  // sky, grass, doors, leaves, wind
        SOUND_WIND01,
        "Music/Devias.mp3",
        nullptr,
        false, // useNamedObjects
        {81, 82, 96, 98, 99},
        5,
        {3},
        1,
        true,
        10, // roofHiding
        {0},
        0, // bridgeTypes: none — Devias terrain attributes are correct as-is
    },
    {
        // Noria (mapId=3)
        3,
        "Noria",
        15.f / 256,
        30.f / 256,
        10.f / 256, // clearColor (warm green tint)
        0.35f,
        0.45f,
        0.30f, // fogColor (forest green)
        1500.f,
        4000.f,
        1.0f, // fogNear, fogFar, luminosity
        0.25f,
        0.45f,
        0.08f, // bloom, threshold, vignette
        1.0f,
        1.02f,
        0.96f, // colorTint (slight green warmth)
        true,
        true,
        false,
        true,
        true,  // sky, grass, doors, leaves, wind
        SOUND_WIND01,
        "Music/Noria.mp3",
        nullptr, // no combat music
        false, // useNamedObjects (Object4/ numbered)
        {0},
        0,
        {0},
        0,
        false,
        0, // roofHiding (none)
        {0},
        0, // bridgeTypes (none)
    },
    {
        // Lost Tower (mapId=4)
        4,
        "Lost Tower",
        0.05f,
        0.04f,
        0.06f, // clearColor (dark warm gray — visible void with subtle color)
        0.05f,
        0.04f,
        0.06f, // fogColor (match clear — smooth fade to void)
        500.f,
        2500.f,
        0.85f, // fogNear, fogFar, luminosity (fog starts closer for smoother edge fade)
        0.22f,
        0.50f,
        0.25f, // bloom, threshold, vignette
        0.75f,
        0.88f,
        1.12f, // colorTint (subtle cyan-blue: shift hue without crushing brightness)
        false,
        false,
        false,
        false,
        false, // sky, grass, doors, leaves, wind
        SOUND_TOWER01, // Main 5.2: aTower.wav (distinct tower ambience)
        "Music/lost_tower_a.mp3",
        nullptr,
        false, // useNamedObjects (Object5/ numbered)
        {0},
        0,
        {0},
        0,
        false,
        0, // roofHiding (none)
        {0},
        0, // bridgeTypes (none)
    },
    {
        // Atlans (mapId=7) — underwater swimming map
        7,
        "Atlans",
        0.0f,
        0.02f,
        0.06f, // clearColor (deep ocean blue-black)
        0.02f,
        0.05f,
        0.10f, // fogColor (subtle underwater blue)
        2000.f,
        4500.f,
        1.0f, // fogNear, fogFar, luminosity (Main 5.2: NO fog, full brightness)
        0.25f,
        0.50f,
        0.15f, // bloom, threshold, vignette
        0.90f,
        0.95f,
        1.10f, // colorTint (subtle underwater blue, much brighter)
        false,
        false,
        false,
        false,
        false, // sky, grass, doors, leaves, wind
        SOUND_WATER01,
        "Music/atlans.mp3",
        nullptr,
        false, // useNamedObjects (Object8/ numbered)
        {0},
        0,
        {0},
        0,
        false,
        0, // roofHiding (none)
        {0},
        0, // bridgeTypes (none)
    },
};

static const MapConfig *g_mapCfg = &MAP_CONFIGS[0]; // Active map config

static const MapConfig *GetMapConfig(uint8_t mapId) {
  for (const auto &cfg : MAP_CONFIGS)
    if (cfg.mapId == mapId)
      return &cfg;
  return &MAP_CONFIGS[0]; // Fallback to Lorencia
}

// Reconstruct bridge attributes: cells near bridge height get TW_NOGROUND set
// and TW_WATER cleared (road surface), cells far below get TW_WATER set.
// bridgeZone output: all cells near bridge height (for terrain mesh protection).
static void ReconstructBridgeAttributes(TerrainData &td, const MapConfig &cfg,
                                         std::vector<bool> &bridgeZone) {
  if (cfg.bridgeTypeCount == 0) return;
  const int S = 256;
  bridgeZone.assign(S * S, false);
  int count = 0;
  for (const auto &obj : td.objects) {
    bool isBridge = false;
    for (int b = 0; b < cfg.bridgeTypeCount; b++) {
      if (obj.type == cfg.bridgeTypes[b]) { isBridge = true; break; }
    }
    if (!isBridge) continue;

    int gz = (int)(obj.position.x / 100.0f);
    int gx = (int)(obj.position.z / 100.0f);
    float angZ = std::abs(std::fmod(glm::degrees(obj.rotation.z) + 360.0f, 180.0f));
    bool spanAlongGZ = (std::abs(angZ - 90.0f) < 45.0f);
    // Bridge halves are ~5 cells apart perpendicular; generous radius to cover edges
    int rGZ = spanAlongGZ ? 4 : 3;
    int rGX = spanAlongGZ ? 3 : 4;

    for (int dz = -rGZ; dz <= rGZ; ++dz) {
      for (int dx = -rGX; dx <= rGX; ++dx) {
        int cz = gz + dz, cx = gx + dx;
        if (cz < 0 || cz >= S || cx < 0 || cx >= S) continue;
        int idx = cz * S + cx;
        float h = td.heightmap[idx];
        // Road/entrance at bridge height: make walkable, hide terrain under 3D model
        if (std::abs(h - obj.position.y) < 80.0f) {
          td.mapping.attributes[idx] &= ~(0x04 | 0x08); // clear TW_NOMOVE + TW_NOGROUND
          td.mapping.attributes[idx] &= ~0x10;           // clear TW_WATER
          td.mapping.attributes[idx] |= 0x20;            // mark as bridge for shader snap
          bridgeZone[idx] = true;
        }
        // Below-bridge cells: leave completely untouched (keep original water/void)
        count++;
      }
    }
  }
  int zoneCount = 0;
  for (int i = 0; i < S * S; i++) if (bridgeZone[i]) zoneCount++;
  if (count > 0)
    std::cout << "[Bridge] Patched " << count << " cells, bridgeZone=" << zoneCount << std::endl;
}

// Apply atmosphere/rendering settings from MapConfig to all subsystems
static void ApplyMapAtmosphere(const MapConfig &cfg);
using LoadProgressFn = std::function<void(float progress, const char *status)>;
static void LoadWorld(int mapId, LoadProgressFn onProgress = nullptr);
// Forward declarations — defined after g_grass, g_hero etc. are declared

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
static int g_serverResets = 0;
static int g_heroCharacterId = 0;
static char g_characterName[32] = "RealPlayer";

// Inventory & UI state
static bool g_showCharInfo = false;
static bool g_showInventory = false;
static bool g_showSkillWindow = false;

// Learned skills (synced from server via 0x41)
static std::vector<uint8_t> g_learnedSkills;

// Quick slot assignments
static int16_t g_potionBar[4] = {850, 851, 852,
                                 -1}; // Apple, SmallHP, MedHP, (empty)
static int8_t g_skillBar[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static float g_potionCooldown = 0.0f; // Potion cooldown timer (seconds)
static constexpr float POTION_COOLDOWN_TIME = 15.0f;
static bool g_shopOpen = false;
bool g_windStarted = false; // Global: reset by ChangeMap for 3D wind restart
static bool g_showGameMenu = false;
static bool g_isFullscreen = false;
static int g_windowedX = 100, g_windowedY = 100;
static int g_windowedW = 1366, g_windowedH = 768;
static std::vector<ShopItem> g_shopItems;
static bool g_questDialogOpen = false;
static bool g_questDialogWasOpen =
    false; // Track previous frame to detect fresh open
static bool g_questDialogJustOpened = false; // Skip clicks on first frame
static float g_questListScrollY = 0.0f;     // Quest list scroll offset
static int g_questDialogNpcIndex = -1;
static int g_questDialogSelected =
    -1; // -1 = quest list view, 0-4 = viewing specific quest
static bool g_showQuestLog = false; // L key quest log window
static int g_questLogSelected = -1; // Quest log detail view (-1 = none)
static float g_qldPanelRect[4] = {}; // Quest log detail panel bounds
static bool g_showMapWindow = false; // M key map/warp window
static bool g_showMinimap = false;  // TAB key minimap overlay
static bool g_mouseOverUIPanel =
    false; // Set each frame: true if cursor is over any UI panel
static bool g_showCommandTerminal = false;
static char g_commandBuffer[128] = {};
static bool g_commandFocusNeeded =
    false; // Set true when terminal opens to grab focus
// Stored panel bounds for quest dialog and quest log (updated during render)
static float g_qdPanelRect[4] = {}; // x, y, w, h
static float g_qlPanelRect[4] = {}; // x, y, w, h

// Quest system state (synced from server) — per-quest tracking
using ActiveQuestClient = ClientGameState::ActiveQuestClient;
static std::vector<ActiveQuestClient> g_activeQuests;
static uint64_t g_completedQuestMask = 0; // Bitmask: bit N = quest N done

// Quest catalog (received from server — replaces hardcoded g_questCatalog)
using QuestCatalogEntry = ClientGameState::QuestCatalogEntry;
using QuestCatalogReward = ClientGameState::QuestCatalogReward;
static std::vector<QuestCatalogEntry> g_questCatalog;
static ClientGameState *g_clientState = nullptr; // For buff tick/buff display access

// WoW-style quest progress popup (center screen, like WoW objective tracker)
struct QuestProgressPopup {
  char text[128] = {};   // "Target Name: 3/12"
  char quest[64] = {};   // Quest name (shown above)
  float timer = 0.0f;    // Remaining display time
  bool complete = false;  // True if current == required (show green)
};
static QuestProgressPopup g_questPopups[4]; // Pool of 4 popups
static int g_questPopupNext = 0;

static void SpawnQuestPopup(const std::string &questName,
                            const std::string &targetName, int current,
                            int required) {
  auto &p = g_questPopups[g_questPopupNext % 4];
  snprintf(p.quest, sizeof(p.quest), "%s", questName.c_str());
  snprintf(p.text, sizeof(p.text), "%s: %d/%d", targetName.c_str(), current,
           required);
  p.timer = 3.0f;
  p.complete = (current >= required);
  g_questPopupNext++;
}

// WoW-style quest difficulty color based on level gap
static ImU32 GetQuestDifficultyColor(int recommendedLevel) {
  int diff = recommendedLevel - (int)g_hero.GetLevel();
  if (diff >= 5)  return IM_COL32(255, 32, 32, 255);   // red — much higher
  if (diff >= 3)  return IM_COL32(255, 128, 0, 255);    // orange — higher
  if (diff >= -2) return IM_COL32(255, 210, 50, 255);   // yellow — on-level
  if (diff >= -8) return IM_COL32(80, 220, 80, 255);    // green — lower
  return IM_COL32(140, 140, 140, 255);                   // grey — trivial
}

// Ornamental double-line separator
static void DrawOrnamentSep(ImDrawList *dl, float x0, float x1, float y) {
  dl->AddLine(ImVec2(x0, y),     ImVec2(x1, y),     IM_COL32(60, 52, 38, 140));
  dl->AddLine(ImVec2(x0, y + 1), ImVec2(x1, y + 1), IM_COL32(100, 85, 55, 60));
}

// Shadow text helper (black offset behind main color)
static void DrawShadowText(ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text) {
  dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 200), text);
  dl->AddText(pos, color, text);
}

// Thin objective progress bar
static void DrawProgressBar(ImDrawList *dl, float x, float y, float w, float h,
                            float frac) {
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                    IM_COL32(15, 12, 8, 180), 2.0f);
  if (frac > 0.0f) {
    float fillW = w * std::min(frac, 1.0f);
    dl->AddRectFilledMultiColor(
        ImVec2(x, y), ImVec2(x + fillW, y + h),
        IM_COL32(40, 140, 40, 200), IM_COL32(40, 140, 40, 200),
        IM_COL32(60, 180, 60, 200), IM_COL32(60, 180, 60, 200));
  }
  dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h),
              IM_COL32(50, 45, 35, 120), 2.0f);
}

// Item quality color by item level
static ImU32 GetRewardQualityColor(int itemLevel) {
  if (itemLevel >= 9) return IM_COL32(255, 128, 0, 255);   // orange — legendary
  if (itemLevel >= 7) return IM_COL32(180, 100, 255, 255);  // purple — epic
  if (itemLevel >= 4) return IM_COL32(100, 180, 255, 255);  // blue — rare
  if (itemLevel >= 1) return IM_COL32(30, 255, 30, 255);    // green — uncommon
  return IM_COL32(200, 200, 200, 255);                       // white — common
}

// Guard name lookup by NPC type
static const char *GetGuardName(uint16_t type) {
  switch (type) {
  case 245:
    return "Warden Aldric";
  case 246:
    return "Corporal Brynn";
  case 247:
    return "Sergeant Dorian";
  case 248:
    return "Lieutenant Kael";
  case 249:
    return "Captain Marcus";
  case 310:
    return "Ranger Elise";
  case 311:
    return "Tracker Nolan";
  case 312:
    return "Warden Hale";
  case 256:
    return "Sentinel Arwen";
  default:
    return "Guard";
  }
}

// Find active quest state by ID (nullptr if not active)
static ActiveQuestClient *FindActiveQuest(int questId) {
  for (auto &aq : g_activeQuests)
    if (aq.questId == questId) return &aq;
  return nullptr;
}

// Check if quest is completed
static bool IsQuestCompleted(int questId) {
  return (g_completedQuestMask & (1ULL << questId)) != 0;
}

// Check if all kill targets are met for an active quest
static bool IsQuestCompletable(const ActiveQuestClient &aq) {
  if (aq.questId < 0 || aq.questId >= (int)g_questCatalog.size()) return false;
  const auto &qd = g_questCatalog[aq.questId];
  for (int i = 0; i < qd.targetCount; i++)
    if (aq.killCount[i] < qd.targets[i].killsReq) return false;
  return true;
}

// Get quest status for a given quest ID:
// 0=available, 1=in_progress, 2=completable, 3=completed
static int GetQuestStatus(int questId) {
  if (IsQuestCompleted(questId)) return 3;
  auto *aq = FindActiveQuest(questId);
  if (!aq) return 0;
  return IsQuestCompletable(*aq) ? 2 : 1;
}

// Draw a weapon reward item box with 3D model + name label
// Returns the height consumed (0 if no valid reward)
static float DrawQuestRewardItem(ImDrawList *dl, float px, float cy,
                                 float panelW, const QuestCatalogReward &reward,
                                 float qs = 1.0f) {
  if (reward.defIndex < 0)
    return 0;
  auto &itemDefs = ItemDatabase::GetItemDefs();
  auto it = itemDefs.find(reward.defIndex);
  if (it == itemDefs.end())
    return 0;

  const auto &def = it->second;
  float boxSize = 44.0f * qs;
  float boxX = px + 22 * qs;
  float boxY = cy;

  // Hover detection
  ImVec2 mPos = ImGui::GetIO().MousePos;
  bool hovered = mPos.x >= boxX && mPos.x <= boxX + boxSize && mPos.y >= boxY &&
                 mPos.y <= boxY + boxSize;

  // Quality-colored slot border based on item level
  ImU32 qualCol = GetRewardQualityColor(reward.itemLevel);
  ImU32 qualDim = (qualCol & 0x00FFFFFF) | (hovered ? 0xDD000000 : 0xAA000000);
  dl->AddRectFilled(
      ImVec2(boxX, boxY), ImVec2(boxX + boxSize, boxY + boxSize),
      hovered ? IM_COL32(35, 32, 25, 220) : IM_COL32(20, 18, 14, 200), 3.0f);
  dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxSize, boxY + boxSize),
              qualDim, 3.0f, 0, 1.5f);

  // Queue 3D model render job (hovered = spin)
  const char *modelName = def.modelFile.empty()
                              ? ItemDatabase::GetDropModelName(reward.defIndex)
                              : def.modelFile.c_str();
  if (modelName && modelName[0]) {
    int questItemY = (int)boxY;
    InventoryUI::AddRenderJob({modelName, reward.defIndex, (int)boxX,
                               questItemY, (int)boxSize,
                               (int)boxSize, hovered});
  }

  // Tooltip on hover
  if (hovered)
    InventoryUI::AddPendingItemTooltip(reward.defIndex, reward.itemLevel);

  // Item name + level text to the right of the box
  float textX = boxX + boxSize + 10 * qs;
  float textY = boxY + (boxSize - ImGui::GetFontSize()) * 0.5f;
  char nameBuf[64];
  if (reward.itemLevel > 0)
    snprintf(nameBuf, sizeof(nameBuf), "%s +%d", def.name.c_str(),
             reward.itemLevel);
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", def.name.c_str());
  ImU32 nameCol = hovered ? ((qualCol & 0x00FFFFFF) | 0xFF000000) : qualCol;
  DrawShadowText(dl, ImVec2(textX, textY), nameCol, nameBuf);

  return boxSize + 4 * qs; // height consumed
}

// Skill learning state
static bool g_isLearningSkill = false;
static float g_learnSkillTimer = 0.0f;
static uint8_t g_learningSkillId = 0;
static float g_autoSaveTimer = 0.0f;
static constexpr float AUTOSAVE_INTERVAL = 60.0f;   // Save quickslots every 60s
static constexpr float LEARN_SKILL_DURATION = 3.0f; // Seconds of heal anim

// RMC (Right Mouse Click) skill slot
static int8_t g_rmcSkillId = -1;
static bool g_rightMouseHeld = false;

// Teleport / warp cast state
static bool g_teleportingToTown = false;
static float g_teleportTimer = 0.0f;
static constexpr float TELEPORT_CAST_TIME = 2.5f; // Seconds of cast anim
static int g_warpTargetMapId = -1; // -1 = town recall, >=0 = map warp target
static int g_warpTargetGX = 0;
static int g_warpTargetGZ = 0;

// Mount toggle state (M key)
static bool g_mountToggling = false;
static float g_mountToggleTimer = 0.0f;
static constexpr float MOUNT_TOGGLE_TIME = 1.0f; // 1 second preloader

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
static ImFont *g_fontRegion = nullptr;
static ImFont *g_fontLabel = nullptr;     // Work Sans Regular: tooltip body/labels (13px)
static ImFont *g_fontLabelBold = nullptr; // Work Sans SemiBold: tooltip bold values (13px)
static ImFont *g_fontHeadline = nullptr;  // Newsreader Bold Italic: item names
static float g_fontPreScale = 1.0f;

static UICoords g_hudCoords; // File-scope for mouse callback access

// ServerEquipSlot defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// ServerData defined in ClientTypes.hpp

// Delegated to ClientPacketHandler::HandleInitialPacket
// (see src/ClientPacketHandler.cpp)

// Delegated to ClientPacketHandler::HandleGamePacket
// (see src/ClientPacketHandler.cpp)

static const TerrainData *g_terrainDataPtr = nullptr;
static std::unique_ptr<TerrainData>
    g_terrainDataOwned; // Owns terrain data (heap for ChangeMap)

// Roof hiding: per-map object types that fade when hero is indoors.
// Rebuilt on each map change from MapConfig::roofHiding (no cross-map bleed).
static std::unordered_map<int, float> g_typeAlpha;
static std::unordered_map<int, float> g_typeAlphaTarget;

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
static GLFWwindow *g_window = nullptr;      // Set once in main(), used by loading helpers

// ── Fullscreen toggle ──
static int g_fullscreenToggleFrame = -999; // ImGui frame count when fullscreen was toggled

// Check if UI buttons should respond (prevents double-click after fullscreen toggle)
bool UIButtonsReady() {
  int currentFrame = ImGui::GetFrameCount();
  if (currentFrame <= 0) return false; // ImGui not initialized

  int framesSince = currentFrame - g_fullscreenToggleFrame;
  bool ready = framesSince > 15; // Require 15+ frames (250ms at 60fps)

  // Debug: print when buttons are blocked
  static bool lastReady = true;
  if (!ready && lastReady) {
    printf("[UIButtons] Blocked after fullscreen toggle (frame %d, need %d more frames)\n",
           currentFrame, 15 - framesSince + 1);
  }
  lastReady = ready;

  return ready;
}

static void ToggleFullscreen(GLFWwindow *win) {
  if (g_isFullscreen) {
    // Return to windowed mode
    glfwSetWindowMonitor(win, nullptr, g_windowedX, g_windowedY,
                         g_windowedW, g_windowedH, 0);
    g_isFullscreen = false;
  } else {
    // Save current windowed position/size
    glfwGetWindowPos(win, &g_windowedX, &g_windowedY);
    glfwGetWindowSize(win, &g_windowedW, &g_windowedH);
    // Get primary monitor native resolution
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(win, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    g_isFullscreen = true;
  }
  g_fullscreenToggleFrame = ImGui::GetFrameCount(); // Store current frame

  // CRITICAL FIX: Clear ImGui's mouse button state after window mode change
  // When user clicks Fullscreen button, ImGui thinks mouse is still "down"
  // This prevents the next button click (Connect) from registering
  ImGuiIO &io = ImGui::GetIO();
  for (int i = 0; i < 5; i++) {
    io.MouseDown[i] = false;
    io.MouseClicked[i] = false;
    io.MouseReleased[i] = false;
  }
  printf("[ToggleFullscreen] Cleared ImGui mouse state\n");
}

// ── Loading helpers (unified preloader for all loading paths) ──

static void RenderLoadingFrame(float progress, const char *status) {
  glfwPollEvents();
  int fbW, fbH, winW, winH;
  glfwGetFramebufferSize(g_window, &fbW, &fbH);
  glfwGetWindowSize(g_window, &winW, &winH);
  if (fbW <= 0 || fbH <= 0) return;

  // Match main render loop scaling so preloader text/bar sizes are consistent
  ImGui::GetIO().FontGlobalScale = (float)winH / 768.0f / g_fontPreScale;

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000005FF, 1.0f, 0);
  bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
  bgfx::touch(0);

  ImGui_BackendSetViewId(IMGUI_VIEW_TRANSITION);
  ImGui_BackendNewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  // Suppress ImGui's default fallback window
  ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(-200, -200), ImGuiCond_Always);
  ImGui::Begin("Debug##Loading", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
               ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
  ImGui::End();
  ImDrawList *dl = ImGui::GetForegroundDrawList();

  // Black background
  dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                    IM_COL32(0, 0, 5, 255));

  // Minimal progress bar at bottom center
  float barW = winW * 0.4f, barH = 4.0f;
  float barX = (winW - barW) * 0.5f;
  float barY = winH * 0.88f;
  float pClamped = std::clamp(progress, 0.0f, 1.0f);
  dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                    IM_COL32(60, 60, 60, 200));
  dl->AddRectFilled(ImVec2(barX, barY),
                    ImVec2(barX + barW * pClamped, barY + barH),
                    IM_COL32(220, 200, 160, 255));

  // Percentage text below bar
  char pctText[32];
  snprintf(pctText, sizeof(pctText), "%d%%", (int)(pClamped * 100));
  ImVec2 tsz = ImGui::CalcTextSize(pctText);
  dl->AddText(ImVec2(winW * 0.5f - tsz.x * 0.5f, barY + barH + 8),
              IM_COL32(220, 200, 160, 255), pctText);

  ImGui::Render();
  ImGui_BackendRenderDrawData(ImGui::GetDrawData());
  bgfx::frame();
}

// ── Map transition preloader (fullscreen overlay during ChangeMap) ──
static bool g_mapTransitionActive = false;
static int g_mapTransitionPhase = 0; // 0=fade-in, 1=do work, 2=fade-out
static float g_mapTransitionAlpha = 0.0f;
static int g_mapTransitionFrames = 0; // Frames rendered in current phase
static uint8_t g_mapTransMapId = 0;
static uint8_t g_mapTransSpawnX = 0;
static uint8_t g_mapTransSpawnY = 0;
static int g_deferredSoundMapId = -1; // Play ambient/music after preloader closes

// ── Shadow map state (BGFX) ──
struct ShadowMapState {
  bgfx::TextureHandle colorTex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle depthTex = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
  std::unique_ptr<Shader> depthShader;
};
static ShadowMapState g_shadowMap;

// ── Minimap state (outline-style attribute map texture) ──
static bgfx::TextureHandle g_minimapTex = BGFX_INVALID_HANDLE;
static int g_minimapTexSize = 256; // matches terrain grid size

static void GenerateMinimapTexture() {
  if (!g_terrainDataPtr) return;
  const auto &attrs = g_terrainDataPtr->mapping.attributes;
  const auto &layer1 = g_terrainDataPtr->mapping.layer1;
  const auto &layer2 = g_terrainDataPtr->mapping.layer2;
  const auto &heightmap = g_terrainDataPtr->heightmap;
  const auto &alpha = g_terrainDataPtr->mapping.alpha;
  const int S = 256;

  auto bgra = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t {
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | ((uint32_t)a << 24);
  };
  auto isBlocked = [&](int gz, int gx) -> bool {
    if (gz < 0 || gz >= S || gx < 0 || gx >= S) return true;
    uint8_t a = attrs[gz * S + gx];
    return (a & 0x04) || (a & 0x08) || layer1[gz * S + gx] >= 254;
  };
  auto getHeight = [&](int gz, int gx) -> float {
    if (gz < 0 || gz >= S || gx < 0 || gx >= S) return 0.0f;
    return heightmap[gz * (S + 1) + gx];
  };

  // Pass 1: classify cells and compute height/gradient data
  enum CellType : uint8_t { CELL_VOID, CELL_WATER, CELL_SAFE, CELL_WALK };
  std::vector<CellType> cellType(S * S, CELL_VOID);
  std::vector<float> heightGrad(S * S, 0.0f);
  std::vector<float> heights(S * S, 0.0f);

  for (int gz = 0; gz < S; gz++) {
    for (int gx = 0; gx < S; gx++) {
      uint8_t attr = attrs[gz * S + gx];
      uint8_t tile = layer1[gz * S + gx];
      if (tile >= 254 || (attr & 0x08) || (attr & 0x04)) continue;
      if (attr & 0x10)      cellType[gz * S + gx] = CELL_WATER;
      else if (attr & 0x01) cellType[gz * S + gx] = CELL_SAFE;
      else                  cellType[gz * S + gx] = CELL_WALK;

      float h = getHeight(gz, gx);
      heights[gz * S + gx] = h;
      float hx = getHeight(gz, gx + 1) - getHeight(gz, gx - 1);
      float hz = getHeight(gz + 1, gx) - getHeight(gz - 1, gx);
      heightGrad[gz * S + gx] = sqrtf(hx * hx + hz * hz);
    }
  }

  // Pass 2: compute edge distances and tile change maps
  // edgeDist: 0=on edge, 1=1 away from edge, 2+=interior
  std::vector<uint8_t> edgeDist(S * S, 255);
  std::vector<bool> tileChange(S * S, false);

  for (int gz = 0; gz < S; gz++) {
    for (int gx = 0; gx < S; gx++) {
      if (cellType[gz * S + gx] == CELL_VOID) continue;
      // Check 1-ring for wall/void edge
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          if (dz == 0 && dx == 0) continue;
          if (isBlocked(gz + dz, gx + dx)) { edgeDist[gz * S + gx] = 0; goto edgeDone; }
        }
      edgeDone:;
      // Tile change detection: layer1 differs from neighbor → inner structure line
      uint8_t myTile = layer1[gz * S + gx];
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          if (dz == 0 && dx == 0) continue;
          int nz = gz + dz, nx = gx + dx;
          if (nz < 0 || nz >= S || nx < 0 || nx >= S) continue;
          if (cellType[nz * S + nx] == CELL_VOID) continue;
          if (layer1[nz * S + nx] != myTile) { tileChange[gz * S + gx] = true; break; }
        }
    }
  }
  // Second pass for edgeDist=1 (near-edge glow)
  for (int gz = 0; gz < S; gz++)
    for (int gx = 0; gx < S; gx++) {
      if (edgeDist[gz * S + gx] != 255) continue;
      if (cellType[gz * S + gx] == CELL_VOID) continue;
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          int nz = gz + dz, nx = gx + dx;
          if (nz < 0 || nz >= S || nx < 0 || nx >= S) continue;
          if (edgeDist[nz * S + nx] == 0) { edgeDist[gz * S + gx] = 1; goto dist1Done; }
        }
      dist1Done:;
    }

  // Pass 3: render pixels
  std::vector<uint32_t> pixels(S * S, 0);

  for (int gz = 0; gz < S; gz++) {
    for (int gx = 0; gx < S; gx++) {
      CellType ct = cellType[gz * S + gx];
      if (ct == CELL_VOID) continue;

      uint8_t ed = edgeDist[gz * S + gx];
      bool edge = (ed == 0);
      bool nearEdge = (ed == 1);
      bool tileLine = tileChange[gz * S + gx];
      float grad = std::min(heightGrad[gz * S + gx] / 6.0f, 1.0f);

      // Type edge: different cell type neighbor
      bool typeEdge = false;
      for (int dz = -1; dz <= 1 && !typeEdge; dz++)
        for (int dx = -1; dx <= 1 && !typeEdge; dx++) {
          if (dz == 0 && dx == 0) continue;
          int nz = gz + dz, nx = gx + dx;
          if (nz >= 0 && nz < S && nx >= 0 && nx < S) {
            CellType nct = cellType[nz * S + nx];
            if (nct != ct && nct != CELL_VOID) typeEdge = true;
          }
        }

      // Layer2 blending detail
      uint8_t tile2 = layer2.size() > 0 ? layer2[gz * S + gx] : 0;
      float tileBright = (tile2 != layer1[gz * S + gx] && tile2 < 254 &&
                          alpha.size() > 0 && alpha[gz * S + gx] > 0.3f)
                         ? 0.12f : 0.0f;

      // Contour lines: every ~400 height units
      float h = heights[gz * S + gx];
      float hMod = fmodf(h, 400.0f);
      if (hMod < 0) hMod += 400.0f;
      bool contour = (ct != CELL_WATER) && (hMod < 20.0f || hMod > 380.0f);

      if (ct == CELL_WATER) {
        if (edge)          pixels[gz*S+gx] = bgra(15, 35, 75, 170);
        else if (nearEdge) pixels[gz*S+gx] = bgra(10, 25, 55, 130);
        else if (typeEdge) pixels[gz*S+gx] = bgra(12, 28, 60, 120);
        else               pixels[gz*S+gx] = bgra(3, 8, 20, 45);
      } else if (ct == CELL_SAFE) {
        if (edge)          pixels[gz*S+gx] = bgra(30, 60, 25, 170);
        else if (nearEdge) pixels[gz*S+gx] = bgra(20, 42, 16, 120);
        else if (typeEdge || contour) pixels[gz*S+gx] = bgra(18, 36, 14, 100);
        else if (tileLine) pixels[gz*S+gx] = bgra(12, 26, 10, 65);
        else {
          uint8_t b = (uint8_t)(5 + grad * 8 + tileBright * 12);
          pixels[gz*S+gx] = bgra(b, (uint8_t)(b+3), (uint8_t)std::max(0,b-2), 32);
        }
      } else {
        // Normal walkable
        if (edge) {
          uint8_t ev = (uint8_t)(45 + grad * 25);
          pixels[gz*S+gx] = bgra(ev, (uint8_t)(ev-5), (uint8_t)(ev-12), 170);
        } else if (nearEdge) {
          uint8_t nv = (uint8_t)(28 + grad * 14);
          pixels[gz*S+gx] = bgra(nv, (uint8_t)(nv-3), (uint8_t)(nv-8), 110);
        } else if (typeEdge) {
          pixels[gz*S+gx] = bgra(30, 27, 22, 110);
        } else if (contour) {
          pixels[gz*S+gx] = bgra(20, 18, 14, 80);
        } else if (tileLine) {
          pixels[gz*S+gx] = bgra(12, 11, 8, 50);
        } else {
          uint8_t b = (uint8_t)(3 + grad * 6 + tileBright * 8);
          pixels[gz*S+gx] = bgra(b, b, (uint8_t)std::max(0,b-1), 22);
        }
      }
    }
  }

  if (bgfx::isValid(g_minimapTex))
    bgfx::destroy(g_minimapTex);
  const bgfx::Memory *mem = bgfx::alloc(S * S * 4);
  memcpy(mem->data, pixels.data(), S * S * 4);
  g_minimapTex = bgfx::createTexture2D(
      S, S, false, 1, bgfx::TextureFormat::BGRA8,
      BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
}

// ── Post-processing (bloom + vignette + color grading) ──
struct PostProcessState {
  bool enabled = false;
  int width = 0, height = 0;
  bgfx::FrameBufferHandle sceneFB = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle sceneColorTex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle sceneDepthTex = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle bloomFB[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::TextureHandle bloomTex[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  int bloomW = 0, bloomH = 0;
  std::unique_ptr<Shader> brightExtract;
  std::unique_ptr<Shader> blur;
  std::unique_ptr<Shader> composite;
  float bloomIntensity = 0.3f;
  float vignetteStrength = 0.1f;
  glm::vec3 colorTint = glm::vec3(1.02f, 1.0f, 0.96f);
  float bloomThreshold = 0.35f;
  float gradingStrength = 0.3f; // 0=no color grading, 1=full grading
  float sharpStrength = 0.25f;  // 0=no sharpening, ~0.2-0.4=subtle, 1.0=strong
  bgfx::VertexBufferHandle screenTriVBO = BGFX_INVALID_HANDLE;
};
static PostProcessState g_postProcess;

static void InitPostProcess() {
  auto &pp = g_postProcess;
  pp.brightExtract = Shader::Load("vs_postprocess.bin", "fs_bright_extract.bin");
  pp.blur = Shader::Load("vs_postprocess.bin", "fs_blur.bin");
  pp.composite = Shader::Load("vs_postprocess.bin", "fs_postprocess.bin");
  if (!pp.brightExtract || !pp.blur || !pp.composite) {
    std::cerr << "[PostProcess] Failed to load shaders, disabling\n";
    pp.enabled = false;
    return;
  }
  // Fullscreen triangle (covers entire NDC quad with one triangle)
  static float screenTriVerts[] = {
    -1.0f, -1.0f, 0.0f,
     3.0f, -1.0f, 0.0f,
    -1.0f,  3.0f, 0.0f,
  };
  bgfx::VertexLayout ppLayout;
  ppLayout.begin()
    .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
    .end();
  pp.screenTriVBO = bgfx::createVertexBuffer(
      bgfx::makeRef(screenTriVerts, sizeof(screenTriVerts)), ppLayout);

  pp.enabled = true;
  std::cout << "[PostProcess] Initialized successfully\n";
}

static void ResizePostProcessFBOs(int fbW, int fbH) {
  auto &pp = g_postProcess;
  if (!pp.enabled || (pp.width == fbW && pp.height == fbH))
    return;
  pp.width = fbW;
  pp.height = fbH;
  pp.bloomW = fbW / 2;
  pp.bloomH = fbH / 2;

  // Cleanup old
  if (bgfx::isValid(pp.sceneFB)) bgfx::destroy(pp.sceneFB);
  if (bgfx::isValid(pp.sceneColorTex)) bgfx::destroy(pp.sceneColorTex);
  if (bgfx::isValid(pp.sceneDepthTex)) bgfx::destroy(pp.sceneDepthTex);
  for (int i = 0; i < 2; ++i) {
    if (bgfx::isValid(pp.bloomFB[i])) bgfx::destroy(pp.bloomFB[i]);
    if (bgfx::isValid(pp.bloomTex[i])) bgfx::destroy(pp.bloomTex[i]);
  }

  // Scene FBO (full resolution)
  pp.sceneColorTex = bgfx::createTexture2D(fbW, fbH, false, 1,
      bgfx::TextureFormat::BGRA8,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  pp.sceneDepthTex = bgfx::createTexture2D(fbW, fbH, false, 1,
      bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
  bgfx::TextureHandle sceneAtts[] = {pp.sceneColorTex, pp.sceneDepthTex};
  pp.sceneFB = bgfx::createFrameBuffer(2, sceneAtts, false);

  // Bloom ping-pong FBOs (half resolution)
  for (int i = 0; i < 2; ++i) {
    pp.bloomTex[i] = bgfx::createTexture2D(pp.bloomW, pp.bloomH, false, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle bt[] = {pp.bloomTex[i]};
    pp.bloomFB[i] = bgfx::createFrameBuffer(1, bt, false);
  }

  std::cout << "[PostProcess] FBOs resized to " << fbW << "x" << fbH
            << " (bloom " << pp.bloomW << "x" << pp.bloomH << ")\n";
}

// Run the bloom + composite post-processing pipeline
static void RenderPostProcess(int fbW, int fbH) {
  auto &pp = g_postProcess;
  if (!pp.enabled || !bgfx::isValid(pp.screenTriVBO))
    return;

  // Pass 1: Bright extract — scene → bloom[0]
  bgfx::setViewName(PP_VIEW_BRIGHT, "BrightExtract");
  bgfx::setViewRect(PP_VIEW_BRIGHT, 0, 0, uint16_t(pp.bloomW), uint16_t(pp.bloomH));
  bgfx::setViewFrameBuffer(PP_VIEW_BRIGHT, pp.bloomFB[0]);
  pp.brightExtract->setTexture(0, "s_scene", pp.sceneColorTex);
  pp.brightExtract->setVec4("u_ppParams", glm::vec4(pp.bloomThreshold, 0, 0, 0));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_BRIGHT, pp.brightExtract->program);

  // Pass 2: Horizontal blur — bloom[0] → bloom[1]
  bgfx::setViewName(PP_VIEW_BLUR0, "BlurH1");
  bgfx::setViewRect(PP_VIEW_BLUR0, 0, 0, uint16_t(pp.bloomW), uint16_t(pp.bloomH));
  bgfx::setViewFrameBuffer(PP_VIEW_BLUR0, pp.bloomFB[1]);
  pp.blur->setTexture(0, "s_image", pp.bloomTex[0]);
  pp.blur->setVec4("u_blurParams", glm::vec4(1.0f, 1.0f / pp.bloomW, 0, 0));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_BLUR0, pp.blur->program);

  // Pass 3: Vertical blur — bloom[1] → bloom[0]
  bgfx::setViewName(PP_VIEW_BLUR1, "BlurV1");
  bgfx::setViewRect(PP_VIEW_BLUR1, 0, 0, uint16_t(pp.bloomW), uint16_t(pp.bloomH));
  bgfx::setViewFrameBuffer(PP_VIEW_BLUR1, pp.bloomFB[0]);
  pp.blur->setTexture(0, "s_image", pp.bloomTex[1]);
  pp.blur->setVec4("u_blurParams", glm::vec4(0.0f, 1.0f / pp.bloomH, 0, 0));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_BLUR1, pp.blur->program);

  // Pass 4: Horizontal blur 2 — bloom[0] → bloom[1]
  bgfx::setViewName(PP_VIEW_BLUR2, "BlurH2");
  bgfx::setViewRect(PP_VIEW_BLUR2, 0, 0, uint16_t(pp.bloomW), uint16_t(pp.bloomH));
  bgfx::setViewFrameBuffer(PP_VIEW_BLUR2, pp.bloomFB[1]);
  pp.blur->setTexture(0, "s_image", pp.bloomTex[0]);
  pp.blur->setVec4("u_blurParams", glm::vec4(1.0f, 1.0f / pp.bloomW, 0, 0));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_BLUR2, pp.blur->program);

  // Pass 5: Vertical blur 2 — bloom[1] → bloom[0]
  bgfx::setViewName(PP_VIEW_BLUR3, "BlurV2");
  bgfx::setViewRect(PP_VIEW_BLUR3, 0, 0, uint16_t(pp.bloomW), uint16_t(pp.bloomH));
  bgfx::setViewFrameBuffer(PP_VIEW_BLUR3, pp.bloomFB[0]);
  pp.blur->setTexture(0, "s_image", pp.bloomTex[1]);
  pp.blur->setVec4("u_blurParams", glm::vec4(0.0f, 1.0f / pp.bloomH, 0, 0));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_BLUR3, pp.blur->program);

  // Pass 6: Composite — scene + bloom → backbuffer
  bgfx::setViewName(PP_VIEW_COMPOSITE, "Composite");
  bgfx::setViewRect(PP_VIEW_COMPOSITE, 0, 0, uint16_t(fbW), uint16_t(fbH));
  bgfx::setViewClear(PP_VIEW_COMPOSITE, BGFX_CLEAR_COLOR, 0x000000FF, 1.0f, 0);
  bgfx::setViewFrameBuffer(PP_VIEW_COMPOSITE, BGFX_INVALID_HANDLE);
  pp.composite->setTexture(0, "s_scene", pp.sceneColorTex);
  pp.composite->setTexture(1, "s_bloom", pp.bloomTex[0]);
  pp.composite->setVec4("u_ppComposite", glm::vec4(pp.bloomIntensity, pp.vignetteStrength, pp.gradingStrength, pp.sharpStrength));
  pp.composite->setVec4("u_ppTint", glm::vec4(pp.colorTint, 0.0f));
  bgfx::setVertexBuffer(0, pp.screenTriVBO);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(PP_VIEW_COMPOSITE, pp.composite->program);
}

struct LightTemplate {
  glm::vec3 color;
  float range;
  float heightOffset; // Y offset above object base for emission point
};

// Build pond cell grid: marks cells near Waterspout/Well world objects (types 105-109)
// so GrassRenderer can force grass (reeds) at those locations.
static std::vector<bool>
BuildPondCells(const std::vector<ObjectRenderer::ObjectInstance> &insts) {
  std::vector<bool> pond(256 * 256, false);
  for (auto &inst : insts) {
    if (inst.type < 105 || inst.type > 109) continue;
    glm::vec3 p = glm::vec3(inst.modelMatrix[3]);
    int gz = (int)(p.x / 100.0f); // world.x -> grid z-row
    int gx = (int)(p.z / 100.0f); // world.z -> grid x-col
    int r = 2; // 2-cell radius around pond objects
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        int nz = gz + dz, nx = gx + dx;
        if (nz >= 0 && nz < 256 && nx >= 0 && nx < 256)
          pond[nz * 256 + nx] = true;
      }
  }
  return pond;
}

// Returns light properties for a given object type, or nullptr if not a light
static const LightTemplate *GetLightProperties(int type, int mapId = -1) {
  static const LightTemplate fireLightProps = {glm::vec3(0.8f, 0.48f, 0.27f),
                                               800.0f, 150.0f};
  // Lost Tower fire column: brighter, wider range light
  static const LightTemplate fireColumnProps = {glm::vec3(1.0f, 0.5f, 0.2f),
                                                1200.0f, 150.0f};
  static const LightTemplate bonfireProps = {glm::vec3(0.8f, 0.4f, 0.16f),
                                             1000.0f, 100.0f};
  static const LightTemplate gateProps = {glm::vec3(0.8f, 0.48f, 0.27f), 800.0f,
                                          200.0f};
  static const LightTemplate bridgeProps = {glm::vec3(0.65f, 0.38f, 0.22f), 700.0f,
                                            50.0f};
  static const LightTemplate streetLightProps = {glm::vec3(0.55f, 0.45f, 0.28f),
                                                 450.0f, 250.0f};
  static const LightTemplate candleProps = {glm::vec3(0.65f, 0.38f, 0.16f), 600.0f,
                                            80.0f};
  static const LightTemplate lightFixtureProps = {glm::vec3(0.65f, 0.46f, 0.27f),
                                                  700.0f, 150.0f};

  // Dungeon torches (Main 5.2: tall fire stand + standard lantern)
  static const LightTemplate dungeonTorchProps = {glm::vec3(0.75f, 0.43f, 0.21f),
                                                  700.0f, 200.0f};
  // (Lance Trap light removed — Main 5.2 has no terrain light for dungeon traps)
  // Noria mystical lights — cyan/blue tint (Main 5.2: Vector(L*0.4,L*0.7,L*1.0))
  static const LightTemplate noriaLightProps = {glm::vec3(0.4f, 0.65f, 0.95f),
                                                 600.0f, 150.0f};
  static const LightTemplate noriaLightLargeProps = {glm::vec3(0.4f, 0.65f, 0.95f),
                                                      900.0f, 200.0f};

  switch (type) {
  case 41: // Dungeon torches only
  case 42:
    return (mapId == 1) ? &dungeonTorchProps : nullptr;
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
  case 98: // Carriage01 — town lantern (near fountain/fences)
    return &streetLightProps;
  case 130:
  case 131:
  case 132:
    return &lightFixtureProps;
  // case 39: Lance Trap — removed (Main 5.2 has no terrain light for dungeon traps)
  case 150:
    return &candleProps;
  case 78: // StoneMuWall02 — torch/window glow (BlendMesh=3)
    return &streetLightProps;
  case 30: // Devias fireplace (Stone01) — warm fire glow
    return (mapId == 2) ? &fireLightProps : nullptr;
  case 66: // Devias wall fire (SteelWall02) — warm fire glow
    return (mapId == 2) ? &candleProps : nullptr;
  // Noria (mapId=3) mystical light objects (Main 5.2: BITMAP_LIGHT blue sprites)
  case 1:
    return (mapId == 3) ? &noriaLightProps : nullptr;
  case 9:
    return (mapId == 3) ? &noriaLightLargeProps : nullptr;
  case 17:
    return (mapId == 3) ? &noriaLightProps : nullptr;
  case 35:
    return (mapId == 3) ? &noriaLightLargeProps : nullptr;
  // Lost Tower (mapId=4) torch/fire objects
  // Main 5.2: types 3,4 = fire torches/braziers, 19 = magic orb, 20 = lightning pillar
  case 3:
  case 4:
    return (mapId == 4) ? &dungeonTorchProps : nullptr;
  case 24: // Lost Tower fire column — bright orange, wide range
    return (mapId == 4) ? &fireColumnProps : nullptr;
  case 19:
    return (mapId == 4) ? &dungeonTorchProps : nullptr;
  case 20:
    return (mapId == 4) ? &noriaLightProps : nullptr; // blue-tinted lightning pillar
  case 23: // Lost Tower window glow
    return (mapId == 4) ? &candleProps : nullptr;
  case 40: // Lost Tower electrical tower
    return (mapId == 4) ? &noriaLightLargeProps : nullptr;
  default:
    return nullptr;
  }
}

// ── Game world initialization (called after character select) ──
// Forward declared, defined after main() helpers
static void InitGameWorld(ServerData &serverData, LoadProgressFn onProgress = nullptr);
static void ChangeMap(uint8_t mapId, uint8_t spawnX, uint8_t spawnY,
                      LoadProgressFn onProgress = nullptr);

// Input handling (mouse, keyboard, click-to-move, processInput) delegated
// to InputHandler module (see src/InputHandler.cpp)

// Panel rendering, click handling, drag/drop, tooltip, and item layout
// all delegated to InventoryUI module (see src/InventoryUI.cpp)

int main(int argc, char **argv) {
  // Require launch via launch.sh (sets MU_LAUNCHED env var)
  if (!getenv("MU_LAUNCHED")) {
    fprintf(stderr, "ERROR: Please use launch.sh to start the game.\n"
                    "  Usage: ./launch.sh\n");
    return 1;
  }

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

  // BGFX: no OpenGL context — BGFX manages the GPU
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  GLFWwindow *window = glfwCreateWindow(
      1366, 768, "Mu Online Remaster (Native macOS C++)", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  g_window = window;

  // Initialize BGFX with Metal backend on macOS
  bgfx::renderFrame(); // Single-threaded mode: call before bgfx::init
  bgfx::Init bgfxInit;
  bgfxInit.type = bgfx::RendererType::Metal;
  bgfxInit.platformData.nwh = glfwGetCocoaWindow(window);
  bgfxInit.callback = &g_bgfxCallback;
  int initW, initH;
  glfwGetFramebufferSize(window, &initW, &initH);
  bgfxInit.resolution.width = initW;
  bgfxInit.resolution.height = initH;
  bgfxInit.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X16;
  if (!bgfx::init(bgfxInit)) {
    std::cerr << "Failed to initialize BGFX" << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }
  bgfx::setDebug(BGFX_DEBUG_NONE);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                      0x000000FF, 1.0f, 0);
  bgfx::setViewRect(0, 0, 0, uint16_t(initW), uint16_t(initH));
  // Preserve submission order: opaque draws must happen before additive
  // BlendMesh glow passes. Default mode reorders for state efficiency,
  // which can cause additive glow to be overwritten by later opaque draws.
  bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
  // Prime the Metal backbuffer — render a few frames to the default surface
  // before any FBO redirections, so Metal's drawable pipeline is initialized.
  for (int i = 0; i < 3; ++i) {
    bgfx::touch(0);
    bgfx::frame();
  }
  std::cout << "[BGFX] Initialized with Metal backend (" << initW << "x" << initH << ")" << std::endl;
  g_terrain.Init(); // Load BGFX terrain shader

  ItemDatabase::Init();


  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.IniFilename = nullptr; // Disable imgui.ini persistence

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // Make default window background + border fully transparent so any
  // accidental/default windows are invisible.  Real UI windows push their
  // own style colours before Begin().
  {
    ImGuiStyle &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg]   = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Border]     = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.WindowBorderSize = 0.0f;
  }

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOther(window, true);
  ImGui_ImplBgfx_Init(30, "shaders"); // View 30 = ImGui overlay

  // Load fonts for high-fidelity UI
  // Body text: Verdana (clean, readable at all sizes) — macOS system font
  // Titles/headers: Cinzel (WoW-style decorative serif)
  // Pre-scale: content scale (Retina=2, standard=1) * resolution factor
  {
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    g_fontPreScale = xscale;
  }
  {
    GLFWmonitor *primaryMon = glfwGetPrimaryMonitor();
    if (primaryMon) {
      const GLFWvidmode *mode = glfwGetVideoMode(primaryMon);
      if (mode && mode->height > 768)
        g_fontPreScale *= (float)mode->height / 768.0f;
    }
  }
  {
    ImFontConfig cfg;
    const char *fontBody = "/System/Library/Fonts/Supplemental/Verdana.ttf";
    const char *fontBodyBold = "/System/Library/Fonts/Supplemental/Verdana Bold.ttf";
    const char *fontTitle = "../fonts/Cinzel.ttf";
    const char *fontAlmendra = "../fonts/Almendra-Regular.ttf";
    const char *fallbackPath = "external/imgui/misc/fonts/ProggyClean.ttf";
    auto tryFont = [](const char *path) -> bool {
      FILE *f = fopen(path, "rb");
      if (f) { fclose(f); return true; }
      return false;
    };
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    float fs = g_fontPreScale; // contentScale + resolution pre-scale combined
    // Body font: Verdana (readable) → Almendra → ProggyClean
    if (tryFont(fontBody)) {
      g_fontDefault =
          io.Fonts->AddFontFromFileTTF(fontBody, 14.0f * fs, &cfg);
    } else if (tryFont(fontAlmendra)) {
      g_fontDefault =
          io.Fonts->AddFontFromFileTTF(fontAlmendra, 15.0f * fs, &cfg);
    } else if (tryFont(fallbackPath)) {
      g_fontDefault =
          io.Fonts->AddFontFromFileTTF(fallbackPath, 15.0f * fs);
    }
    // Title font: Cinzel (WoW-style) → Verdana Bold → body font
    if (tryFont(fontTitle)) {
      g_fontBold =
          io.Fonts->AddFontFromFileTTF(fontTitle, 16.0f * fs, &cfg);
      g_fontRegion =
          io.Fonts->AddFontFromFileTTF(fontTitle, 28.0f * fs, &cfg);
    } else if (tryFont(fontBodyBold)) {
      g_fontBold =
          io.Fonts->AddFontFromFileTTF(fontBodyBold, 16.0f * fs, &cfg);
      g_fontRegion =
          io.Fonts->AddFontFromFileTTF(fontBodyBold, 28.0f * fs, &cfg);
    }
    // Tooltip fonts (HTML design: Newsreader headline, Work Sans labels)
    const char *fontNewsreader = "../fonts/Newsreader-BoldItalic.ttf";
    const char *fontWorkSans = "../fonts/WorkSans-Regular.ttf";
    const char *fontWorkSansSB = "../fonts/WorkSans-SemiBold.ttf";
    const char *fontInter = "../fonts/Inter-Regular.ttf";

    // Headline: Newsreader Bold Italic (item names in tooltips)
    if (tryFont(fontNewsreader))
      g_fontHeadline = io.Fonts->AddFontFromFileTTF(fontNewsreader, 18.0f * fs, &cfg);

    // Label: Work Sans Regular (tooltip body text, 13px)
    if (tryFont(fontWorkSans))
      g_fontLabel = io.Fonts->AddFontFromFileTTF(fontWorkSans, 13.0f * fs, &cfg);

    // Label Bold: Work Sans SemiBold (tooltip bold values — DPS, footer, class)
    if (tryFont(fontWorkSansSB))
      g_fontLabelBold = io.Fonts->AddFontFromFileTTF(fontWorkSansSB, 13.0f * fs, &cfg);

    // Body: prefer Inter over Verdana if available
    if (tryFont(fontInter) && !g_fontDefault)
      g_fontDefault = io.Fonts->AddFontFromFileTTF(fontInter, 14.0f * fs, &cfg);

    if (!g_fontDefault)
      g_fontDefault = io.Fonts->AddFontDefault(&cfg);
    if (!g_fontBold)
      g_fontBold = g_fontDefault;
    if (!g_fontRegion)
      g_fontRegion = g_fontBold;
    if (!g_fontHeadline)
      g_fontHeadline = g_fontBold;
    if (!g_fontLabel)
      g_fontLabel = g_fontDefault;
    if (!g_fontLabelBold)
      g_fontLabelBold = g_fontLabel;

    io.Fonts->Build();
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

  MockData hudData = MockData::CreateDK50();

  std::string data_path = g_dataPath;

  // ── One-time subsystem initialization (before first world load) ──
  g_sky.Init(data_path + "/");
  InitPostProcess();
  {
    g_shadowMap.colorTex = bgfx::createTexture2D(
        SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    g_shadowMap.depthTex = bgfx::createTexture2D(
        SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
        bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle atts[] = { g_shadowMap.colorTex, g_shadowMap.depthTex };
    g_shadowMap.fb = bgfx::createFrameBuffer(2, atts, false);
    g_shadowMap.depthShader = Shader::Load("vs_depth.bin", "fs_depth.bin");
    if (g_shadowMap.depthShader && bgfx::isValid(g_shadowMap.fb)) {
      std::cout << "[ShadowMap] Initialized " << SHADOW_MAP_SIZE << "x"
                << SHADOW_MAP_SIZE << " depth FBO\n";
    } else {
      std::cerr << "[ShadowMap] Failed to initialize shadow mapping\n";
    }
  }
  // Minimap texture will be generated after each terrain load
  g_fireEffect.Init(data_path + "/Effect");
  g_vfxManager.Init(data_path);
  g_vfxManager.SetTerrainHeightFunc(
      [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });
  g_vfxManager.SetPlaySoundFunc(
      [](int soundId) { SoundManager::Play(soundId); });
  g_boidManager.Init(data_path);
  g_hero.Init(data_path);
  g_hero.SetVFXManager(&g_vfxManager);
  g_hero.LoadStats(1, 28, 20, 25, 10, 0, 0, 110, 110, 20, 20, 50, 50, 16);
  ChromeGlow::LoadTextures(g_dataPath);
  ItemModelManager::Init(g_hero.GetShader(), g_dataPath);

  // ── Load initial world (terrain, objects, fire, lights, grass) ──
  LoadWorld(g_currentMapId, [](float p, const char *s) {
    RenderLoadingFrame(p, s);
  });
  for (int i = 0; i < 15; i++)
    RenderLoadingFrame(1.0f, "Ready");

  // ── Point subsystems to loaded terrain ──
  g_hero.SetTerrainData(g_terrainDataPtr);
  g_hero.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_boidManager.SetTerrainData(g_terrainDataPtr);
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
    ctx.showQuestLog = &g_showQuestLog;
    ctx.showMapWindow = &g_showMapWindow;
    ctx.currentMapId = &g_currentMapId;
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
    ctx.serverResets = &g_serverResets;
    ctx.questCatalog = &g_questCatalog;
    ctx.activeQuests = &g_activeQuests;
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
    ctx.warpTargetMapId = &g_warpTargetMapId;
    ctx.warpTargetGX = &g_warpTargetGX;
    ctx.warpTargetGZ = &g_warpTargetGZ;
    ctx.learnSkillDuration = LEARN_SKILL_DURATION;
    ctx.mountToggling = &g_mountToggling;
    ctx.mountToggleTimer = &g_mountToggleTimer;
    ctx.mountToggleTime = MOUNT_TOGGLE_TIME;
    ctx.hero = &g_hero;
    ctx.server = &g_server;
    ctx.hudCoords = &g_hudCoords;
    ctx.fontDefault = g_fontDefault;
    ctx.fontBold = g_fontBold;
    ctx.fontRegion = g_fontRegion;
    ctx.fontHeadline = g_fontHeadline;
    ctx.fontLabel = g_fontLabel;
    ctx.fontLabelBold = g_fontLabelBold;
    InventoryUI::Init(ctx);
  }

  g_clickEffect.LoadAssets(data_path);
  g_clickEffect.SetTerrainData(g_terrainDataPtr);
  g_clickEffect.SetVFXManager(&g_vfxManager);
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
    inputCtx.objectRenderer = &g_objectRenderer;
    inputCtx.groundItems = g_groundItems;
    inputCtx.equipSlots = g_equipSlots;
    inputCtx.maxGroundItems = MAX_GROUND_ITEMS;
    inputCtx.hudCoords = &g_hudCoords;
    inputCtx.showCharInfo = &g_showCharInfo;
    inputCtx.showInventory = &g_showInventory;
    inputCtx.showSkillWindow = &g_showSkillWindow;
    inputCtx.showMapWindow = &g_showMapWindow;
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
    inputCtx.teleportingToTown = &g_teleportingToTown;
    inputCtx.teleportTimer = &g_teleportTimer;
    inputCtx.teleportCastTime = TELEPORT_CAST_TIME;
    inputCtx.mountToggling = &g_mountToggling;
    inputCtx.mountToggleTimer = &g_mountToggleTimer;
    inputCtx.mountToggleTime = MOUNT_TOGGLE_TIME;
    inputCtx.questDialogOpen = &g_questDialogOpen;
    inputCtx.questDialogNpcIndex = &g_questDialogNpcIndex;
    inputCtx.questDialogSelected = &g_questDialogSelected;
    inputCtx.showQuestLog = &g_showQuestLog;
    inputCtx.questLogSelected = &g_questLogSelected;
    inputCtx.mouseOverUIPanel = &g_mouseOverUIPanel;
    inputCtx.showCommandTerminal = &g_showCommandTerminal;
    inputCtx.showMinimap = &g_showMinimap;
    inputCtx.dataPath = data_path;
    InputHandler::Init(inputCtx);
    InputHandler::RegisterCallbacks(window);
  }

  // Connect to server via persistent ServerConnection
  g_npcManager.SetTerrainData(g_terrainDataPtr);
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
    gameState.activeQuests = &g_activeQuests;
    gameState.completedQuestMask = &g_completedQuestMask;
    gameState.questCatalog = &g_questCatalog;
    gameState.spawnDamageNumber = [](const glm::vec3 &pos, int dmg,
                                     uint8_t type) {
      FloatingDamageRenderer::Spawn(pos, dmg, type, g_floatingDmg,
                                    MAX_FLOATING_DAMAGE);
    };
    gameState.getBodyPartIndex = ItemDatabase::GetBodyPartIndex;
    gameState.getBodyPartModelFile = ItemDatabase::GetBodyPartModelFile;
    gameState.getItemRestingAngle = [](int16_t defIdx, glm::vec3 &angle,
                                       float &scale, float &heightBoost) {
      GroundItemRenderer::GetItemRestingAngle(defIdx, angle, scale, heightBoost);
    };
    gameState.onQuestProgress = [](const std::string &qn,
                                    const std::string &tn, int cur, int req) {
      SpawnQuestPopup(qn, tn, cur, req);
    };
    ClientPacketHandler::Init(&gameState);
    g_clientState = &gameState;
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
      SoundManager::StopMusic();
      g_loadingFrames = 0;
      g_gameState = GameState::LOADING;
      std::cout << "[State] -> LOADING (waiting for world data)" << std::endl;
    };
    csCtx.onExit = [&]() { glfwSetWindowShouldClose(window, GLFW_TRUE); };
    csCtx.onToggleFullscreen = [&]() { ToggleFullscreen(window); };
    csCtx.fontDefault = g_fontDefault;
    csCtx.fontBold = g_fontBold;
    csCtx.fontRegion = g_fontRegion;
    if (g_clientState) csCtx.classDefinitions = &g_clientState->classDefinitions;
    ChromeGlow::LoadTextures(g_dataPath);
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
  SystemMessageLog::Init();

  g_gameState = GameState::CHAR_SELECT;
  SoundManager::PlayMusic(g_dataPath + "/Music/crywolf_before-01.ogg", true);
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


  ImVec4 &clear_color = g_clearColor;
  float lastFrame = 0.0f;
  float smoothedDelta = 1.0f / 60.0f; // Initialize to 60fps assumption
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = glfwGetTime();
    float rawDelta = currentFrame - lastFrame;
    lastFrame = currentFrame;
    // Smooth delta time to reduce animation/movement jitter from frame time spikes.
    // Clamp raw to prevent extreme values (e.g. after breakpoint, window drag).
    if (rawDelta > 0.0f && rawDelta < 0.25f)
      smoothedDelta += (rawDelta - smoothedDelta) * 0.2f;
    float deltaTime = smoothedDelta;

    glfwPollEvents();

    // Handle window resize: bgfx::reset() updates backbuffer + preserves MSAA
    {
      static int s_lastFbW = 0, s_lastFbH = 0;
      int curFbW, curFbH;
      glfwGetFramebufferSize(window, &curFbW, &curFbH);
      if (curFbW != s_lastFbW || curFbH != s_lastFbH) {
        printf("[Resize] Framebuffer changed: %dx%d -> %dx%d\n",
               s_lastFbW, s_lastFbH, curFbW, curFbH);
        s_lastFbW = curFbW;
        s_lastFbH = curFbH;
        bgfx::reset(curFbW, curFbH, BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X16);
      }
    }

    // Poll persistent network connection for server packets
    g_server.Poll();
    g_server.Flush();

    // ── LOADING state: show loading screen, then process burst ──
    if (g_gameState == GameState::LOADING && !g_worldInitialized) {
      g_loadingFrames++;
      // Render loading screen for a few frames before doing the heavy burst
      if (g_loadingFrames <= 3) {
        // Just poll lightly and render loading bar at 0%
        g_server.Poll();
        RenderLoadingFrame(0.0f, "Connecting...");
        continue;
      } else {
        // Poll aggressively to receive all world data
        RenderLoadingFrame(0.02f, "Receiving world data...");
        for (int burst = 0; burst < 50; burst++) {
          g_server.Poll();
          usleep(10000);
        }
        // Switch packet handler to game mode before initializing
        RenderLoadingFrame(0.05f, "Initializing...");
        g_gameState = GameState::INGAME;

        // Check if a map change arrived during the burst
        auto &mc = ClientPacketHandler::GetPendingMapChange();
        bool hasMapChange = mc.pending;
        uint8_t pendMapId = mc.mapId;
        uint8_t pendSpawnX = mc.spawnX, pendSpawnY = mc.spawnY;
        if (hasMapChange) mc.pending = false;

        // Scale InitGameWorld progress to 0-50% if map change follows, 0-100% if not
        float igwScale = hasMapChange ? 0.50f : 1.0f;
        InitGameWorld(serverData, [igwScale](float p, const char *s) {
          RenderLoadingFrame(p * igwScale, s);
        });

        // If map change pending, handle the transition
        if (hasMapChange) {
          if ((int)pendMapId != g_currentMapId) {
            // Different map than CharInfo indicated — full terrain reload
            RenderLoadingFrame(0.50f, "Changing map...");
            ChangeMap(pendMapId, pendSpawnX, pendSpawnY, [](float p, const char *s) {
              RenderLoadingFrame(0.50f + p * 0.40f, s);
            });
          }
          // Tell server we're on the map — triggers deferred NPC/monster viewport
          glm::vec3 hp = g_hero.GetPosition();
          g_server.SendPrecisePosition(hp.x, hp.z);
          g_server.Flush();

          // Poll until monsters arrive (or timeout after ~2s)
          RenderLoadingFrame(0.92f, "Waiting for spawns...");
          for (int wait = 0; wait < 80; wait++) {
            g_server.Poll();
            usleep(25000);
            if (g_monsterManager.GetMonsterCount() > 0) break;
          }
          RenderLoadingFrame(0.98f, "Spawns received");
        }

        // Hold at 100%
        for (int i = 0; i < 15; i++)
          RenderLoadingFrame(1.0f, "Ready");

        g_worldInitialized = true;
        std::cout << "[State] -> INGAME" << std::endl;

        // Play map theme once on first visit
        if (GetMapConfig(g_currentMapId)->safeMusic)
          SoundManager::CrossfadeTo(g_dataPath + "/" + GetMapConfig(g_currentMapId)->safeMusic);

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
            objectDebugIdx < (int)g_terrainDataPtr->objects.size()) {
          auto &debugObj = g_terrainDataPtr->objects[objectDebugIdx];
          g_hero.SetPosition(debugObj.position);
          g_hero.SnapToTerrain();
          g_camera.SetPosition(g_hero.GetPosition());
          objectDebugName = "obj_type" + std::to_string(debugObj.type) +
                            "_idx" + std::to_string(objectDebugIdx);
          if (!autoGif)
            autoScreenshot = true;
        }
      }
    }

    // ── CHAR_SELECT state: update and render character select scene ──
    if (g_gameState == GameState::CHAR_SELECT ||
        g_gameState == GameState::CONNECTING) {
      {
        static bool loggedOnce = false;
        if (!loggedOnce) {
          std::cout << "[BGFX Debug] First CHAR_SELECT frame rendering"
                    << std::endl;
          loggedOnce = true;
        }
      }
      CharacterSelect::Update(deltaTime);

      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);

      // Lazy-init post-processing (called before world init for char select)
      { static bool ppInit = false;
        if (!ppInit) { InitPostProcess(); ppInit = true; }
      }
      // Char select: render to scene FBO for post-processing.
      // CharacterSelect::Render() sets its own view order (shadow → face FBO → scene → PP).
      bgfx::setViewOrder(0, 0, nullptr);
      bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                          0x000000FF, 1.0f, 0);
      bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
      if (g_postProcess.enabled) {
        ResizePostProcessFBOs(fbW, fbH);
        g_postProcess.bloomIntensity = 0.15f;
        g_postProcess.bloomThreshold = 0.5f;
        g_postProcess.vignetteStrength = 0.08f;
        g_postProcess.colorTint = glm::vec3(1.0f, 1.0f, 0.98f);
        g_postProcess.gradingStrength = 0.2f;
        if (bgfx::isValid(g_postProcess.sceneFB))
          bgfx::setViewFrameBuffer(0, g_postProcess.sceneFB);
        else
          bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
      } else {
        bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
      }
      bgfx::touch(0);

      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);

      // ImGui frame — must be started before CharacterSelect::Render() because
      // Render() queues ImGui draw commands (buttons, text). Actual GL draw
      // happens at ImGui_ImplOpenGL3_RenderDrawData() after post-processing.
      ImGui_BackendNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::GetIO().FontGlobalScale = (float)winH / 768.0f / g_fontPreScale;
      ImGui::NewFrame();
      // Suppress ImGui's default fallback window
      ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowPos(ImVec2(-200, -200), ImGuiCond_Always);
      ImGui::Begin("Debug##Default", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImGui::End();

      CharacterSelect::Render(winW, winH);

      // Post-processing: bloom + vignette + composite (reads scene FBO → backbuffer)
      if (g_postProcess.enabled)
        RenderPostProcess(fbW, fbH);

      // Draw ImGui UI on top of post-processed scene (sharp, unaffected by
      // bloom)
      ImGui::Render();
      ImGui_BackendRenderDrawData(ImGui::GetDrawData());

      // Poll mouse clicks for character slot selection AFTER ImGui render
      // so WantCaptureMouse is accurate (prevents button clicks selecting
      // slots)
      {
        static bool prevMouseDown = false;
        bool mouseDown =
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouseDown && !prevMouseDown && !ImGui::GetIO().WantCaptureMouse) {
          double mx, my;
          glfwGetCursorPos(window, &mx, &my);
          CharacterSelect::OnMouseClick(mx, my, winW, winH);
        }
        prevMouseDown = mouseDown;
      }

      // Auto-screenshot: capture char select screen after a few frames
      {
        static int charSelectFrame = 0;
        charSelectFrame++;
        if (charSelectFrame == 30) {
          std::cout << "[BGFX] Requesting char select screenshot (frame "
                    << charSelectFrame << ")" << std::endl;
          g_bgfxCallback.pendingPath = "screenshots/bgfx_charselect.png";
          std::filesystem::create_directories("screenshots");
          bgfx::requestScreenShot(BGFX_INVALID_HANDLE,
                                  "screenshots/bgfx_charselect.png");
        }
      }
      bgfx::frame();
      continue; // Skip game world rendering
    }

    // ── LOADING state: already handled above with RenderLoadingFrame ──
    if (g_gameState == GameState::LOADING) {
      continue;
    }

    // ═══════════════════════════════════════════════
    // INGAME state: normal game world update + render
    // ═══════════════════════════════════════════════
    {
      static bool loggedIngame = false;
      if (!loggedIngame) {
        std::cout << "[BGFX Debug] First INGAME frame" << std::endl;
        loggedIngame = true;
      }
    }

    // Check for pending map change from server — start transition overlay
    {
      auto &mc = ClientPacketHandler::GetPendingMapChange();
      if (mc.pending) {
        if (!g_mapTransitionActive) {
          // No transition in progress — start one (e.g. walking into gate)
          g_mapTransitionActive = true;
          g_mapTransitionPhase = 0; // fade-in
          g_mapTransitionAlpha = 0.0f;
          g_mapTransitionFrames = 0;
        }
        // Always update target from server packet (authoritative)
        g_mapTransMapId = mc.mapId;
        g_mapTransSpawnX = mc.spawnX;
        g_mapTransSpawnY = mc.spawnY;
        mc.pending = false;
      }
    }

    // Map transition preloader: fade-in → load → fade-out
    if (g_mapTransitionActive) {
      if (g_mapTransitionPhase == 0) {
        // Phase 0: Fade to black
        g_mapTransitionAlpha += deltaTime * 4.0f; // ~0.25s fade-in
        if (g_mapTransitionAlpha >= 1.0f) {
          g_mapTransitionAlpha = 1.0f;
          g_mapTransitionFrames++;
          // Wait 2 rendered frames at full black before heavy work
          if (g_mapTransitionFrames >= 2) {
            g_mapTransitionPhase = 1;
          }
        }
      } else if (g_mapTransitionPhase == 1) {
        // Phase 1: Do the heavy map loading (runs once)
        ChangeMap(g_mapTransMapId, g_mapTransSpawnX, g_mapTransSpawnY);
        // Tell server we're ready — triggers deferred NPC/monster viewport send
        glm::vec3 hp = g_hero.GetPosition();
        g_server.SendPrecisePosition(hp.x, hp.z);
        g_server.Flush();
        // Wait for monsters/NPCs to arrive before closing preloader
        RenderLoadingFrame(0.92f, "Waiting for spawns...");
        for (int wait = 0; wait < 80; wait++) {
          g_server.Poll();
          usleep(25000);
          if (g_monsterManager.GetMonsterCount() > 0) break;
        }
        RenderLoadingFrame(0.98f, "Spawns received");
        // Hold at 100% so user sees bar complete
        for (int i = 0; i < 15; i++)
          RenderLoadingFrame(1.0f, "Ready");
        g_mapTransitionPhase = 2;
        g_mapTransitionFrames = 0;
      } else if (g_mapTransitionPhase == 2) {
        // Phase 2: Fade out
        g_mapTransitionAlpha -= deltaTime * 2.0f; // ~0.5s fade-out
        if (g_mapTransitionAlpha <= 0.0f) {
          g_mapTransitionAlpha = 0.0f;
          g_mapTransitionActive = false;
          // Deferred sound: play ambient + music NOW that preloader is gone
          if (g_deferredSoundMapId >= 0) {
            int mid = g_deferredSoundMapId;
            g_deferredSoundMapId = -1;
            glm::vec3 hp = g_hero.GetPosition();
            const int S = TerrainParser::TERRAIN_SIZE;
            int gz = (int)(hp.x / 100.0f), gx = (int)(hp.z / 100.0f);
            bool inSafe = (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
                          (g_terrainDataPtr->mapping.attributes[gz * S + gx] & 0x01) != 0;
            g_hero.SetInSafeZone(inSafe);
            bool indoorLoop = (mid == 1 || mid == 4 || mid == 7);
            if (GetMapConfig(mid)->ambientLoop &&
                (indoorLoop || !inSafe) && !GetMapConfig(mid)->hasWind)
              SoundManager::PlayLoop(GetMapConfig(mid)->ambientLoop);
            if (GetMapConfig(mid)->safeMusic)
              SoundManager::CrossfadeTo(g_dataPath + "/" + GetMapConfig(mid)->safeMusic);
            else if (GetMapConfig(mid)->wildMusic)
              SoundManager::CrossfadeTo(g_dataPath + "/" + GetMapConfig(mid)->wildMusic);
          }
        }
      }
    }

    InputHandler::ProcessInput(window, deltaTime);
    g_camera.Update(deltaTime);

    // Main 5.2 Lorencia uses static daylight (no day/night cycle)
    g_worldTime += deltaTime * 25.0f; // Still tick for chrome/sun animation
    // Per-map luminosity from config (only push to renderers when changed)
    {
      float newLum = g_mapCfg->luminosity;
      if (newLum != g_luminosity) {
        g_luminosity = newLum;
        g_terrain.SetLuminosity(g_luminosity);
        g_objectRenderer.SetLuminosity(g_luminosity);
        g_hero.SetLuminosity(g_luminosity);
        g_npcManager.SetLuminosity(g_luminosity);
        g_monsterManager.SetLuminosity(g_luminosity);
        g_boidManager.SetLuminosity(g_luminosity);
        g_grass.SetLuminosity(g_luminosity);
      }
    }

    // Send player position to server periodically (~4Hz)
    {
      // Tick potion cooldown
      if (g_potionCooldown > 0.0f)
        g_potionCooldown = std::max(0.0f, g_potionCooldown - deltaTime);

      // Tick buff durations (Elf auras)
      if (g_clientState) {
        for (int b = 0; b < 2; b++) {
          if (g_clientState->activeBuffs[b].active) {
            g_clientState->activeBuffs[b].remaining -= deltaTime;
            if (g_clientState->activeBuffs[b].remaining <= 0)
              g_clientState->activeBuffs[b].active = false;
          }
        }
        // Tick poison duration + sync tint to hero
        if (g_clientState->poisoned) {
          g_clientState->poisonRemaining -= deltaTime;
          if (g_clientState->poisonRemaining <= 0) {
            g_clientState->poisoned = false;
            g_clientState->poisonRemaining = 0.0f;
          }
        }
        g_hero.SetPoisoned(g_clientState->poisoned);
        // Tick freeze duration + sync tint to hero
        if (g_clientState->frozen) {
          g_clientState->frozenRemaining -= deltaTime;
          if (g_clientState->frozenRemaining <= 0) {
            g_clientState->frozen = false;
            g_clientState->frozenRemaining = 0.0f;
          }
        }
        g_hero.SetFrozen(g_clientState->frozen);
        // Sync buff aura state to hero for VFX particle spawning
        g_hero.SetBuffDefense(g_clientState->activeBuffs[0].active);
        g_hero.SetBuffDamage(g_clientState->activeBuffs[1].active);
      }

      static float posTimer = 0.0f;
      static int lastGridX = -1, lastGridY = -1;
      posTimer += deltaTime;
      if (posTimer >= 0.10f && !g_mapTransitionActive) {
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

      // 3D ambient wind — orbits slowly around the player (maps with wind)
      static float windAngle = 0.0f;
      if (g_mapCfg->hasWind) {
        if (!g_windStarted) {
          float wx = lp.x + 200.0f;
          SoundManager::Play3DLoop(SOUND_WIND01, wx, lp.y + 50.0f, lp.z, 0.15f);
          g_windStarted = true;
        }
        windAngle += deltaTime * 0.3f; // ~0.3 rad/s = full circle in ~21s
        float windDist = 200.0f;
        float wx = lp.x + cosf(windAngle) * windDist;
        float wz = lp.z + sinf(windAngle) * windDist;
        SoundManager::UpdateSource3D(SOUND_WIND01, wx, lp.y + 50.0f, wz);
      } else {
        g_windStarted = false;
      }

      // Noria: intermittent forest sounds (Main 5.2: rand_fps_check(512))
      // At 60fps, rand()%8000 ≈ once every ~133s on average (subtle ambient)
      if (g_currentMapId == 3 && rand() % 8000 == 0) {
        SoundManager::Play(SOUND_FOREST01, 0.25f);
      }
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerFacing(g_hero.GetFacing());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
    g_monsterManager.SetPlayerInSafeZone(g_hero.IsInSafeZone());
    g_monsterManager.SetPlayerMoveTarget(g_hero.GetMoveTarget(), g_hero.IsMoving());
    g_monsterManager.Update(deltaTime);

    // Teleport cooldown ticks regardless of safe zone
    g_hero.TickTeleportCooldown(deltaTime);

    // Validate right-mouse held state against actual GLFW state each frame
    // (prevents stuck state if release event was missed during focus loss)
    if (g_rightMouseHeld &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
      g_rightMouseHeld = false;
    }

    // Hero combat: update attack state machine, send attack packet on hit
    // Block all combat in safe zone — but don't stop movement
    {
      bool nowInSafe = g_hero.IsInSafeZone();
      static bool wasInSafe = false;
      // Always run UpdateState (handles RESPAWNING→ALIVE in safe zone)
      g_hero.UpdateState(deltaTime);
      if (nowInSafe) {
        // On transition INTO safe zone: cancel any active attack once
        if (!wasInSafe &&
            (g_hero.GetAttackTarget() >= 0 || g_hero.IsAttacking())) {
          g_hero.CancelAttack();
        }
        // Don't update attack while in safe zone
      } else if (g_teleportingToTown) {
        // Block all attacks during teleport cast
        if (g_hero.GetAttackTarget() >= 0 || g_hero.IsAttacking()) {
          g_hero.CancelAttack();
          g_hero.ClearGlobalCooldown();
        }
      } else {
        g_hero.UpdateAttack(deltaTime);
        // Cancel attack if player is in safe zone (server rejects these anyway)
        if (g_hero.IsInSafeZone() && g_hero.GetAttackTarget() >= 0) {
          g_hero.CancelAttack();
          g_hero.ClearGlobalCooldown();
        }
        if (g_hero.CheckAttackHit()) {
          // Dismount when attack actually fires (not on click — user can cancel)
          if (g_hero.IsMounted())
            g_hero.UnequipMount();
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
                if (skillId == 12) {
                  // Flash: delay damage until beam spawns at frame 7.0
                  g_hero.SetPendingAquaPacket(serverIdx, skillId, 0.0f, 0.0f);
                } else {
                  g_server.SendSkillAttack(serverIdx, skillId);
                }
              } else {
                InventoryUI::ShowNotification(isDK ? "Not enough AG!"
                                                   : "Not enough Mana!");
              }
            } else {
              // Bow/crossbow: check ammo before sending attack
              bool isBow = g_hero.HasWeapon() && g_hero.GetWeaponCategory() == 4;
              if (isBow && g_equipSlots[1].quantity == 0) {
                // Auto-equip arrows/bolts from inventory
                // Bows (idx 0-6, 17) use Arrows (cat4 idx15),
                // Crossbows (idx 8-14, 16, 18) use Bolts (cat4 idx7)
                uint8_t weapIdx = g_equipSlots[0].itemIndex;
                bool isCrossbow = (weapIdx >= 8 && weapIdx <= 14) ||
                                  weapIdx == 16 || weapIdx == 18;
                uint8_t ammoIdx = isCrossbow ? 7 : 15;
                int16_t ammoDefIndex = 4 * 32 + ammoIdx;
                int foundSlot = -1;
                for (int s = 0; s < INVENTORY_SLOTS; ++s) {
                  if (g_inventory[s].occupied && g_inventory[s].primary &&
                      g_inventory[s].defIndex == ammoDefIndex) {
                    foundSlot = s;
                    break;
                  }
                }
                if (foundSlot >= 0) {
                  g_server.SendEquip(g_heroCharacterId, 1, 4, ammoIdx,
                                     g_inventory[foundSlot].itemLevel);
                  g_equipSlots[1].category = 4;
                  g_equipSlots[1].itemIndex = ammoIdx;
                  g_equipSlots[1].quantity = g_inventory[foundSlot].quantity;
                  g_equipSlots[1].equipped = true;
                  InventoryUI::ClearBagItem(foundSlot);
                  InventoryUI::ShowNotification(isCrossbow ? "Reloading bolts..."
                                                           : "Reloading arrows...");
                  SoundManager::Play(SOUND_GET_ITEM01);
                } else {
                  InventoryUI::ShowNotification("Out of arrows!");
                  SoundManager::Play(SOUND_ERROR01);
                }
                g_hero.CancelAttack();
              } else {
              g_server.SendAttack(serverIdx);
              // Bow/crossbow: spawn visual arrow projectile toward target
              if (isBow) {
                auto mi = g_monsterManager.GetMonsterInfo(targetIdx);
                // Arrow origin: bone 42 (left hand) world position
                glm::vec3 src = g_hero.GetPosition() + glm::vec3(0, 100, 0);
                const auto &bones = g_hero.GetCachedBones();
                if (42 < (int)bones.size()) {
                  glm::mat4 modelRot(1.0f);
                  modelRot = glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0,0,1));
                  modelRot = glm::rotate(modelRot, glm::radians(-90.0f), glm::vec3(0,1,0));
                  modelRot = glm::rotate(modelRot, g_hero.GetFacing(), glm::vec3(0,0,1));
                  const auto &bm = bones[42];
                  glm::vec3 boneLocal(bm[0][3], bm[1][3], bm[2][3]);
                  glm::vec3 boneWorld = glm::vec3(modelRot * glm::vec4(boneLocal, 1.0f));
                  src = boneWorld + g_hero.GetPosition();
                }
                glm::vec3 dst = mi.position + glm::vec3(0, mi.height * 0.5f, 0);

                // Main 5.2: per-weapon arrow model variant
                uint8_t wIdx = g_equipSlots[0].itemIndex;
                int arrowVar = MonsterManager::GetArrowVariant(wIdx);

                // Main 5.2 CreateArrows: +skill bows/crossbows fire spread
                bool hasSkill = (g_equipSlots[0].optionFlags & 0x80) != 0;
                if (hasSkill) {
                  // Rotate dst around src in XZ plane by yaw offset
                  auto spawnSpread = [&](float angleDeg) {
                    float rad = glm::radians(angleDeg);
                    glm::vec3 d = dst - src;
                    float rx = d.x * cosf(rad) - d.z * sinf(rad);
                    float rz = d.x * sinf(rad) + d.z * cosf(rad);
                    g_monsterManager.SpawnArrow(src, src + glm::vec3(rx, d.y, rz), 2000.0f, arrowVar);
                  };
                  // Main 5.2: special crossbows (idx 18,19) = 4 arrows ±5°/±15��
                  // All others = 3 arrows: center, ±15°
                  if (wIdx == 18 || wIdx == 19) {
                    spawnSpread(5.0f);
                    spawnSpread(15.0f);
                    spawnSpread(-5.0f);
                    spawnSpread(-15.0f);
                  } else {
                    spawnSpread(0.0f);
                    spawnSpread(15.0f);
                    spawnSpread(-15.0f);
                  }
                } else {
                  g_monsterManager.SpawnArrow(src, dst, 2000.0f, arrowVar);
                }
              }
              } // end ammo check else
            }
          }
        }
        // Flash: send delayed damage packet when beam spawns at frame 7.0
        {
          uint16_t aqTarget;
          uint8_t aqSkill;
          float aqX, aqZ;
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
                  g_hoveredMonster < g_monsterManager.GetMonsterCount() &&
                  !g_monsterManager.IsOwnSummon(g_hoveredMonster)) {
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
                    InventoryUI::ShowNotification(isDK ? "Not enough AG!"
                                                       : "Not enough Mana!");
                    g_hero.CancelAttack();
                    g_hero.ClearGlobalCooldown();
                  }
                } else {
                  g_hero.CancelAttack();
                  g_hero.ClearGlobalCooldown();
                }
              } else {
                g_hero.CancelAttack();
                g_hero.ClearGlobalCooldown();
              }
            } else if (targetDead) {
              // Target died, no RMC — clear attack and GCD so player can
              // immediately click a new target without double-clicking
              g_hero.CancelAttack();
              g_hero.ClearGlobalCooldown();
            } else if (g_rightMouseHeld && g_rmcSkillId >= 0) {
              // RMC held + target alive — check if hovered a different monster
              int nextTarget = targetIdx;
              glm::vec3 nextPos = mi.position;
              if (g_hoveredMonster >= 0 && g_hoveredMonster != targetIdx &&
                  g_hoveredMonster < g_monsterManager.GetMonsterCount() &&
                  !g_monsterManager.IsOwnSummon(g_hoveredMonster)) {
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
                InventoryUI::ShowNotification(isDK ? "Not enough AG!"
                                                   : "Not enough Mana!");
                g_hero.CancelAttack();
              }
            } else if (g_hero.GetActiveSkillId() == 0) {
              // Normal attack auto-re-engage — check if monster is reachable
              // by tracking HP changes across attack cycles
              if (g_autoAttackLastHp == mi.hp && mi.hp > 0) {
                g_autoAttackSameHpCount++;
                if (g_autoAttackSameHpCount >= 3) {
                  // Monster HP unchanged after 3 attacks — likely unreachable
                  g_hero.CancelAttack();
                  g_hero.ClearGlobalCooldown();
                  g_autoAttackSameHpCount = 0;
                  g_autoAttackLastHp = -1;
                }
              } else {
                g_autoAttackSameHpCount = 0;
              }
              g_autoAttackLastHp = mi.hp;
              if (g_hero.GetAttackTarget() >= 0) {
                g_hero.AttackMonster(targetIdx, mi.position);
              }
            }
          } else {
            // Target index out of bounds (viewport refreshed) — cancel
            g_hero.CancelAttack();
            g_hero.ClearGlobalCooldown();
          }
        }

        // Self-AoE continuous casting: re-cast when GCD expires + RMB held
        bool isAoESkill =
            (g_rmcSkillId == 8 || g_rmcSkillId == 9 || g_rmcSkillId == 10 ||
             g_rmcSkillId == 12 || g_rmcSkillId == 14 || g_rmcSkillId == 41 ||
             g_rmcSkillId == 42 || g_rmcSkillId == 43);
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() < 0 && g_rightMouseHeld && isAoESkill &&
            g_hero.GetGlobalCooldown() <= 0.0f && !g_mouseOverUIPanel) {
          uint8_t skillId = (uint8_t)g_rmcSkillId;
          int cost = InventoryUI::GetSkillResourceCost(skillId);
          bool isDK = (g_hero.GetClass() == 16);
          int curResource = isDK ? g_serverAG : g_serverMP;
          if (curResource >= cost) {
            glm::vec3 heroPos = g_hero.GetPosition();
            bool isMeleeAoE = (skillId == 41 || skillId == 42 || skillId == 43);
            bool casterCentered =
                (skillId == 9 || skillId == 10 || skillId == 14 || isMeleeAoE);
            glm::vec3 groundTarget = heroPos;
            if (!isMeleeAoE) {
              double mx, my;
              glfwGetCursorPos(window, &mx, &my);
              RayPicker::ScreenToTerrain(window, mx, my, groundTarget);
            }
            g_hero.CastSelfAoE(skillId, isMeleeAoE ? heroPos : groundTarget);
            float atkX = casterCentered ? heroPos.x : groundTarget.x;
            float atkZ = casterCentered ? heroPos.z : groundTarget.z;
            if (skillId == 12) {
              g_hero.SetPendingAquaPacket(0xFFFF, skillId, atkX, atkZ);
            } else {
              g_server.SendSkillAttack(0xFFFF, skillId, atkX, atkZ);
            }
            // Optimistically deduct resource to prevent spam before server
            // reply
            if (isDK)
              g_serverAG -= cost;
            else
              g_serverMP -= cost;
          }
        }
      }
      wasInSafe = nowInSafe;
    }

    // Skill learning: play heal animation over 3 seconds, then return to idle
    if (g_isLearningSkill) {
      if (g_learnSkillTimer == 0.0f)
        SoundManager::Play(SOUND_SUMMON); // eSummon.wav on learn start
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
          g_hero.SetAction(g_hero.defaultIdleAction());
        else
          g_hero.SetAction(g_hero.weaponIdleAction());
      }
    }

    // Teleport/warp cast: play cast animation, then warp to target
    // g_warpTargetMapId >= 0: map warp (from M panel), -1: not used (T removed)
    if (g_teleportingToTown && g_hero.IsDead()) {
      g_teleportingToTown = false; // Cancel teleport if hero died during cast
    }
    if (g_teleportingToTown) {
      g_teleportTimer -= deltaTime;
      g_hero.SetSlowAnimDuration(TELEPORT_CAST_TIME);
      g_hero.SetAction(HeroCharacter::ACTION_SKILL_VITALITY);
      if (g_teleportTimer <= 0.0f) {
        g_teleportingToTown = false;
        g_hero.SetSlowAnimDuration(0.0f);

        // Dismount before teleporting
        if (g_hero.IsMounted())
          g_hero.UnequipMount();

        int targetMapId = g_warpTargetMapId;
        int targetGX = g_warpTargetGX;
        int targetGZ = g_warpTargetGZ;
        g_warpTargetMapId = -1; // Reset

        // Always use full warp path (even same-map) so server relocates
        // summons, refreshes monster viewport, and updates spawn position.
        SoundManager::StopAll();
        g_server.SendWarpCommand(targetMapId, targetGX, targetGZ);
        g_hero.SetAction(1);
        g_hero.SetTeleportCooldown();
        g_mapTransitionActive = true;
        g_mapTransitionPhase = 0;
        g_mapTransitionAlpha = 0.0f;
        g_mapTransitionFrames = 0;
        g_mapTransMapId = targetMapId;
        g_mapTransSpawnX = targetGX;
        g_mapTransSpawnY = targetGZ;
      }
    }

    // Mount toggle: 1-second preloader, then mount/dismount
    if (g_mountToggling) {
      g_mountToggleTimer -= deltaTime;
      // Block movement while mounting
      if (g_hero.IsMoving())
        g_hero.StopMoving();
      if (g_mountToggleTimer <= 0.0f) {
        g_mountToggling = false;
        if (g_hero.IsMounted()) {
          // Dismount
          g_hero.UnequipMount();
        } else if (g_hero.HasMountEquipped()) {
          // Mount
          g_hero.EquipMount(g_hero.GetMountItemIndex());
        }
      }
    }

    // Hero respawn: uses the same warp system as M-key teleport
    if (g_hero.ReadyToRespawn()) {
      // Clear debuffs on respawn
      if (g_clientState) {
        g_clientState->poisoned = false;
        g_clientState->poisonRemaining = 0.0f;
        g_clientState->poisonMaxDuration = 0.0f;
        g_hero.SetPoisoned(false);
        g_clientState->frozen = false;
        g_clientState->frozenRemaining = 0.0f;
        g_hero.SetFrozen(false);
        for (int b = 0; b < 2; b++) {
          g_clientState->activeBuffs[b].active = false;
          g_clientState->activeBuffs[b].remaining = 0.0f;
        }
      }
      // Per-map respawn points
      int respawnMapId = g_currentMapId;
      int respawnGX = 137, respawnGZ = 126; // Lorencia default
      if (g_currentMapId == 1) { respawnMapId = 0; respawnGX = 137; respawnGZ = 126; }
      else if (g_currentMapId == 2) { respawnGX = 215; respawnGZ = 47; }
      else if (g_currentMapId == 3) { respawnGX = 174; respawnGZ = 110; }
      else if (g_currentMapId == 4) { respawnGX = 208; respawnGZ = 75; }

      // Mark hero alive immediately (prevents ReadyToRespawn re-entry)
      glm::vec3 spawnPos((float)respawnGZ * 100.0f, 0.0f, (float)respawnGX * 100.0f);
      g_hero.Respawn(spawnPos);
      g_serverHP = g_serverMaxHP;
      g_serverMP = g_serverMaxMP;

      // Dismount before respawn
      if (g_hero.IsMounted()) g_hero.UnequipMount();

      // Use unified warp path (same as M-key teleport)
      SoundManager::StopAll();
      g_server.SendWarpCommand(respawnMapId, respawnGX, respawnGZ);
      g_mapTransitionActive = true;
      g_mapTransitionPhase = 0;
      g_mapTransitionAlpha = 0.8f; // Start mostly black (death screen)
      g_mapTransitionFrames = 0;
      g_mapTransMapId = respawnMapId;
      g_mapTransSpawnX = respawnGX;
      g_mapTransSpawnY = respawnGZ;

      // Notify server player is alive (clears session.dead)
      g_server.SendCharSave(
          (uint16_t)g_heroCharacterId, (uint16_t)g_serverLevel,
          (uint16_t)g_serverStr, (uint16_t)g_serverDex, (uint16_t)g_serverVit,
          (uint16_t)g_serverEne, (uint16_t)g_serverMaxHP,
          (uint16_t)g_serverMaxHP, (uint16_t)g_serverMaxMP,
          (uint16_t)g_serverMaxMP, (uint16_t)g_serverMaxAG,
          (uint16_t)g_serverMaxAG, (uint16_t)g_serverLevelUpPoints,
          (uint64_t)g_serverXP, g_skillBar, g_potionBar, g_rmcSkillId);
    }

    // Periodic autosave (quickslots, stats) every 60 seconds
    g_autoSaveTimer += deltaTime;
    if (g_autoSaveTimer >= AUTOSAVE_INTERVAL && !g_hero.IsDead()) {
      g_autoSaveTimer = 0.0f;
      g_server.SendCharSave(
          (uint16_t)g_heroCharacterId, (uint16_t)g_serverLevel,
          (uint16_t)g_serverStr, (uint16_t)g_serverDex, (uint16_t)g_serverVit,
          (uint16_t)g_serverEne, (uint16_t)g_serverHP, (uint16_t)g_serverMaxHP,
          (uint16_t)g_serverMP, (uint16_t)g_serverMaxMP, (uint16_t)g_serverAG,
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
            float h = g_terrainDataPtr->heightmap[iz * 256 + ix];
            gi.position.y = h + 0.5f;
          }
        }
        float dist = glm::length(
            glm::vec3(heroPos.x - gi.position.x, 0, heroPos.z - gi.position.z));
        // Auto-pickup Zen only (items require explicit click)
        if (gi.defIndex == -1 && dist < 120.0f && !g_hero.IsDead()) {
          g_server.SendPickup(gi.dropIndex);
          gi.active =
              false; // Optimistic remove (sound plays on server confirm)
        }
        // Despawn after 60s
        if (gi.timer > 60.0f)
          gi.active = false;
      }
    }

    // Roof hiding: read layer1 tile at hero position, fade roof types
    if (g_terrainDataPtr) {
      glm::vec3 heroPos = g_hero.GetPosition();
      const int S = TerrainParser::TERRAIN_SIZE;
      int gz = (int)(heroPos.x / 100.0f);
      int gx = (int)(heroPos.z / 100.0f);
      uint8_t heroTile = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroTile = g_terrainDataPtr->mapping.layer1[gz * S + gx];
      // Indoor detection — config-driven roof hiding (Main 5.2: ZzzObject.cpp)
      if (g_mapCfg->roofTypeCount > 0) {
        bool isIndoor = false;
        for (int i = 0; i < g_mapCfg->indoorTileCount; ++i)
          if (heroTile == g_mapCfg->indoorTiles[i])
            isIndoor = true;
        if (g_mapCfg->indoorAbove && heroTile >= g_mapCfg->indoorThreshold)
          isIndoor = true;
        float target = isIndoor ? 0.0f : 1.0f;
        for (int i = 0; i < g_mapCfg->roofTypeCount; ++i)
          g_typeAlphaTarget[g_mapCfg->roofTypes[i]] = target;
      }
      // Fast fade — nearly instant (95%+ in 1-2 frames)
      float blend = 1.0f - std::exp(-20.0f * deltaTime);
      for (auto &[type, alpha] : g_typeAlpha) {
        auto it = g_typeAlphaTarget.find(type);
        float target = (it != g_typeAlphaTarget.end()) ? it->second : 1.0f;
        alpha += (target - alpha) * blend;
      }
      g_objectRenderer.SetTypeAlpha(g_typeAlpha);

      // Door animation: proximity-based swing/slide (Main 5.2:
      // ZzzObject.cpp:3871)
      if (g_mapCfg->hasDoors)
        g_objectRenderer.UpdateDoors(heroPos, deltaTime);

      // Lost Tower skull tracking (Main 5.2: ZzzEffectFireLeave.cpp:88-113)
      if (g_currentMapId == 4) {
        bool heroMoving = g_hero.IsMoving();
        g_objectRenderer.UpdateSkulls(heroPos, heroMoving, deltaTime);
      }

      // SafeZone detection: attribute 0x01 = TW_SAFEZONE
      uint8_t heroAttr = 0;
      if (gx >= 0 && gz >= 0 && gx < S && gz < S)
        heroAttr = g_terrainDataPtr->mapping.attributes[gz * S + gx];
      bool wasInSafeZone = g_hero.IsInSafeZone();
      bool nowInSafeZone = (heroAttr & 0x01) != 0;
      g_hero.SetInSafeZone(nowInSafeZone);
      // Config-driven safe zone music/wind transitions
      if (nowInSafeZone && !wasInSafeZone) {
        if (g_mapCfg->hasWind)
          SoundManager::Stop(SOUND_WIND01);
        if (g_mapCfg->safeMusic)
          SoundManager::CrossfadeTo(g_dataPath + "/" + g_mapCfg->safeMusic);
      } else if (!nowInSafeZone && wasInSafeZone) {
        if (g_mapCfg->hasWind) {
          // 3D wind loop handles this with proper gain — just restart it
          g_windStarted = false;
        }
        if (g_mapCfg->wildMusic)
          SoundManager::CrossfadeTo(g_dataPath + "/" + g_mapCfg->wildMusic);
        else
          SoundManager::FadeOut();
      }
    }

    // Update music fade transitions
    SoundManager::UpdateMusic(deltaTime);

    // Re-trigger safe zone music after silence timeout (plays once, then waits again)
    if (g_hero.IsInSafeZone() && !SoundManager::IsMusicPlaying() && g_mapCfg->safeMusic)
      SoundManager::CrossfadeTo(g_dataPath + "/" + g_mapCfg->safeMusic);

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
    // BGFX view ordering: shadow pass (view 8) renders before scene (view 0)
    // and post-processing (views 1-6). Views 9+ (ImGui 30, 200, 201) use default order.
    {
      bgfx::ViewId order[] = { SHADOW_VIEW, 0, PP_VIEW_BRIGHT, PP_VIEW_BLUR0,
                                PP_VIEW_BLUR1, PP_VIEW_BLUR2, PP_VIEW_BLUR3,
                                PP_VIEW_COMPOSITE, 7 };
      bgfx::setViewOrder(0, 9, order);
    }
    // BGFX view 0 setup: clear, viewport, and post-process FBO
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                        0x000000FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
    if (g_postProcess.enabled) {
      ResizePostProcessFBOs(fbW, fbH);
      if (bgfx::isValid(g_postProcess.sceneFB))
        bgfx::setViewFrameBuffer(0, g_postProcess.sceneFB);
    } else {
      bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
    }
    bgfx::touch(0);

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    glm::mat4 projection =
        g_camera.GetProjectionMatrix((float)winW, (float)winH);
    glm::mat4 view = g_camera.GetViewMatrix();
    glm::vec3 camPos = g_camera.GetPosition();

    // Main 5.2: EarthQuake variable — camera shake from Rageful Blow
    float cameraShake = g_vfxManager.GetCameraShake();
    if (std::abs(cameraShake) > 0.001f) {
      glm::vec3 shakeOffset(cameraShake * 5.0f, cameraShake * 3.0f, 0.0f);
      view = glm::translate(view, shakeOffset);
    }

    // ── Shadow map pass: render depth from directional light ──
    if (g_shadowMap.depthShader && bgfx::isValid(g_shadowMap.fb)) {
      glm::vec3 heroPos = g_hero.GetPosition();
      // Directional light: nearly overhead sun with subtle side tilt
      glm::vec3 lightDir = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.1f));
      glm::vec3 lightPos = heroPos - lightDir * 2000.0f;
      glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
      // Avoid degenerate up vector
      if (std::abs(glm::dot(lightDir, up)) > 0.99f)
        up = glm::vec3(1.0f, 0.0f, 0.0f);
      glm::mat4 lightView = glm::lookAt(lightPos, heroPos, up);
      // Metal uses [0,1] Z clip range; OpenGL uses [-1,1]
      glm::mat4 lightProj = bgfx::getCaps()->homogeneousDepth
        ? glm::ortho(-3500.0f, 3500.0f, -3500.0f, 3500.0f, 100.0f, 8000.0f)
        : glm::orthoRH_ZO(-3500.0f, 3500.0f, -3500.0f, 3500.0f, 100.0f, 8000.0f);
      glm::mat4 lightMtx = lightProj * lightView;

      // Setup shadow view
      bgfx::setViewName(SHADOW_VIEW, "ShadowMap");
      bgfx::setViewRect(SHADOW_VIEW, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
      bgfx::setViewClear(SHADOW_VIEW, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xFFFFFFFF, 1.0f, 0);
      bgfx::setViewFrameBuffer(SHADOW_VIEW, g_shadowMap.fb);
      bgfx::setViewTransform(SHADOW_VIEW, glm::value_ptr(lightView),
                              glm::value_ptr(lightProj));
      bgfx::touch(SHADOW_VIEW);

      // View 8 renders before view 0 via setViewOrder (set above).

      // Submit shadow casters
      g_hero.RenderToShadowMap(SHADOW_VIEW, g_shadowMap.depthShader->program);
      g_monsterManager.RenderToShadowMap(SHADOW_VIEW, g_shadowMap.depthShader->program);
      g_npcManager.RenderToShadowMap(SHADOW_VIEW, g_shadowMap.depthShader->program);

      // Pass shadow map texture + light matrix to all receivers
      g_hero.SetShadowMap(g_shadowMap.colorTex, lightMtx);
      g_monsterManager.SetShadowMap(g_shadowMap.colorTex, lightMtx);
      g_npcManager.SetShadowMap(g_shadowMap.colorTex, lightMtx);
      g_terrain.SetShadowMap(g_shadowMap.colorTex, lightMtx);
    }

    // Sky renders first (behind everything, no depth write)
    if (g_mapCfg->hasSky) {
      g_sky.Render(view, projection, camPos, g_luminosity);
    }

    // Main 5.2: AddTerrainLight — merge world point lights + spell projectile
    // lights Spell lights are transient (per-frame), so rebuild the full list
    // each frame. Static vectors reuse capacity across frames (zero heap allocs
    // after warmup).
    {
      static std::vector<glm::vec3> lightPos, lightCol;
      static std::vector<float> lightRange;
      static std::vector<int> lightObjTypes;
      lightPos.clear();
      lightCol.clear();
      lightRange.clear();
      lightObjTypes.clear();
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
      // Trap terrain lights (Main 5.2 GMAida.cpp: Lance=red, IronStick=blue)
      g_monsterManager.GetTrapPointLights(lightPos, lightCol, lightRange,
                                          lightObjTypes);
      // Pet companion glow light
      g_hero.GetPetLight(lightPos, lightCol, lightRange, lightObjTypes);
      // Main 5.2: enhanced weapons emit terrain point light
      g_hero.GetWeaponLight(lightPos, lightCol, lightRange, lightObjTypes);
      // Update terrain (CPU lightmap) and object renderer (shader uniforms)
      g_terrain.SetPointLights(lightPos, lightCol, lightRange, lightObjTypes);
      g_objectRenderer.SetPointLights(lightPos, lightCol, lightRange);
      // Update character renderers with merged PointLight list
      static std::vector<PointLight> mergedLights;
      mergedLights.clear();
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

    // Main 5.2: grass renders WITH terrain (before objects).
    // Rocks/structures render after and occlude grass via depth buffer.
    if (g_mapCfg->hasGrass) {
      std::vector<GrassRenderer::PushSource> pushSources;
      pushSources.push_back({g_hero.GetPosition(), 100.0f});
      g_grass.Render(view, projection, currentFrame, camPos, pushSources);
    }

    // Lightmap texture is destroyed+recreated each frame by Terrain, so refresh handle
    g_objectRenderer.SetLightmapTexture(g_terrain.GetLightmapTexture());
    g_objectRenderer.Render(view, projection, g_camera.GetPosition(),
                            currentFrame);

    // Main 5.2 level-up VFX: 15 BITMAP_FLARE joints in a ring
    if (g_hero.LeveledUpThisFrame()) {
      g_vfxManager.SpawnLevelUpEffect(g_hero.GetPosition());
      SoundManager::Play(SOUND_LEVEL_UP);
      g_hero.ClearLevelUpFlag();
    }

    // Update effects (VFX rendered after characters for correct layering)
    g_fireEffect.Update(deltaTime, g_hero.GetPosition());
    g_vfxManager.UpdateLevelUpCenter(g_hero.GetPosition());

    // Dungeon trap VFX now handled by MonsterManager::TriggerAttackAnimation
    // (on-attack only, matching Main 5.2 CharacterAnimation trigger pattern).
    // Fire emitters for type 51/39 world objects remain in fire emitter system.

    g_vfxManager.Update(deltaTime);

    // Atlans: underwater bubbles from hero head
    // Main 5.2 EXACT: 1 bubble/frame for 1 second every 10 seconds
    // WorldTime%10000 < 1000 → emit during first 1s of 10s cycle
    if (g_currentMapId == 7 && !g_hero.IsInSafeZone()) {
      static float bubbleCycle = 0.0f;
      bubbleCycle += deltaTime;
      if (bubbleCycle >= 10.0f) bubbleCycle -= 10.0f;
      // Emit during first 1 second of each 10-second cycle
      if (bubbleCycle < 1.0f) {
        // Throttle: 1 bubble every 3 frames (~20/sec instead of 60/sec)
        static int bubbleFrame = 0;
        if (++bubbleFrame % 3 == 0) {
          glm::vec3 hp = g_hero.GetPosition();
          // Swimming walk tilts character forward — head is lower and offset forward
          glm::vec3 headPos;
          if (g_hero.IsMoving()) {
            float facing = g_hero.GetFacing();
            float fwd = 80.0f; // Forward offset toward facing direction
            headPos = hp + glm::vec3(std::sin(facing) * fwd, 150, std::cos(facing) * fwd);
          } else {
            headPos = hp + glm::vec3(0, 270, 0);
          }
          // Wider spread so bubbles aren't condensed
          glm::vec3 off((float)(rand() % 40 - 20), (float)(rand() % 30),
                        (float)(rand() % 40 - 20));
          g_vfxManager.SpawnBurst(ParticleType::BUBBLE, headPos + off, 1);
        }
      }
    }

    // Atlans: environmental bubbles from type 22 hidden objects
    // Main 5.2: very occasional bubble (~every 5 frames) at object positions
    if (g_currentMapId == 7) {
      static int envBubbleTick = 0;
      if (++envBubbleTick % 8 == 0) { // Every 8 frames
        const auto &insts = g_objectRenderer.GetInstances();
        for (const auto &inst : insts) {
          if (inst.type != 22) continue;
          glm::vec3 objPos(inst.modelMatrix[3]);
          // Distance cull — only spawn near hero
          float dx = objPos.x - g_hero.GetPosition().x;
          float dz = objPos.z - g_hero.GetPosition().z;
          if (dx * dx + dz * dz > 800.0f * 800.0f) continue;
          // Random skip: ~1 in 3 objects emit per cycle
          if (rand() % 3 != 0) continue;
          glm::vec3 off((float)(rand() % 60 - 30), (float)(rand() % 40),
                        (float)(rand() % 60 - 30));
          g_vfxManager.SpawnBurst(ParticleType::BUBBLE, objPos + off, 1);
        }
      }
    }

    // Twister proximity: apply StormTime spin when tornado VFX reaches a
    // monster
    if (g_vfxManager.HasActiveTwisters()) {
      int monCount = g_monsterManager.GetMonsterCount();
      for (int mi = 0; mi < monCount; ++mi) {
        MonsterInfo info = g_monsterManager.GetMonsterInfo(mi);
        if (info.hp <= 0)
          continue;
        if (g_vfxManager.CheckTwisterHit(info.serverIndex, info.position))
          g_monsterManager.ApplyStormTime(info.serverIndex, 10);
      }
    }

    // Evil Spirit: StormTime spin on nearby monsters (Main 5.2: same as
    // Twister)
    if (g_vfxManager.HasActiveSpiritBeams()) {
      int monCount = g_monsterManager.GetMonsterCount();
      for (int mi = 0; mi < monCount; ++mi) {
        MonsterInfo info = g_monsterManager.GetMonsterInfo(mi);
        if (info.hp <= 0)
          continue;
        if (g_vfxManager.CheckSpiritBeamHit(info.serverIndex, info.position))
          g_monsterManager.ApplyStormTime(info.serverIndex, 10);
      }
    }

    // Boids — birds in Lorencia, bats in Dungeon (BoidManager handles map
    // logic)
    g_boidManager.SetCameraView(projection * view);
    g_boidManager.Update(deltaTime, g_hero.GetPosition(), 0, currentFrame);
    g_fireEffect.Render(view, projection);
    g_objectRenderer.RenderLightningSprites(view, projection, currentFrame);
    if (g_currentMapId == 4) {
      g_objectRenderer.RenderOrbSprites(view, projection, currentFrame);
    }

    // Render ambient creatures (birds/fish/bats/leaves)
    g_boidManager.RenderShadows(view, projection);
    g_boidManager.Render(view, projection, camPos);
    if (g_mapCfg->hasLeaves)
      g_boidManager.RenderLeaves(view, projection, camPos);

    // Update NPC interaction state (guard faces player only when quest dialog
    // is open)
    g_npcManager.SetPlayerPosition(g_hero.GetPosition());
    g_npcManager.SetInteractingNpc(g_questDialogOpen ? g_questDialogNpcIndex
                                                     : -1);
    // Precompute quest markers per guard NPC type
    {
      // Collect unique guard types from quest defs
      std::vector<NpcManager::GuardMarker> markers;
      uint16_t seenGuards[20];
      int seenCount = 0;
      for (int qi = 0; qi < (int)g_questCatalog.size(); qi++) {
        uint16_t gt = g_questCatalog[qi].guardType;
        bool found = false;
        for (int s = 0; s < seenCount; s++)
          if (seenGuards[s] == gt) { found = true; break; }
        if (found) continue;
        seenGuards[seenCount++] = gt;

        // Determine best marker for this guard
        // Priority: completable '?' gold > available '!' gold > in-progress '?' grey
        // Low-level available quests (recommendedLevel < heroLevel-10) → grey '!'
        // No available/active quests → no marker
        char bestMarker = '\0';
        bool bestGold = false;
        int heroLevel = g_hero.GetLevel();
        bool hasLevelAppropriate = false; // Any quest near player's level?
        for (int qi2 = 0; qi2 < (int)g_questCatalog.size(); qi2++) {
          if (g_questCatalog[qi2].guardType != gt) continue;
          int st = GetQuestStatus(qi2);
          if (st == 2) { // completable — highest priority (always gold)
            bestMarker = '?'; bestGold = true;
            hasLevelAppropriate = true;
            break;
          } else if (st == 0 && bestMarker != '?') { // available
            bestMarker = '!';
            if ((int)g_questCatalog[qi2].recommendedLevel + 10 >= heroLevel)
              hasLevelAppropriate = true;
          } else if (st == 1 && bestMarker == '\0') { // in-progress
            bestMarker = '?'; bestGold = false;
            hasLevelAppropriate = true; // active quest is always relevant
          }
        }
        // Gold only if level-appropriate; grey for low-level available quests
        if (bestMarker == '!')
          bestGold = hasLevelAppropriate;
        if (bestMarker != '\0')
          markers.push_back({gt, bestMarker, bestGold});
      }
      g_npcManager.SetQuestMarkers(markers);
    }

    // Render NPC stencil shadows + models (skip stencil when shadow map active)
    bool hasShadowMap = g_shadowMap.depthShader && bgfx::isValid(g_shadowMap.fb);
    if (!hasShadowMap) g_npcManager.RenderShadows(view, projection);
    g_npcManager.Render(view, projection, camPos, deltaTime);

    // Render monster stencil shadows + models
    if (!hasShadowMap) g_monsterManager.RenderShadows(view, projection);
    g_monsterManager.Render(view, projection, camPos, deltaTime);

    // Silhouette outline on hovered NPC/monster (stencil-based)
    if (g_hoveredMonster >= 0)
      g_monsterManager.RenderSilhouetteOutline(g_hoveredMonster, view,
                                               projection);

    // Render ground item shadows (before hero so items don't shadow-over hero)
    GroundItemRenderer::RenderShadows(g_groundItems, MAX_GROUND_ITEMS, view,
                                      projection);

    // Render hero stencil shadow + model (skip stencil when shadow map active)
    g_clickEffect.Update(deltaTime);
    g_clickEffect.Render(view, projection, deltaTime, g_hero.GetShader());
    if (!hasShadowMap) g_hero.RenderShadow(view, projection);
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
        // Apply facing rotation in MU space (same as shadow
        // HeroCharacter.cpp:994)
        float rx = bx * cosF - by * sinF;
        float ry = bx * sinF + by * cosF;
        // Full model transform: translate * rotZ(-90) * rotY(-90) *
        // rotZ(facing) After facing rotation in MU space: (rx, ry, bz) After
        // rotY(-90): (-bz, ry, rx) After rotZ(-90): (ry, bz, rx)
        boneWorldPos[i] = heroPos + glm::vec3(ry, bz, rx);
      }
      g_vfxManager.SetHeroBonePositions(boneWorldPos);
    }

    // Feed weapon blur trail points to VFX (Main 5.2: per-frame capture)
    if (g_hero.IsWeaponTrailActive() && g_hero.HasValidTrailPoints()) {
      g_vfxManager.AddWeaponTrailPoint(g_hero.GetWeaponTrailTip(),
                                       g_hero.GetWeaponTrailBase());
    }

    // Render VFX (after all characters so particles layer on top)
    g_vfxManager.Render(view, projection);

    // Post-processing: bloom + vignette + composite (reads scene FBO → backbuffer)
    RenderPostProcess(fbW, fbH);

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
    ImGui_BackendNewFrame();

    ImGui_ImplGlfw_NewFrame();
    if (winH > 0)
      ImGui::GetIO().FontGlobalScale = (float)winH / 768.0f / g_fontPreScale;
    ImGui::NewFrame();

    // Suppress ImGui's default fallback window
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(-200, -200), ImGuiCond_Always);
    ImGui::Begin("Debug##Default", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::End();

    // Simplified ImGui HUD
    {
      // Unified bottom HUD bar (HP, QWER, 1234, RMC, AG, XP)
      ImDrawList *dl = ImGui::GetForegroundDrawList();

      InventoryUI::RenderQuickbar(dl, g_hudCoords);
      InventoryUI::RenderCastBar(dl);

      // ── FPS counter (top-left) ──
      {
        float uiS = ImGui::GetIO().FontGlobalScale;
        float fps = (smoothedDelta > 0.0f) ? 1.0f / smoothedDelta : 0.0f;
        char fpsBuf[16];
        snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS", fps);
        float fSize = 18.0f * uiS;
        float px = 8.0f * uiS, py = 6.0f * uiS;
        dl->AddText(g_fontDefault, fSize, ImVec2(px + 1, py + 1), IM_COL32(0, 0, 0, 100), fpsBuf);
        dl->AddText(g_fontDefault, fSize, ImVec2(px, py), IM_COL32(200, 200, 200, 180), fpsBuf);
      }

      // ── Map name + coordinates + time (top-right) ──
      {
        float uiS = ImGui::GetIO().FontGlobalScale;
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float fSize = 18.0f * uiS;
        glm::vec3 hPos = g_hero.GetPosition();
        int gx = (int)(hPos.z / 100.0f), gz = (int)(hPos.x / 100.0f);
        const char *mapName = GetFloorDisplayName();

        // Server time (use system local time)
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm *lt = std::localtime(&tt);
        char infoBuf[80];
        snprintf(infoBuf, sizeof(infoBuf), "%s  %d, %d  %02d:%02d",
                 mapName, gx, gz, lt->tm_hour, lt->tm_min);
        ImVec2 tsz = g_fontDefault->CalcTextSizeA(fSize, FLT_MAX, 0, infoBuf);
        float ix = disp.x - tsz.x - 10.0f * uiS;
        float py = 6.0f * uiS;
        dl->AddText(g_fontDefault, fSize, ImVec2(ix + 1, py + 1), IM_COL32(0, 0, 0, 100), infoBuf);
        dl->AddText(g_fontDefault, fSize, ImVec2(ix, py), IM_COL32(210, 200, 170, 200), infoBuf);
      }

      // ── Summon unit frame (WoW-style, left of HP orb) ──
      {
        MonsterInfo sumInfo;
        if (g_monsterManager.GetOwnSummonInfo(sumInfo)) {
          // HP orb screen position via UICoords (matches InventoryUISkills.cpp)
          constexpr float HP_ORB_VX = 339.0f;  // HudLayout::HP_ORB_CX
          constexpr float HP_ORB_VY = 772.0f;  // HudLayout::ORB_CY
          constexpr float HP_ORB_VR = 56.0f;   // HudLayout::ORB_RADIUS

          float orbSX = g_hudCoords.ToScreenX(HP_ORB_VX);
          float orbSY = g_hudCoords.ToScreenY(HP_ORB_VY);
          float orbSR = g_hudCoords.ToScreenX(HP_ORB_VX + HP_ORB_VR) - orbSX;

          // Frame in virtual coords, converted to screen via UICoords
          constexpr float FRAME_VW = 160.0f;
          constexpr float FRAME_VH = 46.0f;
          constexpr float FRAME_VGAP = 8.0f;

          float frameW = g_hudCoords.ToScreenX(FRAME_VW) - g_hudCoords.ToScreenX(0.0f);
          float frameH = g_hudCoords.ToScreenY(FRAME_VH) - g_hudCoords.ToScreenY(0.0f);
          float gapSc  = g_hudCoords.ToScreenX(FRAME_VGAP) - g_hudCoords.ToScreenX(0.0f);
          float fx = orbSX - orbSR - gapSc - frameW;
          float fy = orbSY - frameH * 0.5f;

          // Dark background with subtle gradient
          dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH),
                            IM_COL32(10, 10, 12, 210), 3.0f);
          // Inner shadow (top/left darker)
          dl->AddRectFilledMultiColor(
              ImVec2(fx, fy), ImVec2(fx + frameW, fy + 2),
              IM_COL32(0, 0, 0, 80), IM_COL32(0, 0, 0, 80),
              IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));

          ImFont *font = g_fontDefault ? g_fontDefault : ImGui::GetFont();
          float fs = font->LegacySize * ImGui::GetIO().FontGlobalScale;
          float pad = 4.0f;

          // Name (top half)
          char nameBuf[64];
          snprintf(nameBuf, sizeof(nameBuf), "%s Lv.%d", sumInfo.name.c_str(), sumInfo.level);
          dl->AddText(font, fs, ImVec2(fx + pad + 1, fy + 2 + 1), IM_COL32(0, 0, 0, 180), nameBuf);
          dl->AddText(font, fs, ImVec2(fx + pad, fy + 2), IM_COL32(180, 220, 160, 240), nameBuf);

          // HP bar (bottom half)
          float barX = fx + pad;
          float barY = fy + frameH * 0.52f;
          float barW = frameW - pad * 2;
          float barH = frameH * 0.34f;
          // Bar background
          dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                            IM_COL32(20, 20, 20, 220), 2.0f);
          // HP fill
          float hpFrac = (sumInfo.maxHp > 0)
              ? std::clamp((float)sumInfo.hp / (float)sumInfo.maxHp, 0.0f, 1.0f) : 0.0f;
          if (hpFrac > 0.01f) {
            // WoW-style green gradient
            ImU32 barTop = (hpFrac > 0.5f) ? IM_COL32(40, 180, 40, 230)
                         : (hpFrac > 0.25f) ? IM_COL32(200, 180, 30, 230)
                                            : IM_COL32(200, 40, 40, 230);
            ImU32 barBot = (hpFrac > 0.5f) ? IM_COL32(20, 120, 20, 230)
                         : (hpFrac > 0.25f) ? IM_COL32(140, 120, 10, 230)
                                            : IM_COL32(140, 20, 20, 230);
            dl->AddRectFilledMultiColor(
                ImVec2(barX + 1, barY + 1),
                ImVec2(barX + 1 + (barW - 2) * hpFrac, barY + barH - 1),
                barTop, barTop, barBot, barBot);
          }

          // Frame border (dark with subtle highlight)
          dl->AddRect(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH),
                      IM_COL32(40, 36, 28, 200), 3.0f);
          dl->AddRect(ImVec2(fx + 1, fy + 1), ImVec2(fx + frameW - 1, fy + frameH - 1),
                      IM_COL32(60, 55, 40, 120), 2.0f);
        }
      }

      // ── Floating damage numbers ──
      FloatingDamageRenderer::UpdateAndRender(
          g_floatingDmg, MAX_FLOATING_DAMAGE, deltaTime, dl, g_fontBold,
          view, projection, winW, winH);

      // ── System message log ──
      SystemMessageLog::Update(deltaTime);
      {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        SystemMessageLog::Render(dl, g_fontDefault, (float)winW, (float)winH,
                                 70.0f, (float)mx, (float)my);
      }

      // ── Monster nameplates ──
      g_monsterManager.RenderNameplates(
          dl, g_fontDefault, view, projection, winW, winH, camPos,
          g_hoveredMonster, g_hero.GetAttackTarget(), g_serverLevel);

      // ── Ground item 3D models + physics ──
      GroundItemRenderer::RenderModels(
          g_groundItems, MAX_GROUND_ITEMS, deltaTime, view, projection,
          [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });

      // Ground item sparkle: Main 5.2 BITMAP_FLARE at enhanced item drops
      {
        static std::vector<glm::vec3> sparklePos;
        sparklePos.clear();
        GroundItemRenderer::UpdateSparkleTimers(
            g_groundItems, MAX_GROUND_ITEMS, deltaTime, sparklePos);
        for (auto &sp : sparklePos)
          g_vfxManager.SpawnBurst(ParticleType::FLARE, sp, 1);
      }

      // ── Ground item labels + tooltips ──
      GroundItemRenderer::RenderLabels(
          g_groundItems, MAX_GROUND_ITEMS, dl, g_fontDefault, view, projection,
          winW, winH, camPos, g_hoveredGroundItem, g_itemDefs);
    }

    // NPC name labels
    g_npcManager.RenderLabels(ImGui::GetForegroundDrawList(), view, projection,
                              winW, winH, camPos, g_hoveredNpc);

    // Helper: close quest dialog and notify server
    auto closeQuestDialog = [&]() {
      if (g_questDialogOpen && g_questDialogNpcIndex >= 0 &&
          g_questDialogNpcIndex < g_npcManager.GetNpcCount()) {
        NpcInfo qi = g_npcManager.GetNpcInfo(g_questDialogNpcIndex);
        g_server.SendNpcInteract(qi.type, false);
      }
      g_questDialogOpen = false;
      g_questDialogNpcIndex = -1;
      g_questDialogSelected = -1;
      g_selectedNpc = -1;
    };

    // NPC click interaction dialog has been replaced with direct shop opening
    // through InputHandler.cpp (SendShopOpen). Optionally we could keep
    // g_selectedNpc for highlighting purposes without rendering a dialog.
    if (g_selectedNpc >= 0) {
      // Close selection on Escape
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        closeQuestDialog();
        g_selectedNpc = -1;
      }
    }

    // ── Quest Dialog (guard NPC overlay — per-NPC quest list) ──
    g_questDialogJustOpened = false;
    if (g_questDialogOpen && !g_questDialogWasOpen) {
      g_questDialogSelected = -1;
      g_questDialogJustOpened = true;
      g_questListScrollY = 0.0f;
      SoundManager::Play(SOUND_INTERFACE01);
    }
    g_questDialogWasOpen = g_questDialogOpen;

    if (g_questDialogOpen && g_questDialogNpcIndex >= 0 &&
        g_questDialogNpcIndex < g_npcManager.GetNpcCount()) {
      NpcInfo npcInfo = g_npcManager.GetNpcInfo(g_questDialogNpcIndex);

      // Close if player walks too far away
      float dist = glm::distance(g_hero.GetPosition(), npcInfo.position);
      if (dist > 350.0f) {
        closeQuestDialog();
      } else {
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 dispSize = ImGui::GetIO().DisplaySize;
        ImVec2 mousePos = ImGui::GetIO().MousePos;

        uint16_t guardType = npcInfo.type;
        int classIdx = g_hero.GetClass() / 16; // DW=0, DK=1, ELF=2, MG=3

        // WoW-style colors
        ImU32 cBg = IM_COL32(12, 10, 8, 235);
        ImU32 cBorder = IM_COL32(80, 70, 50, 180);
        ImU32 cTitle = IM_COL32(255, 210, 80, 255);
        ImU32 cText = IM_COL32(200, 195, 180, 255);
        ImU32 cTextDim = IM_COL32(140, 135, 120, 255);
        ImU32 cGold = IM_COL32(255, 210, 50, 255);
        ImU32 cGreen = IM_COL32(80, 220, 80, 255);
        ImU32 cSep = IM_COL32(50, 45, 35, 120);

        auto drawButton = [&](float bx, float by, float bw, float bh,
                              const char *label, ImU32 textColor) -> bool {
          ImVec2 bMin2(bx, by), bMax2(bx + bw, by + bh);
          bool hov = mousePos.x >= bMin2.x && mousePos.x <= bMax2.x &&
                     mousePos.y >= bMin2.y && mousePos.y <= bMax2.y;
          // Gradient fill: darker bottom → lighter top
          ImU32 topCol = hov ? IM_COL32(55, 50, 35, 235) : IM_COL32(35, 30, 22, 230);
          ImU32 botCol = hov ? IM_COL32(35, 30, 22, 235) : IM_COL32(18, 16, 12, 230);
          dl->AddRectFilledMultiColor(bMin2, bMax2, topCol, topCol, botCol, botCol);
          // Gold border (brighter on hover)
          dl->AddRect(bMin2, bMax2,
                      hov ? IM_COL32(140, 120, 70, 220) : cBorder,
                      3.0f, 0, 1.0f);
          // Top highlight line
          dl->AddLine(ImVec2(bx + 2, by + 1), ImVec2(bx + bw - 2, by + 1),
                      IM_COL32(255, 255, 255, hov ? (uint8_t)30 : (uint8_t)15));
          ImVec2 ls = ImGui::CalcTextSize(label);
          DrawShadowText(dl, ImVec2(bx + (bw - ls.x) * 0.5f,
                         by + (bh - ls.y) * 0.5f), textColor, label);
          return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                 !g_questDialogJustOpened;
        };

        float qs = ImGui::GetIO().DisplaySize.y / 768.0f; // quest UI scale
        float btnW = 80.0f * qs, btnH = 26.0f * qs;
        float panelW = 340.0f * qs;
        const char *npcName = npcInfo.name.c_str();

        // Collect all quests belonging to this guard, sorted by level (low→high)
        if (g_questCatalog.empty()) { closeQuestDialog(); }
        else {
        int npcQuests[(int)g_questCatalog.size()];
        int npcQuestCount = 0;
        for (int qi = 0; qi < (int)g_questCatalog.size(); qi++)
          if (g_questCatalog[qi].guardType == guardType && !IsQuestCompleted(qi))
            npcQuests[npcQuestCount++] = qi;
        std::sort(npcQuests, npcQuests + npcQuestCount, [](int a, int b) {
          return g_questCatalog[a].recommendedLevel < g_questCatalog[b].recommendedLevel;
        });

        if (g_questDialogSelected >= 0 && g_questDialogSelected < (int)g_questCatalog.size()) {
          // ── QUEST DETAIL PANEL ──
          int qi = g_questDialogSelected;
          const auto &q = g_questCatalog[qi];
          int st = GetQuestStatus(qi);
          auto *aq = FindActiveQuest(qi);

          const char *dialogText = q.loreText.c_str();
          if (st == 2) dialogText = "Well done! You've completed\nthe task. Here is your reward.";
          else if (st == 3) dialogText = "This task is already complete.";

          bool showObjectives = (st == 0 || st == 1 || st == 2);
          bool showRewards = (st == 0 || st == 1 || st == 2);
          bool completable = aq && IsQuestCompletable(*aq);
          bool showAcceptBtn = (st == 0);
          bool showCompleteBtn = (st == 2);

          // Count class-specific item rewards
          int itemCount = 0;
          if (showRewards) {
            if (q.classReward[classIdx][0].defIndex >= 0) itemCount++;
            if (q.classReward[classIdx][1].defIndex >= 0) itemCount++;
          }

          float textWrap = panelW - 40 * qs; // lore text wrap width
          ImVec2 dialogSize = ImGui::CalcTextSize(dialogText, nullptr, false, textWrap);
          float subtitleH = ImGui::CalcTextSize(npcName).y + 4 * qs;
          float objH = showObjectives ? (18 * qs + q.targetCount * 26.0f * qs) : 0;
          float weaponH = itemCount > 0 ? (itemCount * 48.0f * qs + 4.0f * qs) : 0;
          float rewardsH = showRewards ? ((10 + 18 + 18 + 18) * qs + weaponH) : 0;
          float buttonsH = btnH + 16 * qs;
          float infoH = ImGui::GetFontSize() + 8 * qs;
          float hintH = (st == 1 && completable) ? 24.0f * qs : 0.0f;
          float panelH = 14 * qs + ImGui::CalcTextSize(q.questName.c_str()).y + 6 * qs +
                         subtitleH + infoH + 12 * qs +
                         dialogSize.y + 10 * qs + objH + rewardsH + hintH + buttonsH + 14 * qs;
          float px = (dispSize.x - panelW) * 0.5f;
          float py = (dispSize.y - panelH) * 0.5f;
          g_qdPanelRect[0] = px; g_qdPanelRect[1] = py;
          g_qdPanelRect[2] = panelW; g_qdPanelRect[3] = panelH;

          dl->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                            cBg, 5.0f);
          dl->AddRect(ImVec2(px + 1, py + 1),
                      ImVec2(px + panelW - 1, py + panelH - 1),
                      IM_COL32(40, 35, 25, 100), 4.0f, 0, 1.0f);
          dl->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                      cBorder, 5.0f, 0, 1.5f);

          float contentY = py + 14 * qs;
          float margin = 20 * qs;
          float sepInset = 16 * qs;
          // Title with shadow
          {
            ImVec2 ts = ImGui::CalcTextSize(q.questName.c_str());
            DrawShadowText(dl, ImVec2(px + (panelW - ts.x) * 0.5f, contentY),
                           cTitle, q.questName.c_str());
            contentY += ts.y + 6 * qs;
          }
          // Guard name subtitle
          {
            ImVec2 gs = ImGui::CalcTextSize(npcName);
            dl->AddText(ImVec2(px + (panelW - gs.x) * 0.5f, contentY),
                        cTextDim, npcName);
            contentY += gs.y + 4 * qs;
          }
          // Location + level (difficulty colored)
          {
            char infoBuf[64];
            snprintf(infoBuf, sizeof(infoBuf), "%s  |  Lv. %d",
                     q.location.c_str(), (int)q.recommendedLevel);
            ImVec2 infoSz = ImGui::CalcTextSize(infoBuf);
            ImU32 lvColor = GetQuestDifficultyColor(q.recommendedLevel);
            dl->AddText(ImVec2(px + (panelW - infoSz.x) * 0.5f, contentY),
                        lvColor, infoBuf);
            contentY += infoSz.y + 4 * qs;
          }
          DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);
          contentY += 12 * qs;
          // Dialog text (wrapped)
          dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                      ImVec2(px + margin, contentY), cText,
                      dialogText, nullptr, textWrap);
          contentY += dialogSize.y + 10 * qs;
          // Objectives
          if (showObjectives) {
            DrawShadowText(dl, ImVec2(px + margin, contentY),
                           IM_COL32(210, 180, 80, 255), "Objectives");
            contentY += 18 * qs;
            float barW = panelW - 60 * qs;
            for (int i = 0; i < q.targetCount; i++) {
              char objBuf[80];
              if (aq) {
                snprintf(objBuf, sizeof(objBuf), "  %s  %d / %d",
                         q.targets[i].name.c_str(), aq->killCount[i],
                         (int)q.targets[i].killsReq);
                bool done = aq->killCount[i] >= q.targets[i].killsReq;
                DrawShadowText(dl, ImVec2(px + 22 * qs, contentY),
                               done ? cGreen : cText, objBuf);
                contentY += 18 * qs;
                float frac = (float)aq->killCount[i] / (float)q.targets[i].killsReq;
                DrawProgressBar(dl, px + 24 * qs, contentY, barW, 4.0f * qs, frac);
                contentY += 8 * qs;
              } else {
                snprintf(objBuf, sizeof(objBuf), "  Slay %d %s",
                         q.targets[i].killsReq, q.targets[i].name.c_str());
                dl->AddText(ImVec2(px + 22 * qs, contentY), cText, objBuf);
                contentY += 18 * qs;
                DrawProgressBar(dl, px + 24 * qs, contentY, barW, 4.0f * qs, 0.0f);
                contentY += 8 * qs;
              }
            }
          }
          // Rewards (only your class items)
          if (showRewards) {
            contentY += 4 * qs;
            DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);
            contentY += 8 * qs;
            DrawShadowText(dl, ImVec2(px + margin, contentY),
                           IM_COL32(210, 180, 80, 255), "Rewards");
            contentY += 18 * qs;
            // Zen
            {
              std::string zenStr = std::to_string(q.zenReward);
              int n = (int)zenStr.length() - 3;
              while (n > 0) { zenStr.insert(n, ","); n -= 3; }
              char rwBuf[64];
              snprintf(rwBuf, sizeof(rwBuf), "  %s Zen", zenStr.c_str());
              DrawShadowText(dl, ImVec2(px + 22 * qs, contentY), cGold, rwBuf);
              contentY += 18 * qs;
            }
            // XP
            {
              std::string xpStr = std::to_string(q.xpReward);
              int n = (int)xpStr.length() - 3;
              while (n > 0) { xpStr.insert(n, ","); n -= 3; }
              char rwBuf[64];
              snprintf(rwBuf, sizeof(rwBuf), "  %s Experience", xpStr.c_str());
              DrawShadowText(dl, ImVec2(px + 22 * qs, contentY),
                             IM_COL32(180, 140, 255, 255), rwBuf);
              contentY += 18 * qs;
            }
            // Class-specific item rewards
            contentY += 4 * qs;
            float h1 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                           q.classReward[classIdx][0], qs);
            contentY += h1;
            float h2 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                           q.classReward[classIdx][1], qs);
            contentY += h2;
          }
          // Completable hint for in-progress quests
          if (st == 1 && completable) {
            contentY += 6 * qs;
            dl->AddText(ImVec2(px + margin, contentY), cGold,
                        "Return to complete this quest.");
            contentY += 18 * qs;
          }
          // Separator before buttons
          contentY += 2 * qs;
          DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);
          // Buttons
          float btnY = py + panelH - btnH - 12.0f * qs;
          if (showAcceptBtn) {
            float gap = 16.0f;
            float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
            float bx2 = bx1 + btnW + gap;
            if (drawButton(bx1, btnY, btnW, btnH, "Accept", cGreen)) {
              g_server.SendQuestAccept((uint8_t)qi);
              SoundManager::Play3D(SOUND_QUEST_ACCEPT, npcInfo.position.x,
                                   npcInfo.position.y, npcInfo.position.z);
              closeQuestDialog();
            }
            if (drawButton(bx2, btnY, btnW, btnH, "Back", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              g_questDialogSelected = -1;
            }
          } else if (showCompleteBtn) {
            float gap = 16.0f;
            float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
            float bx2 = bx1 + btnW + gap;
            if (drawButton(bx1, btnY, btnW, btnH, "Complete", cGold)) {
              SoundManager::Play3D(SOUND_QUEST_ACCEPT, npcInfo.position.x,
                                   npcInfo.position.y, npcInfo.position.z);
              g_server.SendQuestComplete((uint8_t)qi);
              closeQuestDialog();
            }
            if (drawButton(bx2, btnY, btnW, btnH, "Back", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              g_questDialogSelected = -1;
            }
          } else if (st == 1) {
            // In-progress: Back + Abandon
            float gap = 16.0f;
            float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
            float bx2 = bx1 + btnW + gap;
            if (drawButton(bx1, btnY, btnW, btnH, "Back", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              g_questDialogSelected = -1;
            }
            if (drawButton(bx2, btnY, btnW, btnH, "Abandon",
                           IM_COL32(220, 80, 80, 255))) {
              SoundManager::Play(SOUND_CLICK01);
              g_server.SendQuestAbandon((uint8_t)qi);
              g_questDialogSelected = -1;
            }
          } else {
            if (drawButton(px + (panelW - btnW) * 0.5f, btnY, btnW, btnH,
                           "Back", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              g_questDialogSelected = -1;
            }
          }
        } else {
          // ── QUEST LIST PANEL (all quests for this NPC) ──
          if (npcQuestCount == 0) {
            // No quests — simple dialog
            float panelH = 100.0f * qs;
            float px = (dispSize.x - panelW) * 0.5f;
            float py = (dispSize.y - panelH) * 0.5f;
            g_qdPanelRect[0] = px; g_qdPanelRect[1] = py;
            g_qdPanelRect[2] = panelW; g_qdPanelRect[3] = panelH;
            dl->AddRectFilled(ImVec2(px, py),
                              ImVec2(px + panelW, py + panelH), cBg, 5.0f);
            dl->AddRect(ImVec2(px, py),
                        ImVec2(px + panelW, py + panelH), cBorder, 5.0f, 0, 1.5f);
            ImVec2 ts = ImGui::CalcTextSize(npcName);
            dl->AddText(ImVec2(px + (panelW - ts.x) * 0.5f, py + 14 * qs),
                        cTitle, npcName);
            const char *msg = "I have no tasks for you.";
            ImVec2 ms = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(px + (panelW - ms.x) * 0.5f, py + 42 * qs),
                        cText, msg);
            float btnY = py + panelH - btnH - 12.0f * qs;
            if (drawButton(px + (panelW - btnW) * 0.5f, btnY, btnW, btnH,
                           "Close", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              closeQuestDialog();
            }
          } else {
            float rowH = 44.0f * qs;
            float totalListH = npcQuestCount * rowH + 8 * qs;
            constexpr int MAX_VISIBLE_QUESTS = 5;
            float maxListH = MAX_VISIBLE_QUESTS * rowH + 8 * qs;
            float visListH = std::min(totalListH, maxListH);
            bool needsScroll = totalListH > maxListH;
            float panelH = 44 * qs + visListH + btnH + 20 * qs;
            float px = (dispSize.x - panelW) * 0.5f;
            float py = (dispSize.y - panelH) * 0.5f;
            g_qdPanelRect[0] = px; g_qdPanelRect[1] = py;
            g_qdPanelRect[2] = panelW; g_qdPanelRect[3] = panelH;

            dl->AddRectFilled(ImVec2(px, py),
                              ImVec2(px + panelW, py + panelH), cBg, 5.0f);
            dl->AddRect(ImVec2(px + 1, py + 1),
                        ImVec2(px + panelW - 1, py + panelH - 1),
                        IM_COL32(40, 35, 25, 100), 4.0f, 0, 1.0f);
            dl->AddRect(ImVec2(px, py),
                        ImVec2(px + panelW, py + panelH), cBorder, 5.0f, 0, 1.5f);

            float contentY = py + 14 * qs;
            {
              ImVec2 ts = ImGui::CalcTextSize(npcName);
              DrawShadowText(dl, ImVec2(px + (panelW - ts.x) * 0.5f, contentY),
                             cTitle, npcName);
              contentY += ts.y + 6 * qs;
            }
            DrawOrnamentSep(dl, px + 16 * qs, px + panelW - 16 * qs, contentY);
            contentY += 8 * qs;

            // Scroll: handle mouse wheel over list area
            float listTop = contentY;
            if (needsScroll) {
              float maxScroll = totalListH - maxListH;
              bool mouseInList = mousePos.x >= px && mousePos.x <= px + panelW &&
                                 mousePos.y >= listTop && mousePos.y <= listTop + visListH;
              if (mouseInList) {
                g_questListScrollY -= ImGui::GetIO().MouseWheel * rowH;
              }
              g_questListScrollY = glm::clamp(g_questListScrollY, 0.0f, maxScroll);
            }

            // Clip quest rows to visible list area
            dl->PushClipRect(ImVec2(px, listTop), ImVec2(px + panelW, listTop + visListH), true);

            for (int a = 0; a < npcQuestCount; a++) {
              int qi = npcQuests[a];
              const auto &q = g_questCatalog[qi];
              int st = GetQuestStatus(qi);

              float rowY = contentY - g_questListScrollY;
              float rowX = px + 10 * qs;
              float rowW = panelW - 20 * qs - (needsScroll ? 8 * qs : 0);
              // Skip rows entirely outside visible area
              if (rowY + rowH < listTop || rowY > listTop + visListH) {
                contentY += rowH;
                continue;
              }
              ImVec2 rMin(rowX, rowY), rMax(rowX + rowW, rowY + rowH);
              bool hov = mousePos.x >= rMin.x && mousePos.x <= rMax.x &&
                         mousePos.y >= std::max(rMin.y, listTop) &&
                         mousePos.y <= std::min(rMax.y, listTop + visListH);

              // Alternating row tint + hover highlight
              if (a % 2 == 1)
                dl->AddRectFilled(rMin, rMax, IM_COL32(25, 22, 18, 80), 2.0f);
              if (hov)
                dl->AddRectFilled(rMin, rMax, IM_COL32(50, 44, 30, 180), 2.0f);

              // Left accent stripe by status
              ImU32 accentCol;
              if (st == 0) accentCol = cGold;
              else if (st == 2) accentCol = cGreen;
              else if (st == 1) accentCol = IM_COL32(140, 135, 120, 160);
              else accentCol = IM_COL32(80, 80, 80, 120);
              dl->AddRectFilled(ImVec2(rowX, rowY + 2 * qs),
                                ImVec2(rowX + 3 * qs, rowY + rowH - 2 * qs), accentCol);

              // Status icon and colors
              const char *icon;
              ImU32 iconCol;
              if (st == 0) { icon = "!"; iconCol = cGold; }
              else if (st == 1) { icon = "?"; iconCol = cTextDim; }
              else if (st == 2) { icon = "?"; iconCol = cGold; }
              else { icon = " "; iconCol = cTextDim; }

              // Difficulty-colored quest name
              ImU32 nameCol = (st == 3) ? IM_COL32(100, 100, 100, 180)
                                        : GetQuestDifficultyColor(q.recommendedLevel);

              float fontSize = ImGui::GetFontSize();
              float textY = rowY + (rowH - fontSize * 2 - 2 * qs) * 0.5f;
              DrawShadowText(dl, ImVec2(rowX + 8 * qs, textY), iconCol, icon);
              DrawShadowText(dl, ImVec2(rowX + 26 * qs, textY), nameCol,
                             q.questName.c_str());
              // Location + level
              {
                char locBuf[48];
                snprintf(locBuf, sizeof(locBuf), "%s  Lv. %d",
                         q.location.c_str(), (int)q.recommendedLevel);
                dl->AddText(ImVec2(rowX + 26 * qs, textY + fontSize + 2 * qs),
                            cTextDim, locBuf);
              }
              // Status text on right
              if (st == 2) {
                const char *done = "Complete";
                ImVec2 dSz = ImGui::CalcTextSize(done);
                DrawShadowText(dl, ImVec2(rowX + rowW - dSz.x - 6 * qs, textY),
                               cGold, done);
              } else if (st == 1) {
                auto *aqp = FindActiveQuest(qi);
                if (aqp) {
                  for (int i = 0; i < q.targetCount; i++) {
                    if (aqp->killCount[i] < q.targets[i].killsReq) {
                      char prog[16];
                      snprintf(prog, sizeof(prog), "%d/%d",
                               aqp->killCount[i], (int)q.targets[i].killsReq);
                      ImVec2 pSz = ImGui::CalcTextSize(prog);
                      dl->AddText(ImVec2(rowX + rowW - pSz.x - 6 * qs, textY),
                                  cTextDim, prog);
                      break;
                    }
                  }
                }
              } else if (st == 3) {
                const char *cmpl = "Done";
                ImVec2 cSz2 = ImGui::CalcTextSize(cmpl);
                dl->AddText(ImVec2(rowX + rowW - cSz2.x - 6 * qs, textY),
                            IM_COL32(100, 100, 100, 180), cmpl);
              }

              dl->AddLine(ImVec2(rowX, rowY + rowH - 1),
                          ImVec2(rowX + rowW, rowY + rowH - 1), cSep);

              // Click to select (not completed quests)
              if (hov && st != 3 &&
                  ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                  !g_questDialogJustOpened) {
                SoundManager::Play(SOUND_CLICK01);
                g_questDialogSelected = qi;
              }

              contentY += rowH;
            }

            dl->PopClipRect();

            // Scrollbar track + thumb
            if (needsScroll) {
              float maxScroll = totalListH - maxListH;
              float trackX = px + panelW - 10 * qs;
              float trackW = 4 * qs;
              float trackTop = listTop;
              float trackH = visListH;
              dl->AddRectFilled(ImVec2(trackX, trackTop),
                                ImVec2(trackX + trackW, trackTop + trackH),
                                IM_COL32(30, 25, 18, 120), 2.0f);
              float thumbRatio = visListH / totalListH;
              float thumbH = std::max(thumbRatio * trackH, 16.0f * qs);
              float thumbY = trackTop + (g_questListScrollY / maxScroll) * (trackH - thumbH);
              dl->AddRectFilled(ImVec2(trackX, thumbY),
                                ImVec2(trackX + trackW, thumbY + thumbH),
                                IM_COL32(180, 160, 110, 180), 2.0f);
            }

            // Close button
            float btnY = py + panelH - btnH - 12.0f * qs;
            if (drawButton(px + (panelW - btnW) * 0.5f, btnY, btnW, btnH,
                           "Close", cTextDim)) {
              SoundManager::Play(SOUND_CLICK01);
              closeQuestDialog();
            }
          }
        }
      }
      }
    }

    // ── Quest Log (L key) — WoW-style compact tracker, right side ──
    if (g_showQuestLog) {
      ImDrawList *ql = ImGui::GetForegroundDrawList();
      ImVec2 dispSz = ImGui::GetIO().DisplaySize;
      ImVec2 mPos = ImGui::GetIO().MousePos;
      float qs = dispSz.y / 768.0f;

      ImU32 qlBg = IM_COL32(10, 8, 5, 160);
      ImU32 qlBorder = IM_COL32(80, 70, 50, 100);
      ImU32 qlTitle = IM_COL32(255, 210, 80, 255);
      ImU32 qlText = IM_COL32(200, 195, 180, 230);
      ImU32 qlDim = IM_COL32(140, 135, 120, 200);
      ImU32 qlGold = IM_COL32(255, 210, 50, 255);
      ImU32 qlGreen = IM_COL32(80, 220, 80, 230);

      float panelW = 230.0f * qs;
      float pad = 8.0f * qs;
      float lineH = 15.0f * qs;
      float questGap = 6.0f * qs;
      float px = dispSz.x - panelW - 12.0f * qs;

      int activeCount = (int)g_activeQuests.size();

      if (activeCount == 0) {
        // Empty state
        float panelH = 50.0f * qs;
        float py = 50.0f * qs;
        g_qlPanelRect[0] = px; g_qlPanelRect[1] = py;
        g_qlPanelRect[2] = panelW; g_qlPanelRect[3] = panelH;
        ql->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                          qlBg, 3.0f);
        ql->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                    qlBorder, 3.0f, 0, 1.0f);
        float cy = py + pad;
        DrawShadowText(ql, ImVec2(px + pad, cy), qlTitle, "Quests");
        cy += lineH + 2 * qs;
        DrawOrnamentSep(ql, px + pad, px + panelW - pad, cy);
        cy += 6 * qs;
        ql->AddText(ImVec2(px + pad, cy), qlDim, "No active quests.");
      } else {
        // Sort active quests by recommended level (low→high)
        std::vector<int> sortedAQ(activeCount);
        for (int i = 0; i < activeCount; i++) sortedAQ[i] = i;
        std::sort(sortedAQ.begin(), sortedAQ.end(), [](int a, int b) {
          int qa = g_activeQuests[a].questId, qb = g_activeQuests[b].questId;
          uint8_t la = (qa >= 0 && qa < (int)g_questCatalog.size()) ? g_questCatalog[qa].recommendedLevel : 0;
          uint8_t lb = (qb >= 0 && qb < (int)g_questCatalog.size()) ? g_questCatalog[qb].recommendedLevel : 0;
          return la < lb;
        });

        // Measure total content height
        float contentH = lineH + 4 * qs; // "Quests" header + ornament
        for (int si = 0; si < activeCount; si++) {
          auto &aq = g_activeQuests[sortedAQ[si]];
          if (aq.questId < 0 || aq.questId >= (int)g_questCatalog.size()) continue;
          const auto &qd = g_questCatalog[aq.questId];
          contentH += lineH + 2 * qs; // Quest name line
          for (int i = 0; i < qd.targetCount; i++) {
            contentH += lineH; // Objective line
          }
          bool allDone = IsQuestCompletable(aq);
          if (allDone) contentH += lineH; // "Return to NPC" line
          contentH += questGap; // Gap between quests
        }

        float maxH = dispSz.y * 0.6f; // Max 60% of screen
        float visH = std::min(contentH, maxH);
        bool needsScroll = contentH > maxH;
        float panelH = visH + pad * 2;
        float py = 50.0f * qs;
        g_qlPanelRect[0] = px; g_qlPanelRect[1] = py;
        g_qlPanelRect[2] = panelW; g_qlPanelRect[3] = panelH;

        // Background
        ql->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                          qlBg, 3.0f);
        ql->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                    qlBorder, 3.0f, 0, 1.0f);

        // Scroll handling
        static float qlScrollY = 0.0f;
        if (needsScroll) {
          float maxScroll = contentH - visH;
          bool mouseInPanel = mPos.x >= px && mPos.x <= px + panelW &&
                              mPos.y >= py && mPos.y <= py + panelH;
          if (mouseInPanel)
            qlScrollY -= ImGui::GetIO().MouseWheel * lineH * 3.0f;
          qlScrollY = glm::clamp(qlScrollY, 0.0f, maxScroll);
        } else {
          qlScrollY = 0.0f;
        }

        // Clip content
        ql->PushClipRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH), true);

        float cy = py + pad - qlScrollY;

        // "Quests" header
        DrawShadowText(ql, ImVec2(px + pad, cy), qlTitle, "Quests");
        cy += lineH;
        DrawOrnamentSep(ql, px + pad, px + panelW - pad, cy);
        cy += 4 * qs;

        // Quest entries
        int questNum = 0;
        for (int si = 0; si < activeCount; si++) {
          auto &aq = g_activeQuests[sortedAQ[si]];
          if (aq.questId < 0 || aq.questId >= (int)g_questCatalog.size()) continue;
          const auto &qd = g_questCatalog[aq.questId];
          bool allDone = IsQuestCompletable(aq);
          questNum++;

          // Calculate this entry's total height for hover region
          float entryH = lineH + 2 * qs; // name line
          if (!allDone) entryH += qd.targetCount * lineH; // objective lines (hidden when done)
          if (allDone) entryH += lineH; // "Return to" hint

          // Hover detection for the whole quest entry
          float entryTop = cy;
          float entryLeft = px + 2;
          float entryRight = px + panelW - 2;
          bool entryHov = mPos.x >= entryLeft && mPos.x <= entryRight &&
                          mPos.y >= std::max(entryTop, py) &&
                          mPos.y <= std::min(entryTop + entryH, py + panelH);

          // Completed quest: golden glow behind the entry
          if (allDone) {
            float pulse = 0.5f + 0.5f * sinf((float)glfwGetTime() * 2.0f);
            int glowA = (int)(30 + 25 * pulse);
            ql->AddRectFilled(ImVec2(entryLeft, entryTop),
                              ImVec2(entryRight, entryTop + entryH),
                              IM_COL32(255, 200, 50, glowA), 3.0f);
            ql->AddRect(ImVec2(entryLeft, entryTop),
                        ImVec2(entryRight, entryTop + entryH),
                        IM_COL32(255, 210, 50, 40 + (int)(20 * pulse)), 3.0f);
          }

          // Hover highlight background
          if (entryHov)
            ql->AddRectFilled(ImVec2(entryLeft, entryTop),
                              ImVec2(entryRight, entryTop + entryH),
                              IM_COL32(50, 44, 30, 100), 2.0f);

          // Click to open detail view
          if (entryHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            SoundManager::Play(SOUND_CLICK01);
            g_questLogSelected = aq.questId;
          }

          float badgeR = 7.0f * qs;
          float badgeCx = px + pad + badgeR;
          float badgeCy = cy + lineH * 0.5f;

          if (allDone) {
            // Completed: golden checkmark badge
            ql->AddCircleFilled(ImVec2(badgeCx, badgeCy), badgeR,
                                IM_COL32(60, 50, 20, 220));
            ql->AddCircle(ImVec2(badgeCx, badgeCy), badgeR, qlGold, 0, 1.5f);
            // Draw checkmark
            float cs = badgeR * 0.55f;
            ImVec2 p1(badgeCx - cs * 0.6f, badgeCy + cs * 0.1f);
            ImVec2 p2(badgeCx - cs * 0.05f, badgeCy + cs * 0.55f);
            ImVec2 p3(badgeCx + cs * 0.7f, badgeCy - cs * 0.5f);
            ql->AddLine(p1, p2, qlGold, 1.5f * qs);
            ql->AddLine(p2, p3, qlGold, 1.5f * qs);
          } else {
            // In-progress: numbered badge
            ImU32 nameCol = GetQuestDifficultyColor(qd.recommendedLevel);
            char numBuf[4];
            snprintf(numBuf, sizeof(numBuf), "%d", questNum);
            ql->AddCircleFilled(ImVec2(badgeCx, badgeCy), badgeR,
                                IM_COL32(40, 35, 25, 200));
            ql->AddCircle(ImVec2(badgeCx, badgeCy), badgeR, nameCol, 0, 1.0f);
            ImVec2 numSz = ImGui::CalcTextSize(numBuf);
            ql->AddText(ImVec2(badgeCx - numSz.x * 0.5f,
                               badgeCy - numSz.y * 0.5f), nameCol, numBuf);
          }

          // Quest name (gold if completable, difficulty colored otherwise)
          ImU32 nameCol = allDone ? qlGold
                                  : GetQuestDifficultyColor(qd.recommendedLevel);
          float nameX = px + pad + badgeR * 2 + 6 * qs;
          DrawShadowText(ql, ImVec2(nameX, cy), nameCol, qd.questName.c_str());
          if (entryHov) {
            ImVec2 nameSize = ImGui::CalcTextSize(qd.questName.c_str());
            ql->AddLine(ImVec2(nameX, cy + nameSize.y),
                        ImVec2(nameX + nameSize.x, cy + nameSize.y),
                        (nameCol & 0x00FFFFFF) | 0x80000000);
          }
          cy += lineH + 2 * qs;

          if (allDone) {
            // Completable — just show "Return to NPC" (no objectives)
            char turnIn[64];
            snprintf(turnIn, sizeof(turnIn), "Return to %s",
                     GetGuardName(qd.guardType));
            DrawShadowText(ql, ImVec2(px + pad + 14 * qs, cy),
                           qlGold, turnIn);
            cy += lineH;
          } else {
            // In-progress — show objective lines
            for (int i = 0; i < qd.targetCount; i++) {
              char objBuf[80];
              bool done = aq.killCount[i] >= qd.targets[i].killsReq;
              snprintf(objBuf, sizeof(objBuf), "- %d/%d %s",
                       aq.killCount[i], (int)qd.targets[i].killsReq,
                       qd.targets[i].name.c_str());
              ImU32 objCol = done ? qlGreen : qlDim;
              DrawShadowText(ql, ImVec2(px + pad + 14 * qs, cy), objCol, objBuf);
              cy += lineH;
            }
          }

          cy += questGap;
        }

        ql->PopClipRect();

        // Scrollbar
        if (needsScroll) {
          float maxScroll = contentH - visH;
          float trackX = px + panelW - 5 * qs;
          float trackW = 3 * qs;
          float trackTop = py + 4;
          float trackH = panelH - 8;
          ql->AddRectFilled(ImVec2(trackX, trackTop),
                            ImVec2(trackX + trackW, trackTop + trackH),
                            IM_COL32(30, 25, 18, 80), 1.5f);
          float thumbRatio = visH / contentH;
          float thumbH = std::max(thumbRatio * trackH, 14.0f * qs);
          float thumbY = trackTop + (qlScrollY / maxScroll) * (trackH - thumbH);
          ql->AddRectFilled(ImVec2(trackX, thumbY),
                            ImVec2(trackX + trackW, thumbY + thumbH),
                            IM_COL32(160, 140, 100, 150), 1.5f);
        }
      }
    }

    // ── Quest Log Detail Panel (opened by clicking quest in log) ──
    if (!g_showQuestLog) g_questLogSelected = -1; // Close detail when log closes
    if (g_questLogSelected >= 0 && g_questLogSelected < (int)g_questCatalog.size()) {

      // Validate quest is still active
      auto *aq = FindActiveQuest(g_questLogSelected);
      if (!aq) {
        g_questLogSelected = -1;
      } else {
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 dispSz = ImGui::GetIO().DisplaySize;
        ImVec2 mPos = ImGui::GetIO().MousePos;
        float qs = dispSz.y / 768.0f;

        int classIdx = g_hero.GetClass() / 16;
        int qi = g_questLogSelected;
        const auto &q = g_questCatalog[qi];
        bool completable = IsQuestCompletable(*aq);

        // Colors (same as NPC quest dialog)
        ImU32 cBg = IM_COL32(12, 10, 8, 235);
        ImU32 cBorder = IM_COL32(80, 70, 50, 180);
        ImU32 cTitle = IM_COL32(255, 210, 80, 255);
        ImU32 cText = IM_COL32(200, 195, 180, 255);
        ImU32 cTextDim = IM_COL32(140, 135, 120, 255);
        ImU32 cGold = IM_COL32(255, 210, 50, 255);
        ImU32 cGreen = IM_COL32(80, 220, 80, 255);

        float panelW = 340.0f * qs;
        float btnW = 80.0f * qs, btnH = 26.0f * qs;

        // Guard name
        const char *guardName = GetGuardName(q.guardType);

        // Count class-specific item rewards
        int itemCount = 0;
        if (q.classReward[classIdx][0].defIndex >= 0) itemCount++;
        if (q.classReward[classIdx][1].defIndex >= 0) itemCount++;

        // Measure panel height
        float textWrap = panelW - 40 * qs;
        ImVec2 loreSize = ImGui::CalcTextSize(q.loreText.c_str(), nullptr, false, textWrap);
        float subtitleH = ImGui::CalcTextSize(guardName).y + 4 * qs;
        float objH = 18 * qs + q.targetCount * 26.0f * qs;
        float weaponH = itemCount > 0 ? (itemCount * 48.0f * qs + 4.0f * qs) : 0;
        float rewardsH = (10 + 18 + 18 + 18) * qs + weaponH;
        float buttonsH = btnH + 16 * qs;
        float infoH = ImGui::GetFontSize() + 8 * qs;
        float hintH = completable ? 24.0f * qs : 0.0f;
        float panelH = 14 * qs + ImGui::CalcTextSize(q.questName.c_str()).y + 6 * qs +
                       subtitleH + infoH + 12 * qs +
                       loreSize.y + 10 * qs + objH + rewardsH + hintH + buttonsH + 14 * qs;
        float px = (dispSz.x - panelW) * 0.5f;
        float py = (dispSz.y - panelH) * 0.5f;
        g_qldPanelRect[0] = px; g_qldPanelRect[1] = py;
        g_qldPanelRect[2] = panelW; g_qldPanelRect[3] = panelH;

        // Panel background
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                          cBg, 5.0f);
        dl->AddRect(ImVec2(px + 1, py + 1),
                    ImVec2(px + panelW - 1, py + panelH - 1),
                    IM_COL32(40, 35, 25, 100), 4.0f, 0, 1.0f);
        dl->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                    cBorder, 5.0f, 0, 1.5f);

        float contentY = py + 14 * qs;
        float margin = 20 * qs;
        float sepInset = 16 * qs;
        // Title
        {
          ImVec2 ts = ImGui::CalcTextSize(q.questName.c_str());
          DrawShadowText(dl, ImVec2(px + (panelW - ts.x) * 0.5f, contentY),
                         cTitle, q.questName.c_str());
          contentY += ts.y + 6 * qs;
        }
        // Guard name subtitle
        {
          ImVec2 gs = ImGui::CalcTextSize(guardName);
          dl->AddText(ImVec2(px + (panelW - gs.x) * 0.5f, contentY),
                      cTextDim, guardName);
          contentY += gs.y + 4 * qs;
        }
        // Location + level
        {
          char infoBuf[64];
          snprintf(infoBuf, sizeof(infoBuf), "%s  |  Lv. %d",
                   q.location.c_str(), (int)q.recommendedLevel);
          ImVec2 infoSz = ImGui::CalcTextSize(infoBuf);
          ImU32 lvColor = GetQuestDifficultyColor(q.recommendedLevel);
          dl->AddText(ImVec2(px + (panelW - infoSz.x) * 0.5f, contentY),
                      lvColor, infoBuf);
          contentY += infoSz.y + 4 * qs;
        }
        DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);
        contentY += 12 * qs;
        // Lore text (wrapped)
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(px + margin, contentY), cText,
                    q.loreText.c_str(), nullptr, textWrap);
        contentY += loreSize.y + 10 * qs;
        // Objectives with progress
        DrawShadowText(dl, ImVec2(px + margin, contentY),
                       IM_COL32(210, 180, 80, 255), "Objectives");
        contentY += 18 * qs;
        float barW = panelW - 60 * qs;
        for (int i = 0; i < q.targetCount; i++) {
          char objBuf[80];
          bool done = aq->killCount[i] >= q.targets[i].killsReq;
          snprintf(objBuf, sizeof(objBuf), "  %s  %d / %d",
                   q.targets[i].name.c_str(), aq->killCount[i],
                   (int)q.targets[i].killsReq);
          DrawShadowText(dl, ImVec2(px + 22 * qs, contentY),
                         done ? cGreen : cText, objBuf);
          contentY += 18 * qs;
          float frac = (float)aq->killCount[i] / (float)q.targets[i].killsReq;
          DrawProgressBar(dl, px + 24 * qs, contentY, barW, 4.0f * qs, frac);
          contentY += 8 * qs;
        }
        // Rewards
        contentY += 4 * qs;
        DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);
        contentY += 8 * qs;
        DrawShadowText(dl, ImVec2(px + margin, contentY),
                       IM_COL32(210, 180, 80, 255), "Rewards");
        contentY += 18 * qs;
        // Zen
        {
          std::string zenStr = std::to_string(q.zenReward);
          int n = (int)zenStr.length() - 3;
          while (n > 0) { zenStr.insert(n, ","); n -= 3; }
          char rwBuf[64];
          snprintf(rwBuf, sizeof(rwBuf), "  %s Zen", zenStr.c_str());
          DrawShadowText(dl, ImVec2(px + 22 * qs, contentY), cGold, rwBuf);
          contentY += 18 * qs;
        }
        // XP
        {
          std::string xpStr = std::to_string(q.xpReward);
          int n = (int)xpStr.length() - 3;
          while (n > 0) { xpStr.insert(n, ","); n -= 3; }
          char rwBuf[64];
          snprintf(rwBuf, sizeof(rwBuf), "  %s Experience", xpStr.c_str());
          DrawShadowText(dl, ImVec2(px + 22 * qs, contentY),
                         IM_COL32(180, 140, 255, 255), rwBuf);
          contentY += 18 * qs;
        }
        // Class-specific item rewards
        contentY += 4 * qs;
        float h1 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                       q.classReward[classIdx][0], qs);
        contentY += h1;
        float h2 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                       q.classReward[classIdx][1], qs);
        contentY += h2;
        // Completable hint
        if (completable) {
          contentY += 6 * qs;
          dl->AddText(ImVec2(px + margin, contentY), cGold,
                      "Return to complete this quest.");
          contentY += 18 * qs;
        }
        // Separator before buttons
        contentY += 2 * qs;
        DrawOrnamentSep(dl, px + sepInset, px + panelW - sepInset, contentY);

        // Buttons: Close + Abandon
        float btnY = py + panelH - btnH - 12.0f * qs;
        auto drawBtn = [&](float bx, float by, float bw, float bh,
                            const char *label, ImU32 textColor) -> bool {
          ImVec2 bMin(bx, by), bMax(bx + bw, by + bh);
          bool hov = mPos.x >= bMin.x && mPos.x <= bMax.x &&
                     mPos.y >= bMin.y && mPos.y <= bMax.y;
          ImU32 topCol = hov ? IM_COL32(55, 50, 35, 235) : IM_COL32(35, 30, 22, 230);
          ImU32 botCol = hov ? IM_COL32(35, 30, 22, 235) : IM_COL32(18, 16, 12, 230);
          dl->AddRectFilledMultiColor(bMin, bMax, topCol, topCol, botCol, botCol);
          dl->AddRect(bMin, bMax,
                      hov ? IM_COL32(140, 120, 70, 220) : cBorder,
                      3.0f, 0, 1.0f);
          dl->AddLine(ImVec2(bx + 2, by + 1), ImVec2(bx + bw - 2, by + 1),
                      IM_COL32(255, 255, 255, hov ? (uint8_t)30 : (uint8_t)15));
          ImVec2 ls = ImGui::CalcTextSize(label);
          DrawShadowText(dl, ImVec2(bx + (bw - ls.x) * 0.5f,
                         by + (bh - ls.y) * 0.5f), textColor, label);
          return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        };

        float gap = 16.0f;
        float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
        float bx2 = bx1 + btnW + gap;
        if (drawBtn(bx1, btnY, btnW, btnH, "Close", cTextDim)) {
          SoundManager::Play(SOUND_CLICK01);
          g_questLogSelected = -1;
        }
        if (drawBtn(bx2, btnY, btnW, btnH, "Abandon",
                     IM_COL32(220, 80, 80, 255))) {
          SoundManager::Play(SOUND_CLICK01);
          g_server.SendQuestAbandon((uint8_t)qi);
          g_questLogSelected = -1;
        }
      }
    } else {
      g_qldPanelRect[2] = 0; // No panel visible
    }

    // ── Quest Progress Popups (WoW-style center screen) ──
    {
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      ImVec2 dispSz = ImGui::GetIO().DisplaySize;
      float qs = dispSz.y / 768.0f;
      float popupY = 12.0f * qs; // top of screen
      float stackOffset = 0.0f;

      for (int i = 0; i < 4; i++) {
        auto &p = g_questPopups[i];
        if (p.timer <= 0.0f) continue;
        p.timer -= deltaTime;
        if (p.timer <= 0.0f) { p.timer = 0.0f; continue; }

        // Fade: full alpha for first 2s, fade out over last 1s
        float alpha = p.timer < 1.0f ? p.timer : 1.0f;
        int a = (int)(alpha * 255);

        float fontSize = 14.0f * qs;
        float smallFs = 11.0f * qs;

        // Quest name (small, gold, above)
        ImVec2 qSz = ImGui::CalcTextSize(p.quest);
        float qx = (dispSz.x - qSz.x * (smallFs / ImGui::GetFontSize())) * 0.5f;
        float qy = popupY + stackOffset;
        dl->AddText(nullptr, smallFs, ImVec2(qx + 1, qy + 1),
                    IM_COL32(0, 0, 0, (int)(alpha * 160)), p.quest);
        dl->AddText(nullptr, smallFs, ImVec2(qx, qy),
                    IM_COL32(255, 210, 50, a), p.quest);

        // Objective progress (larger, white or green)
        ImVec2 tSz = ImGui::CalcTextSize(p.text);
        float tx = (dispSz.x - tSz.x * (fontSize / ImGui::GetFontSize())) * 0.5f;
        float ty = qy + smallFs + 2 * qs;
        ImU32 col = p.complete ? IM_COL32(80, 255, 80, a)
                               : IM_COL32(255, 255, 255, a);
        dl->AddText(nullptr, fontSize, ImVec2(tx + 1, ty + 1),
                    IM_COL32(0, 0, 0, (int)(alpha * 180)), p.text);
        dl->AddText(nullptr, fontSize, ImVec2(tx, ty), col, p.text);

        stackOffset += (fontSize + smallFs + 8 * qs);
      }
    }

    // ── Minimap overlay (TAB held) — camera-aligned outline style, left side ──
    if (g_showMinimap && bgfx::isValid(g_minimapTex)) {
      ImDrawList *mmDl = ImGui::GetForegroundDrawList();
      ImVec2 dispSz = ImGui::GetIO().DisplaySize;

      // Map display area — left side of screen
      float mapSz = 400.0f;
      float scale = mapSz / 362.0f; // 256*sqrt(2) ≈ 362 to fit diamond
      float h = 256.0f * scale;     // half-diagonal
      float cx = h + 30.0f;         // left padding
      float cy = h + 30.0f;         // top padding

      // World-to-minimap screen coordinate transform
      auto worldToMinimap = [&](const glm::vec3 &worldPos) -> ImVec2 {
        float gx = worldPos.z / 100.0f;
        float gz = worldPos.x / 100.0f;
        float rx = (gz + gx - 256.0f) * scale;
        float ry = (-gz + gx) * scale;
        return ImVec2(cx + rx, cy + ry);
      };

      // Diamond corners
      ImVec2 pTop(cx, cy - h);
      ImVec2 pRight(cx + h, cy);
      ImVec2 pBottom(cx, cy + h);
      ImVec2 pLeft(cx - h, cy);

      // 50% black background diamond
      mmDl->AddQuadFilled(pTop, pRight, pBottom, pLeft, IM_COL32(0, 0, 0, 128));

      // Map texture overlay
      ImTextureID mmTex = (ImTextureID)(uintptr_t)g_minimapTex.idx;
      mmDl->AddImageQuad(mmTex,
          pTop, pRight, pBottom, pLeft,
          ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0));

      // WoW-style ornate border — layered diamond outlines
      float bw = 3.0f; // border width
      // Outer dark shadow
      auto offsetPt = [](ImVec2 c, ImVec2 p, float off) -> ImVec2 {
        float dx = p.x - c.x, dy = p.y - c.y;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.01f) return p;
        return ImVec2(p.x + dx/len*off, p.y + dy/len*off);
      };
      ImVec2 cPt(cx, cy);
      ImVec2 oT = offsetPt(cPt, pTop, bw+2), oR = offsetPt(cPt, pRight, bw+2);
      ImVec2 oB = offsetPt(cPt, pBottom, bw+2), oL = offsetPt(cPt, pLeft, bw+2);
      mmDl->AddQuad(oT, oR, oB, oL, IM_COL32(0, 0, 0, 120), 2.0f);
      // Main gold border
      ImVec2 mT = offsetPt(cPt, pTop, bw), mR = offsetPt(cPt, pRight, bw);
      ImVec2 mB = offsetPt(cPt, pBottom, bw), mL = offsetPt(cPt, pLeft, bw);
      mmDl->AddQuad(mT, mR, mB, mL, IM_COL32(160, 130, 60, 200), 2.5f);
      // Inner highlight
      mmDl->AddQuad(pTop, pRight, pBottom, pLeft, IM_COL32(200, 170, 80, 140), 1.0f);
      // Corner ornaments — small filled diamonds at each corner
      float orn = 6.0f;
      for (auto &cp : {mT, mR, mB, mL}) {
        mmDl->AddCircleFilled(cp, orn, IM_COL32(140, 110, 45, 200), 4);
        mmDl->AddCircle(cp, orn, IM_COL32(200, 170, 80, 220), 4, 1.5f);
        mmDl->AddCircleFilled(cp, orn*0.4f, IM_COL32(220, 195, 100, 240), 4);
      }

      // Quest target monster markers (red dots — ALL active quest targets)
      glm::vec3 heroPos = g_hero.GetPosition();
      if (!g_activeQuests.empty()) {
        // Collect all target monster types across all active quests
        uint8_t targetTypes[60]; // max 20 quests * 3 targets
        int nTargets = 0;
        for (auto &aq : g_activeQuests) {
          if (aq.questId < 0 || aq.questId >= (int)g_questCatalog.size()) continue;
          const auto &qd = g_questCatalog[aq.questId];
          for (int t = 0; t < qd.targetCount && nTargets < 60; t++) {
            if (aq.killCount[t] >= qd.targets[t].killsReq) continue;
            targetTypes[nTargets++] = qd.targets[t].monType;
          }
        }
        if (nTargets > 0) {
          int monCount = g_monsterManager.GetMonsterCount();
          for (int i = 0; i < monCount; i++) {
            MonsterInfo mi = g_monsterManager.GetMonsterInfo(i);
            if (mi.hp <= 0) continue;
            // Skip own summon — don't mark it as quest target
            if (g_monsterManager.IsOwnSummon(i)) continue;
            bool isTarget = false;
            for (int t = 0; t < nTargets; t++)
              if (mi.type == targetTypes[t]) { isTarget = true; break; }
            if (!isTarget) continue;
            ImVec2 sp = worldToMinimap(mi.position);
            mmDl->AddCircleFilled(sp, 3.0f, IM_COL32(255, 60, 60, 220));
          }
        }
      }

      // NPC markers — quest NPCs get ! or ? icons (WoW-style), others get yellow dots
      {
        auto &questMarkers = g_npcManager.GetQuestMarkers();
        int npcCount = g_npcManager.GetNpcCount();
        ImFont *font = ImGui::GetFont();
        float markerFs = 20.0f; // Quest marker font size on minimap

        for (int i = 0; i < npcCount; i++) {
          NpcInfo ni = g_npcManager.GetNpcInfo(i);
          ImVec2 sp = worldToMinimap(ni.position);

          // Check if this NPC has a quest marker
          char marker = '\0';
          bool isGold = false;
          for (auto &gm : questMarkers) {
            if (gm.guardType == ni.type) {
              marker = gm.marker;
              isGold = gm.isGold;
              break;
            }
          }

          if (marker != '\0') {
            // Quest marker: ! (available) or ? (completable/in-progress)
            char buf[2] = {marker, '\0'};
            ImVec2 ts = font->CalcTextSizeA(markerFs, FLT_MAX, 0, buf);
            float tx = sp.x - ts.x * 0.5f;
            float ty = sp.y - ts.y * 0.5f;
            ImU32 col = isGold ? IM_COL32(255, 210, 50, 255)
                               : IM_COL32(160, 160, 160, 220);
            // Shadow
            mmDl->AddText(font, markerFs, ImVec2(tx + 1.5f, ty + 1.5f),
                          IM_COL32(0, 0, 0, 200), buf);
            mmDl->AddText(font, markerFs, ImVec2(tx, ty), col, buf);
          } else {
            // Non-quest NPC: small yellow dot
            mmDl->AddCircleFilled(sp, 3.0f, IM_COL32(255, 220, 50, 230));
          }
        }
      }

      // Player arrow — rotates with character facing
      // facing = atan2(dir.z, -dir.x)
      // On rotated minimap: the 45° rotation changes the arrow mapping.
      // World direction (dx, dz) → grid direction (dz_g=dx/100, dx_g=dz/100)
      // Screen: (dz_g+dx_g, -dz_g+dx_g) * scale
      // facing=0 → dir=(-worldX) → (-dz_g, 0) → screen(-1, 1)*scale = down-left...
      // Simpler: just use worldToMinimap offset
      ImVec2 heroScreen = worldToMinimap(heroPos);
      float facing = g_hero.GetFacing();
      // Compute a world offset in facing direction, then project
      float fwdX = -cosf(facing) * 100.0f; // world X offset (facing is atan2(dz,-dx))
      float fwdZ = sinf(facing) * 100.0f;  // world Z offset
      glm::vec3 fwdPos = heroPos + glm::vec3(fwdX, 0, fwdZ);
      ImVec2 fwdScreen = worldToMinimap(fwdPos);
      float adx = fwdScreen.x - heroScreen.x;
      float ady = fwdScreen.y - heroScreen.y;
      float alen = sqrtf(adx * adx + ady * ady);
      if (alen > 0.01f) { adx /= alen; ady /= alen; }
      float sz = 10.0f;
      ImVec2 tip(heroScreen.x + adx * sz, heroScreen.y + ady * sz);
      ImVec2 left(heroScreen.x + (-adx * 0.5f + ady * 0.6f) * sz,
                  heroScreen.y + (-ady * 0.5f - adx * 0.6f) * sz);
      ImVec2 right(heroScreen.x + (-adx * 0.5f - ady * 0.6f) * sz,
                   heroScreen.y + (-ady * 0.5f + adx * 0.6f) * sz);
      mmDl->AddTriangleFilled(tip, left, right, IM_COL32(50, 255, 50, 255));
      mmDl->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 200), 1.5f);

    }

    // ── Character Info and Inventory panels ──
    // Recalculate UI panel scale based on current window height
    {
      int ww, wh;
      glfwGetWindowSize(window, &ww, &wh);
      InventoryUI::UpdatePanelScale(wh);
    }
    ImDrawList *panelDl = ImGui::GetForegroundDrawList();
    if (g_shopOpen)
      InventoryUI::RenderShopPanel(panelDl, g_hudCoords);
    if (g_showCharInfo)
      InventoryUI::RenderCharInfoPanel(panelDl, g_hudCoords);
    if (g_showSkillWindow)
      InventoryUI::RenderSkillPanel(panelDl, g_hudCoords);
    if (g_showMapWindow)
      InventoryUI::RenderMapPanel(panelDl, g_hudCoords);
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
      float ms = ImGui::GetIO().DisplaySize.y / 768.0f; // menu scale
      float panelW = 260.0f * ms, panelH = 240.0f * ms;
      float px = (dispSize.x - panelW) * 0.5f;
      float py = (dispSize.y - panelH) * 0.5f;
      ImVec2 pMin(px, py), pMax(px + panelW, py + panelH);

      // Panel background: gradient fill
      dl->AddRectFilledMultiColor(pMin, pMax,
          IM_COL32(18, 14, 10, 245), IM_COL32(18, 14, 10, 245),
          IM_COL32(10, 8, 6, 250), IM_COL32(10, 8, 6, 250));
      // Outer dark border
      dl->AddRect(pMin, pMax, IM_COL32(6, 5, 3, 255), 2.0f, 0, 1.5f);
      // Gold frame
      dl->AddRect(ImVec2(px + 2, py + 2), ImVec2(px + panelW - 2, py + panelH - 2),
                  IM_COL32(130, 110, 50, 200), 1.0f, 0, 1.5f);

      // Title: "Game Menu" with shadow
      const char *title = "Game Menu";
      ImVec2 titleSize = ImGui::CalcTextSize(title);
      float titleX = px + (panelW - titleSize.x) * 0.5f;
      DrawShadowText(dl, ImVec2(titleX, py + 16.0f * ms),
                     IM_COL32(255, 215, 90, 255), title);
      // Separator line under title
      float sepY = py + 38.0f * ms;
      dl->AddLine(ImVec2(px + 16 * ms, sepY), ImVec2(px + panelW - 16 * ms, sepY),
                  IM_COL32(100, 85, 40, 150));

      // Ornate button helper (same style as CharacterSelect)
      auto drawMenuBtn = [&](float bx, float by, float bw, float bh,
                             const char *label) -> bool {
        ImVec2 mp = ImGui::GetMousePos();
        bool hov = mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh;
        bool held = hov && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool clicked = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        // Outer dark
        dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                    IM_COL32(8, 6, 4, 255), 2.0f, 0, 1.5f);
        // Gold frame
        ImU32 goldCol = hov ? IM_COL32(200, 170, 75, 240)
                            : IM_COL32(145, 120, 55, 220);
        dl->AddRect(ImVec2(bx + 2, by + 2), ImVec2(bx + bw - 2, by + bh - 2),
                    goldCol, 1.0f, 0, 1.5f);
        // Crimson gradient fill
        float ix = bx + 4, iy = by + 4, iw = bw - 8, ih = bh - 8;
        ImU32 topCol = held  ? IM_COL32(130, 45, 30, 255)
                     : hov   ? IM_COL32(110, 35, 25, 250)
                              : IM_COL32(80, 25, 20, 240);
        ImU32 botCol = held  ? IM_COL32(75, 22, 16, 255)
                     : hov   ? IM_COL32(60, 18, 14, 250)
                              : IM_COL32(40, 12, 10, 240);
        dl->AddRectFilledMultiColor(ImVec2(ix, iy), ImVec2(ix + iw, iy + ih),
                                    topCol, topCol, botCol, botCol);
        // Top highlight
        dl->AddLine(ImVec2(ix + 1, iy + 1), ImVec2(ix + iw - 1, iy + 1),
                    IM_COL32(255, 200, 150, hov ? (uint8_t)50 : (uint8_t)30));
        // Bottom shadow
        dl->AddLine(ImVec2(ix + 1, iy + ih - 1), ImVec2(ix + iw - 1, iy + ih - 1),
                    IM_COL32(0, 0, 0, 80));
        // Text
        ImVec2 ts = ImGui::CalcTextSize(label);
        DrawShadowText(dl, ImVec2(bx + (bw - ts.x) * 0.5f, by + (bh - ts.y) * 0.5f),
                       IM_COL32(240, 220, 155, 255), label);
        return clicked;
      };

      // Button dimensions and positions
      float btnW = 190.0f * ms, btnH = 38.0f * ms;
      float btnX = px + (panelW - btnW) * 0.5f;
      float btn1Y = py + 52.0f * ms;
      float btn2Y = py + 102.0f * ms;
      float btn3Y = py + 162.0f * ms;

      // "Full Screen" / "Windowed" toggle button
      if (drawMenuBtn(btnX, btn1Y, btnW, btnH,
                      g_isFullscreen ? "Windowed" : "Full Screen")) {
        SoundManager::Play(SOUND_CLICK01);
        ToggleFullscreen(window);
        g_showGameMenu = false;
      }

      // "Switch Character" button
      if (drawMenuBtn(btnX, btn2Y, btnW, btnH, "Switch Character")) {
        SoundManager::Play(SOUND_CLICK01);
        SoundManager::StopAll();
        SoundManager::StopMusic();
        SoundManager::PlayMusic(g_dataPath + "/Music/crywolf_before-01.ogg", true);
        g_server.SendCharListRequest();
        g_showGameMenu = false;
        g_hero.StopMoving();
        InputHandler::ResetGameReady();
        g_gameState = GameState::CHAR_SELECT;
        g_worldInitialized = false;
        // Reset all state to prevent bleed-through between characters
        serverData.equipment.clear();
        serverData.npcs.clear();
        serverData.monsters.clear();
        g_monsterManager.ClearMonsters();
        g_npcManager.ClearSpawnedNpcs();
        serverData.hasSpawnPos = false;
        g_currentMapId = 0;
        serverData.spawnMapId = 0;
        ClientPacketHandler::ResetForCharSwitch();
        // Re-init character select scene
        CharacterSelect::Context csCtx;
        csCtx.server = &g_server;
        csCtx.dataPath = data_path;
        csCtx.window = window;
        csCtx.onCharSelected = [&]() {
          SoundManager::StopMusic();
          g_loadingFrames = 0;
          g_gameState = GameState::LOADING;
          std::cout << "[State] -> LOADING (waiting for world data)"
                    << std::endl;
        };
        csCtx.onExit = [&]() { glfwSetWindowShouldClose(window, GLFW_TRUE); };
        csCtx.onToggleFullscreen = [&]() { ToggleFullscreen(window); };
        csCtx.fontDefault = g_fontDefault;
        csCtx.fontBold = g_fontBold;
        csCtx.fontRegion = g_fontRegion;
        if (g_clientState) csCtx.classDefinitions = &g_clientState->classDefinitions;
        CharacterSelect::Init(csCtx);
      }

      // "Exit Game" button
      if (drawMenuBtn(btnX, btn3Y, btnW, btnH, "Exit Game")) {
        SoundManager::Play(SOUND_CLICK01);
        glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
    }

    // ── Command Terminal ──
    {
      static bool prevTerminalOpen = false;
      if (g_showCommandTerminal && !prevTerminalOpen) {
        g_commandFocusNeeded = true; // grab keyboard focus on open
      }
      prevTerminalOpen = g_showCommandTerminal;
    }
    if (g_showCommandTerminal) {
      ImVec2 dispSize = ImGui::GetIO().DisplaySize;
      float ts = dispSize.y / 768.0f; // terminal scale
      float termW = 400.0f * ts, termH = 32.0f * ts;
      float termX = (dispSize.x - termW) * 0.5f;
      float termY = dispSize.y - 185.0f * ts;

      ImGui::SetNextWindowPos(ImVec2(termX, termY));
      ImGui::SetNextWindowSize(ImVec2(termW, termH));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.75f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.47f, 0.39f, 0.24f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
      ImGui::Begin("##CommandTerminal", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoCollapse);

      ImGui::PushItemWidth(termW - 12.0f * ts);
      if (g_commandFocusNeeded) {
        ImGui::SetKeyboardFocusHere();
        g_commandFocusNeeded = false;
      }
      bool submitted =
          ImGui::InputText("##cmd", g_commandBuffer, sizeof(g_commandBuffer),
                           ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::PopItemWidth();

      if (submitted && g_commandBuffer[0] != '\0') {
        std::string cmd(g_commandBuffer);
        std::string cmdLower = cmd;
        for (auto &c : cmdLower)
          c = (char)std::tolower((unsigned char)c);

        {
          SystemMessageLog::Log(MSG_SYSTEM, IM_COL32(255, 200, 100, 255),
                                "Unknown command: %s", cmd.c_str());
        }
        g_commandBuffer[0] = '\0';
        g_showCommandTerminal = false;
      }
      if (submitted && g_commandBuffer[0] == '\0') {
        // Empty submit = close terminal
        g_showCommandTerminal = false;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_commandBuffer[0] = '\0';
        g_showCommandTerminal = false;
      }

      ImGui::End();
      ImGui::PopStyleVar(3);
      ImGui::PopStyleColor(2);
    }



    ImGui_BackendSetViewId(IMGUI_VIEW_MAIN);
    bgfx::setViewRect(IMGUI_VIEW_MAIN, 0, 0, uint16_t(fbW), uint16_t(fbH));
    bgfx::touch(IMGUI_VIEW_MAIN);
    ImGui::Render();
    ImGui_BackendRenderDrawData(ImGui::GetDrawData());

    // Flatten render queue (items on top of UI)
    // Scale logical pixel coords to physical framebuffer pixels (HiDPI/Retina
    // fix)
    {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      ItemModelManager::SetFramebufferSize(fbW, fbH);
      float scaleX = (float)fbW / ImGui::GetIO().DisplaySize.x;
      float scaleY = (float)fbH / ImGui::GetIO().DisplaySize.y;
      for (const auto &job : InventoryUI::GetRenderQueue()) {
        int px = (int)(job.x * scaleX);
        int py = (int)(job.y * scaleY);
        int pw = (int)(job.w * scaleX);
        int ph = (int)(job.h * scaleY);
        ItemModelManager::RenderItemUI(job.modelFile, job.defIndex, px, py, pw,
                                       ph, job.hovered, job.itemLevel,
                                       job.overlay);
      }
    }

    // Second ImGui pass: draw deferred tooltip and HUD overlays ON TOP of 3D
    // items. Use a higher BGFX view so these render after item models.
    if (InventoryUI::HasPendingTooltip() ||
        InventoryUI::HasDeferredOverlays() ||
        InventoryUI::HasDeferredCooldowns() ||
        InventoryUI::HasActiveNotification() ||
        InventoryUI::HasActiveRegionName()) {
      ImGui_BackendSetViewId(IMGUI_VIEW_OVERLAY);
      bgfx::setViewRect(IMGUI_VIEW_OVERLAY, 0, 0, uint16_t(fbW), uint16_t(fbH));
      bgfx::touch(IMGUI_VIEW_OVERLAY);
      ImGui_BackendNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      // Suppress ImGui's default fallback window (overlay uses foreground draw list only)
      ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowPos(ImVec2(-200, -200), ImGuiCond_Always);
      ImGui::Begin("Debug##Overlay", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImGui::End();

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
      InventoryUI::UpdateAndRenderRegionName(deltaTime);

      ImGui::Render();
      ImGui_BackendRenderDrawData(ImGui::GetDrawData());
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
      break;
    }

    // Auto-diagnostic: capture AFTER render, BEFORE swap (back buffer has
    // current frame)
    if (autoDiag && diagFrame >= 2) {
      int mode = (diagFrame - 2) / 2;
      if (mode < 6 && (diagFrame - 2) % 2 == 1) {
        std::string diagPath =
            "screenshots/diag_" + std::string(diagNames[mode]) + ".jpg";
      } else if (mode >= 6) {
        break;
      }
    }
    if (autoDiag || autoScreenshot || autoGif)
      diagFrame++;

    // Update mouseOverUIPanel flag for cursor/hover gating in InputHandler
    {
      ImVec2 mp = ImGui::GetIO().MousePos;
      float sx = mp.x, sy = mp.y; // screen pixels
      // Virtual coords for InventoryUI panels and skill window (1280x720 space)
      float vx = g_hudCoords.ToVirtualX(sx);
      float vy = g_hudCoords.ToVirtualY(sy);
      bool over = false;
      if (g_showGameMenu || g_showCommandTerminal) {
        over = true; // full-screen overlay or terminal open
      } else {
        // InventoryUI panels — virtual coords
        if (g_showCharInfo && InventoryUI::IsCharInfoPointInPanel(vx, vy))
          over = true;
        if (g_showInventory && InventoryUI::IsPointInPanel(
                                   vx, vy, InventoryUI::GetInventoryPanelX()))
          over = true;
        if (g_shopOpen &&
            InventoryUI::IsPointInPanel(vx, vy, InventoryUI::GetShopPanelX()))
          over = true;
        if (g_showMapWindow && InventoryUI::IsPointInPanel(
                                    vx, vy, InventoryUI::GetMapPanelX()))
          over = true;
        // Skill window — virtual coords
        if (g_showSkillWindow) {
          float sw = 492.0f, sh = 400.0f;
          float skx = (1270.0f - sw) * 0.5f, sky = (720.0f - sh) * 0.5f;
          if (vx >= skx && vx < skx + sw && vy >= sky && vy < sky + sh)
            over = true;
        }
        // Quest dialog — screen pixels
        if (g_questDialogOpen && g_qdPanelRect[2] > 0) {
          if (sx >= g_qdPanelRect[0] &&
              sx < g_qdPanelRect[0] + g_qdPanelRect[2] &&
              sy >= g_qdPanelRect[1] &&
              sy < g_qdPanelRect[1] + g_qdPanelRect[3])
            over = true;
        }
        // Quest log — screen pixels
        if (g_showQuestLog && g_qlPanelRect[2] > 0) {
          if (sx >= g_qlPanelRect[0] &&
              sx < g_qlPanelRect[0] + g_qlPanelRect[2] &&
              sy >= g_qlPanelRect[1] &&
              sy < g_qlPanelRect[1] + g_qlPanelRect[3])
            over = true;
        }
        // Quest log detail panel — screen pixels
        if (g_questLogSelected >= 0 && g_qldPanelRect[2] > 0) {
          if (sx >= g_qldPanelRect[0] &&
              sx < g_qldPanelRect[0] + g_qldPanelRect[2] &&
              sy >= g_qldPanelRect[1] &&
              sy < g_qldPanelRect[1] + g_qldPanelRect[3])
            over = true;
        }
        // Minimap overlay — screen pixels (diamond bounding box, left side)
        if (g_showMinimap) {
          ImVec2 ds = ImGui::GetIO().DisplaySize;
          float mmScale = 400.0f / 362.0f;
          float mmH = 256.0f * mmScale;
          float ccx = mmH + 20.0f, ccy = ds.y * 0.5f;
          if (sx >= ccx - mmH && sx <= ccx + mmH &&
              sy >= ccy - mmH && sy <= ccy + mmH)
            over = true;
        }
        // Bottom HUD bar — prevent hover/clicks through health orbs and quickbar
        {
          ImVec2 ds = ImGui::GetIO().DisplaySize;
          float hudH = 100.0f; // approximate HUD bar height at bottom
          if (sy >= ds.y - hudH)
            over = true;
        }
      }
      g_mouseOverUIPanel = over;
    }

    // Map transition overlay (fullscreen black fade) — highest view layer.
    if (g_mapTransitionActive && g_mapTransitionAlpha > 0.0f) {
      ImGui_BackendSetViewId(IMGUI_VIEW_TRANSITION);
      ImGui_BackendNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      // Suppress ImGui's default fallback window
      ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowPos(ImVec2(-200, -200), ImGuiCond_Always);
      ImGui::Begin("Debug##Transition", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImGui::End();
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      uint8_t a = (uint8_t)(g_mapTransitionAlpha * 255.0f);
      dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                        IM_COL32(0, 0, 0, a));
      ImGui::Render();
      ImGui_BackendRenderDrawData(ImGui::GetDrawData());
    }

    // Per-frame GL error check (only first 10 frames to avoid log spam)
    {
      static int frameNum = 0;
      if (frameNum < 10)
        checkGLError(("frame " + std::to_string(frameNum)).c_str());
      frameNum++;
    }

    ImGui_BackendSetViewId(IMGUI_VIEW_MAIN); // Reset for next frame
    bgfx::frame();
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
  ChromeGlow::DeleteTextures();
  g_monsterManager.Cleanup();
  g_boidManager.Cleanup();
  g_npcManager.Cleanup();
  g_hero.Cleanup();
  g_clickEffect.Cleanup();
  g_sky.Cleanup();
  g_fireEffect.Cleanup();
  g_objectRenderer.Cleanup();
  g_grass.Cleanup();
  g_vfxManager.Cleanup();
  g_terrain.Cleanup();
  // Cleanup shadow map
  if (g_shadowMap.depthShader) g_shadowMap.depthShader->destroy();
  if (bgfx::isValid(g_shadowMap.fb)) bgfx::destroy(g_shadowMap.fb);
  if (bgfx::isValid(g_shadowMap.colorTex)) bgfx::destroy(g_shadowMap.colorTex);
  if (bgfx::isValid(g_shadowMap.depthTex)) bgfx::destroy(g_shadowMap.depthTex);
  // Cleanup minimap
  if (bgfx::isValid(g_minimapTex)) bgfx::destroy(g_minimapTex);

  ImGui_ImplBgfx_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  bgfx::shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();

  // StreamRedirector handles restoration and deletion

  return 0;
}

// ═══════════════════════════════════════════════════════════════════
// InitGameWorld — called once after character select, when server
// has sent all initial world data (NPCs, monsters, equipment, stats)
// ═══════════════════════════════════════════════════════════════════

static void InitGameWorld(ServerData &serverData, LoadProgressFn onProgress) {
  std::string data_path = g_dataPath;

  if (onProgress) onProgress(0.10f, "Shutting down char select...");

  // Shut down character select scene (free World74 resources)
  CharacterSelect::Shutdown();

  // Load the correct map directly from CharInfo (server tells us which map)
  if (onProgress) onProgress(0.15f, "Loading terrain...");
  LoadWorld(serverData.spawnMapId);

  // Re-point all subsystems to freshly loaded terrain (LoadWorld replaces g_terrainDataPtr)
  g_hero.SetTerrainData(g_terrainDataPtr);
  g_hero.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_npcManager.SetTerrainData(g_terrainDataPtr);
  g_boidManager.SetTerrainData(g_terrainDataPtr);
  g_clickEffect.SetTerrainData(g_terrainDataPtr);

  if (onProgress) onProgress(0.20f, "Loading NPCs...");

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

  if (onProgress) onProgress(0.35f, "Loading equipment...");

  // Reset hero equipment before applying new character's equipment
  {
    WeaponEquipInfo emptyWeapon;
    emptyWeapon.category = 0xFF;
    g_hero.EquipWeapon(emptyWeapon);
    g_hero.EquipShield(emptyWeapon);
    g_hero.UnequipPet();
    g_hero.UnequipMount();
    for (int bp = 0; bp < 5; bp++)
      g_hero.EquipBodyPart(bp, ""); // revert to default naked body
  }
  // Reset equipment UI slots
  for (int i = 0; i < 12; i++) {
    g_equipSlots[i] = {};
    g_equipSlots[i].category = 0xFF;
  }

  // Equip weapon + shield + armor + pet from server equipment data (DB-driven)
  for (auto &eq : serverData.equipment) {
    // Re-populate UI slots (they were reset above, need server data restored)
    if (eq.slot < 12 && eq.info.category != 0xFF) {
      g_equipSlots[eq.slot].category = eq.info.category;
      g_equipSlots[eq.slot].itemIndex = eq.info.itemIndex;
      g_equipSlots[eq.slot].itemLevel = eq.info.itemLevel;
      g_equipSlots[eq.slot].quantity = eq.info.quantity;
      int16_t defIdx =
          (int16_t)eq.info.category * 32 + (int16_t)eq.info.itemIndex;
      const char *clientModel = ItemDatabase::GetDropModelName(defIdx);
      g_equipSlots[eq.slot].modelFile =
          (clientModel && clientModel[0]) ? clientModel : eq.info.modelFile;
      g_equipSlots[eq.slot].equipped = true;
    }
    if (eq.slot == 0) {
      g_hero.EquipWeapon(eq.info);
    } else if (eq.slot == 1) {
      g_hero.EquipShield(eq.info);
    } else if (eq.slot == 8 && eq.info.category == 13 &&
               (eq.info.itemIndex == 0 || eq.info.itemIndex == 1)) {
      // Pet slot: Guardian Angel (0) or Imp (1)
      g_hero.EquipPet(eq.info.itemIndex);
    }
    int bodyPart = ItemDatabase::GetBodyPartIndex(eq.info.category);
    if (bodyPart >= 0) {
      std::string partModel = ItemDatabase::GetBodyPartModelFile(
          eq.info.category, eq.info.itemIndex);
      if (!partModel.empty())
        g_hero.EquipBodyPart(bodyPart, partModel, eq.info.itemLevel,
                             eq.info.itemIndex);
    }
    std::cout << "[Equip] Slot " << (int)eq.slot << ": " << eq.info.modelFile
              << " cat=" << (int)eq.info.category << std::endl;
  }

  g_syncDone = true;
  // Stop char select music — actual game music chosen after spawn position is
  // known
  SoundManager::StopMusic();
  // Determine floor from spawn position and show floor-specific region name
  if (serverData.hasSpawnPos)
    g_currentFloor = DetermineFloor(g_currentMapId, serverData.spawnGridX, serverData.spawnGridY);
  else
    g_currentFloor = 0;
  InventoryUI::ShowRegionName(GetFloorDisplayName());
  g_npcManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_npcManager.SetVFXManager(&g_vfxManager);
  InventoryUI::RecalcEquipmentStats();
  g_npcManager.SetPointLights(g_pointLights);
  g_boidManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_boidManager.SetPointLights(g_pointLights);

  if (onProgress) onProgress(0.55f, "Loading monsters...");

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

  if (onProgress) onProgress(0.75f, "Placing hero...");

  // Spawn at server-provided position (from CharInfo F3:03), fallback to town
  // center
  if (serverData.hasSpawnPos) {
    float spawnX = (float)serverData.spawnGridY * 100.0f;
    float spawnZ = (float)serverData.spawnGridX * 100.0f;
    g_hero.SetPosition(glm::vec3(spawnX, 0.0f, spawnZ));
    std::cout << "[Spawn] Using server position: grid ("
              << (int)serverData.spawnGridX << "," << (int)serverData.spawnGridY
              << ") -> world (" << spawnX << "," << spawnZ << ")" << std::endl;
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
    bool walkable =
        (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
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

  // Restore camera zoom from server (persisted per character)
  if (serverData.cameraZoom >= 7000 && serverData.cameraZoom <= 9500) {
    g_camera.SetZoom(serverData.cameraZoom / 10.0f);
  }

  g_objectRenderer.ResetDoorStates();

  // Choose music based on spawn position terrain attribute
  {
    glm::vec3 heroPos = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(heroPos.x / 100.0f);
    int gx = (int)(heroPos.z / 100.0f);
    bool inSafeZone =
        (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
        (g_terrainDataPtr->mapping.attributes[gz * S + gx] & 0x01) != 0;
    g_hero.SetInSafeZone(inSafeZone);
    const MapConfig &sndCfg = *GetMapConfig(g_currentMapId);
    // Indoor maps (Dungeon, Lost Tower): always play ambient regardless of safe zone
    bool indoorMap = (g_currentMapId == 1 || g_currentMapId == 4 || g_currentMapId == 7);
    if (sndCfg.ambientLoop && (indoorMap || !inSafeZone) && !sndCfg.hasWind)
      SoundManager::PlayLoop(sndCfg.ambientLoop);
    if (inSafeZone && sndCfg.safeMusic)
      SoundManager::PlayMusic(g_dataPath + "/" + sndCfg.safeMusic);
    else if (!inSafeZone && sndCfg.wildMusic)
      SoundManager::PlayMusic(g_dataPath + "/" + sndCfg.wildMusic);
  }

  std::cout << "[World] Game world initialized" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// ApplyMapAtmosphere — applies all per-map rendering/audio settings
// from a MapConfig. Called by both initial load and ChangeMap.
// ═══════════════════════════════════════════════════════════════════
static void ApplyMapAtmosphere(const MapConfig &cfg) {
  g_mapCfg = &cfg;
  g_currentMapId = cfg.mapId;

  // Clear color
  g_clearColor = ImVec4(cfg.clearR, cfg.clearG, cfg.clearB, 1.0f);

  // Fog — propagate to all renderers including grass
  glm::vec3 fogCol(cfg.fogR, cfg.fogG, cfg.fogB);
  g_objectRenderer.SetFogColor(fogCol);
  g_objectRenderer.SetFogRange(cfg.fogNear, cfg.fogFar);
  g_terrain.SetFogColor(fogCol);
  g_terrain.SetFogRange(cfg.fogNear, cfg.fogFar);
  g_grass.SetFogColor(fogCol);
  g_grass.SetFogRange(cfg.fogNear, cfg.fogFar);

  // Luminosity
  g_luminosity = cfg.luminosity;
  g_terrain.SetLuminosity(g_luminosity);
  g_objectRenderer.SetLuminosity(g_luminosity);
  g_hero.SetLuminosity(g_luminosity);
  g_npcManager.SetLuminosity(g_luminosity);
  g_monsterManager.SetLuminosity(g_luminosity);
  g_boidManager.SetLuminosity(g_luminosity);
  if (cfg.hasGrass)
    g_grass.SetLuminosity(g_luminosity);


  // Post-processing: apply per-map bloom/vignette/tint
  g_postProcess.bloomIntensity = cfg.bloomIntensity;
  g_postProcess.bloomThreshold = cfg.bloomThreshold;
  g_postProcess.vignetteStrength = cfg.vignetteStrength;
  g_postProcess.colorTint = glm::vec3(cfg.tintR, cfg.tintG, cfg.tintB);
  g_postProcess.gradingStrength = 0.3f;

  // Rebuild roof hiding maps for this map only (no cross-map bleed)
  g_typeAlpha.clear();
  g_typeAlphaTarget.clear();
  for (int i = 0; i < cfg.roofTypeCount; ++i) {
    g_typeAlpha[cfg.roofTypes[i]] = 1.0f;
    g_typeAlphaTarget[cfg.roofTypes[i]] = 1.0f;
  }

  // Set map ID on all subsystems
  g_objectRenderer.SetMapId(cfg.mapId);
  g_objectRenderer.SetVFXManager(&g_vfxManager);
  g_boidManager.SetMapId(cfg.mapId);
  g_monsterManager.SetMapId(cfg.mapId);
  g_npcManager.SetMapId(cfg.mapId);
  g_hero.SetMapId(cfg.mapId);
}

// ═══════════════════════════════════════════════════════════════════
// LoadWorld — centralized world loading. Called by both initial load
// and ChangeMap. Handles terrain, objects, grass, fire, lights.
// ═══════════════════════════════════════════════════════════════════

static void LoadWorld(int mapId, LoadProgressFn onProgress) {
  const MapConfig &cfg = *GetMapConfig(mapId);
  int fileWorldId = mapId + 1;
  std::string data_path = g_dataPath;

  if (onProgress) onProgress(0.0f, "Cleaning up...");

  // ── Cleanup old state (safe on first call — all are no-ops when empty) ──
  g_fireEffect.ClearEmitters();
  g_objectRenderer.Cleanup();
  g_grass.Cleanup();

  if (onProgress) onProgress(0.05f, "Loading terrain...");

  // ── Load terrain ──
  g_terrainDataOwned = std::make_unique<TerrainData>(
      TerrainParser::LoadWorld(fileWorldId, data_path));
  auto originalAttrs = g_terrainDataOwned->mapping.attributes;
  std::vector<bool> bridgeZone;
  ReconstructBridgeAttributes(*g_terrainDataOwned, cfg, bridgeZone);

  // Patch void (tile 255) AND water (tile 5) in bridge zone with nearest
  // valid ground tile. Water tiles left under the bridge cause visible bleed
  // at bridge edges due to bilinear interpolation in the terrain shader.
  // Lorencia-only: Devias/Dungeon bridges have different geometry that breaks
  // with this aggressive patching.
  if (!bridgeZone.empty() && cfg.mapId == 0) {
    auto &l1 = g_terrainDataOwned->mapping.layer1;
    auto &l2 = g_terrainDataOwned->mapping.layer2;
    for (int i = 0; i < 256 * 256; i++) {
      if (!bridgeZone[i]) continue;
      bool l1Bad = (l1[i] >= 255 || l1[i] == 5);
      bool l2Bad = (l2[i] >= 255 || l2[i] == 5);
      if (!l1Bad && !l2Bad) continue;
      int bz = i / 256, bx = i % 256;
      uint8_t best1 = l1[i], best2 = l2[i];
      float bestDist = 999.0f;
      for (int dz = -10; dz <= 10; ++dz) {
        for (int dx = -10; dx <= 10; ++dx) {
          int nz = bz + dz, nx = bx + dx;
          if (nz < 0 || nz >= 256 || nx < 0 || nx >= 256) continue;
          int ni = nz * 256 + nx;
          if (bridgeZone[ni] || l1[ni] >= 255 || l1[ni] == 5) continue;
          float d = std::sqrt((float)(dz * dz + dx * dx));
          if (d < bestDist) { bestDist = d; best1 = l1[ni]; best2 = l2[ni]; }
        }
      }
      if (bestDist < 999.0f) {
        if (l1Bad) l1[i] = best1;
        if (l2Bad) l2[i] = best2;
      }
    }
  }

  // Fix bridge zone lightmap: the JPEG has dark/black values at void areas
  // where bridges sit. Sample brightness from nearest non-void terrain and
  // fill bridge zone cells so bridges, characters, and objects are properly lit.
  // Must happen BEFORE g_terrainDataPtr assignment so all subsystems get the fix.
  // Lorencia-only: other maps don't need this aggressive lightmap override.
  if (!bridgeZone.empty() && !g_terrainDataOwned->lightmap.empty() && cfg.mapId == 0) {
    const int S = 256;
    auto &lm = g_terrainDataOwned->lightmap;
    // Compute distance-to-bridge for each cell
    std::vector<float> bDist(S * S, 999.0f);
    for (int i = 0; i < S * S; i++)
      if (bridgeZone[i]) bDist[i] = 0.0f;
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++) {
        int i = y * S + x;
        if (x > 0) bDist[i] = std::min(bDist[i], bDist[i-1] + 1.0f);
        if (y > 0) bDist[i] = std::min(bDist[i], bDist[i-S] + 1.0f);
      }
    for (int y = S-1; y >= 0; y--)
      for (int x = S-1; x >= 0; x--) {
        int i = y * S + x;
        if (x < S-1) bDist[i] = std::min(bDist[i], bDist[i+1] + 1.0f);
        if (y < S-1) bDist[i] = std::min(bDist[i], bDist[i+S] + 1.0f);
      }
    // Restore bridge cell lightmap: void cells skipped the 0.30 clamp in
    // TerrainParser (because they have TW_NOGROUND), but bridge cells over
    // void need normal brightness. Sample avg from nearby non-void terrain,
    // ensure a minimum floor of 0.50, and apply to bridge zone + neighbors.
    glm::vec3 sum(0.0f);
    int cnt = 0;
    for (int i = 0; i < S * S; i++) {
      if (!bridgeZone[i] && !(originalAttrs[i] & 0x08) && bDist[i] < 6.0f) {
        sum += lm[i];
        cnt++;
      }
    }
    glm::vec3 avg = (cnt > 0) ? sum / (float)cnt : glm::vec3(0.50f);
    avg = glm::max(avg, glm::vec3(0.80f)); // floor: bridges must be well-lit walkable areas
    for (int i = 0; i < S * S; i++) {
      if (bDist[i] < 8.0f)
        lm[i] = glm::max(lm[i], avg);
    }
  }

  if (onProgress) onProgress(0.12f, "Preparing terrain...");

  g_terrainDataPtr = g_terrainDataOwned.get();

  if (onProgress) onProgress(0.15f, "Uploading terrain...");

  g_terrain.Load(*g_terrainDataPtr, fileWorldId, data_path, originalAttrs,
                 bridgeZone);
  GenerateMinimapTexture();
  std::cout << "[LoadWorld] Map " << fileWorldId << " (" << cfg.regionName
            << "): " << g_terrainDataPtr->heightmap.size() << " height samples, "
            << g_terrainDataPtr->objects.size() << " objects" << std::endl;

  if (onProgress) onProgress(0.25f, "Preparing objects...");

  if (onProgress) onProgress(0.30f, "Loading objects...");

  // ── Load objects (config-driven path selection) ──
  g_objectRenderer.Init();
  g_objectRenderer.SetMapId(cfg.mapId);
  g_objectRenderer.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_objectRenderer.SetLightmapTexture(g_terrain.GetLightmapTexture());
  g_objectRenderer.SetTerrainMapping(&g_terrainDataPtr->mapping);
  g_objectRenderer.SetTerrainHeightmap(g_terrainDataPtr->heightmap);
  if (cfg.useNamedObjects) {
    std::string object1_path = data_path + "/Object1";
    g_objectRenderer.LoadObjects(g_terrainDataPtr->objects, object1_path);
  } else {
    std::string objectN_path =
        data_path + "/Object" + std::to_string(fileWorldId);
    std::string object1_path = data_path + "/Object1";
    g_objectRenderer.LoadObjectsGeneric(g_terrainDataPtr->objects, objectN_path,
                                        object1_path);
  }

  if (onProgress) onProgress(0.50f, "Setting up objects...");

  if (onProgress) onProgress(0.55f, "Loading grass...");

  // ── Grass + doors ──
  // Main 5.2: grass placement uses only baked .map data (layer1 + alpha).
  // No runtime object occupancy — rocks occlude grass via depth buffer.
  g_grass.Init();
  if (cfg.hasGrass) {
    g_grass.Load(*g_terrainDataPtr, fileWorldId, data_path);
  }
  if (cfg.hasDoors)
    g_objectRenderer.InitDoors();

  if (onProgress) onProgress(0.65f, "Preparing lighting...");

  if (onProgress) onProgress(0.70f, "Setting up lighting...");

  // ── Register fire/smoke emitters from world objects ──
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type, mapId);
    for (auto &off : offsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      // Type 24 on Lost Tower: tall rising fire column, all others: stationary fire
      bool isColumn = (mapId == 4 && inst.type == 24);
      if (isColumn)
        g_fireEffect.AddColumnEmitter(worldPos + rot * off);
      else
        g_fireEffect.AddEmitter(worldPos + rot * off);
    }
  }
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &smokeOffsets = GetSmokeOffsets(inst.type, mapId);
    for (auto &off : smokeOffsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddSmokeEmitter(worldPos + rot * off);
    }
    if (inst.type == 105) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      g_fireEffect.AddWaterSmokeEmitter(worldPos + glm::vec3(0, 180, 0));
      g_fireEffect.AddWaterSmokeEmitter(worldPos + glm::vec3(0, 120, 0));
    }
  }

  // ── Collect point lights + register ambient fire emitters ──
  g_pointLights.clear();
  g_vfxManager.ClearAmbientFires();
  g_vfxManager.SetCameraPos(g_camera.GetPosition());
  for (auto &inst : g_objectRenderer.GetInstances()) {
    const LightTemplate *props = GetLightProperties(inst.type, mapId);
    if (!props)
      continue;
    glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
    PointLight light;
    light.position = worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f);
    light.color = props->color;
    light.range = props->range;
    light.objectType = inst.type;
    g_pointLights.push_back(light);

    // Register fire emitters (BlendMesh glow replaced by VFX particles)
    // Main 5.2: Only types with CreateFire() calls get fire particles.
    // Type 150 (candle) only does AddTerrainLight, no CreateFire.
    // Lost Tower type 19 uses rotating magic sprite, not fire.
    bool isFire = (inst.type == 50 || inst.type == 51 || inst.type == 52 ||
                   (mapId == 1 && (inst.type == 41 || inst.type == 42)) ||
                   (mapId == 4 && (inst.type == 3 || inst.type == 4 || inst.type == 24)));
    if (isFire) {
      float intensity = (inst.type == 52)  ? 1.2f :   // bonfire
                        (mapId == 4 && inst.type == 24) ? 1.5f : // fire column
                        0.7f;                          // torch
      g_vfxManager.AddAmbientFire(
          worldPos + glm::vec3(0.0f, props->heightOffset, 0.0f), intensity);
    }
  }
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

  if (onProgress) onProgress(0.90f, "Finalizing...");

  // ── RayPicker (terrain picking) ──
  RayPicker::Init(g_terrainDataPtr, &g_camera, &g_npcManager, &g_monsterManager,
                  g_groundItems, MAX_GROUND_ITEMS, &g_objectRenderer);

  // ── Apply atmosphere (clear color, fog, luminosity, post-proc) ──
  g_currentMapId = mapId;
  ApplyMapAtmosphere(cfg);

  std::cout << "[LoadWorld] Ready: " << g_objectRenderer.GetInstanceCount()
            << " objects, " << g_pointLights.size() << " lights, "
            << g_fireEffect.GetEmitterCount() << " fire emitters" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// ChangeMap — called when server sends MAP_CHANGE packet (0x1C)
// ═══════════════════════════════════════════════════════════════════

static void ChangeMap(uint8_t mapId, uint8_t spawnX, uint8_t spawnY,
                      LoadProgressFn onProgress) {
  std::cout << "[ChangeMap] Transitioning to map " << (int)mapId << " ("
            << GetMapConfig(mapId)->regionName << ") spawn=("
            << (int)spawnX << "," << (int)spawnY << ")" << std::endl;

  g_hero.StopMoving();
  g_hero.CancelAttack();
  g_monsterManager.ClearMonsters();
  g_npcManager.ClearSpawnedNpcs();
  for (int i = 0; i < MAX_GROUND_ITEMS; i++)
    g_groundItems[i].active = false;

  // Use caller's progress callback if provided, otherwise self-contained
  LoadProgressFn lwProgress = onProgress ? onProgress : [](float p, const char *s) {
    RenderLoadingFrame(p, s);
  };
  LoadWorld(mapId, lwProgress);

  // Re-point subsystems to new terrain data
  g_hero.SetTerrainData(g_terrainDataPtr);
  g_hero.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_hero.SetPointLights(g_pointLights);
  g_npcManager.SetTerrainData(g_terrainDataPtr);
  g_npcManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_npcManager.SetPointLights(g_pointLights);
  g_monsterManager.SetTerrainData(g_terrainDataPtr);
  g_monsterManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_monsterManager.SetPointLights(g_pointLights);
  g_boidManager.SetTerrainData(g_terrainDataPtr);
  g_boidManager.SetTerrainLightmap(g_terrainDataPtr->lightmap);
  g_boidManager.SetPointLights(g_pointLights);
  g_clickEffect.SetTerrainData(g_terrainDataPtr);

  // Place hero at spawn position
  float heroX = (float)spawnY * 100.0f;
  float heroZ = (float)spawnX * 100.0f;
  g_hero.SetPosition(glm::vec3(heroX, 0.0f, heroZ));
  g_hero.SnapToTerrain();
  g_camera.SetPosition(g_hero.GetPosition());

  // Determine floor number and show floor-specific region name
  g_currentFloor = DetermineFloor(mapId, spawnX, spawnY);
  InventoryUI::ShowRegionName(GetFloorDisplayName());

  // Sound/music transition — stop old sounds immediately, but defer new sounds
  // until preloader fully closes to avoid audio playing over loading screen
  SoundManager::StopAll();
  SoundManager::StopMusic();
  g_windStarted = false;
  g_deferredSoundMapId = mapId; // Deferred: play after preloader closes

}
