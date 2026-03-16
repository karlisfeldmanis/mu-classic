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
static constexpr uint16_t SHADOW_MAP_SIZE = 2048;

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

// Client-side item definitions (owned by ItemDatabase, reference here)
static auto &g_itemDefs = ItemDatabase::GetItemDefs();

// ── Floating damage numbers (type in GroundItemRenderer.hpp) ──
static FloatingDamage g_floatingDmg[MAX_FLOATING_DAMAGE] = {};

// Ground item drops (type in ClientTypes.hpp)
static GroundItem g_groundItems[MAX_GROUND_ITEMS] = {};
static const std::string g_dataPath = "Data";

// Current map ID (0=Lorencia, 1=Dungeon, 2=Devias). File world = mapId + 1.
static int g_currentMapId = 0;
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
  bool hasDungeonTraps;

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
        0.5f,
        0.35f,
        0.15f, // bloom, threshold, vignette
        1.02f,
        1.0f,
        0.96f, // colorTint (warm)
        true,
        true,
        false,
        true,
        true,
        false, // sky, grass, doors, leaves, wind, traps
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
        1.2f, // fogNear, fogFar, luminosity
        0.7f,
        0.25f,
        0.4f, // bloom, threshold, vignette
        0.88f,
        0.93f,
        1.08f, // colorTint (cool blue)
        false,
        false,
        false,
        false,
        false,
        true, // sky, grass, doors, leaves, wind, traps
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
        {80, 85},
        2, // bridgeTypes (Bridge, BridgeStone)
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
        0.45f,
        0.4f,
        0.1f, // bloom, threshold, vignette
        0.92f,
        0.96f,
        1.08f, // colorTint (cool ice)
        false,
        true,
        true,
        true,
        true,
        false, // sky, grass, doors, leaves, wind, traps
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
        {80},
        1, // bridgeTypes (MODEL_BRIDGE, same as Lorencia)
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
    // Bridge01.bmd is ~800 units long, ~300 wide → generous radius + height
    int rGZ = spanAlongGZ ? 8 : 5;
    int rGX = spanAlongGZ ? 5 : 8;

    for (int dz = -rGZ; dz <= rGZ; ++dz) {
      for (int dx = -rGX; dx <= rGX; ++dx) {
        int cz = gz + dz, cx = gx + dx;
        if (cz < 0 || cz >= S || cx < 0 || cx >= S) continue;
        int idx = cz * S + cx;
        float h = td.heightmap[idx];
        if (std::abs(h - obj.position.y) < 200.0f) {
          td.mapping.attributes[idx] |= 0x08;   // TW_NOGROUND
          td.mapping.attributes[idx] &= ~0x10;   // clear TW_WATER
          bridgeZone[idx] = true;                 // mark for mesh protection
        } else {
          td.mapping.attributes[idx] |= 0x10;    // TW_WATER
        }
        count++;
      }
    }
  }
  int zoneCount = 0;
  for (int i = 0; i < S * S; i++) if (bridgeZone[i]) zoneCount++;
  if (count > 0)
    std::cout << "[Bridge] Patched " << count << " attribute cells, bridgeZone=" << zoneCount << std::endl;
}

// Apply atmosphere/rendering settings from MapConfig to all subsystems
static void ApplyMapAtmosphere(const MapConfig &cfg);
// Forward declaration — defined after g_grass, g_hero etc. are declared

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
static int16_t g_potionBar[4] = {850, 851, 852,
                                 -1}; // Apple, SmallHP, MedHP, (empty)
static int8_t g_skillBar[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static float g_potionCooldown = 0.0f; // Potion cooldown timer (seconds)
static constexpr float POTION_COOLDOWN_TIME = 30.0f;
static bool g_shopOpen = false;
static bool g_showGameMenu = false;
static std::vector<ShopItem> g_shopItems;
static bool g_questDialogOpen = false;
static bool g_questDialogWasOpen =
    false; // Track previous frame to detect fresh open
static bool g_questDialogJustOpened = false; // Skip clicks on first frame
static int g_questDialogNpcIndex = -1;
static int g_questDialogSelected =
    -1; // -1 = quest list view, 0-4 = viewing specific quest
static bool g_showQuestLog = false; // L key quest log window
static bool g_mouseOverUIPanel =
    false; // Set each frame: true if cursor is over any UI panel
static bool g_showCommandTerminal = false;
static char g_commandBuffer[128] = {};
static bool g_commandFocusNeeded =
    false; // Set true when terminal opens to grab focus
// Stored panel bounds for quest dialog and quest log (updated during render)
static float g_qdPanelRect[4] = {}; // x, y, w, h
static float g_qlPanelRect[4] = {}; // x, y, w, h

// Quest system state (synced from server)
static int g_questIndex = 0; // Active quest for current map
static int g_questKillCount[3] = {};
static int g_questRequired[3] = {};
static int g_questTargetCount = 0;
static bool g_questAccepted = false;
static int g_deviasQuestIndex = 12; // Devias chain (12-17), 18=done

// Quest definitions (client mirror of server QuestHandler data)
// 9 quests: 5 kill + 4 travel, linear chain across 5 guards
struct QuestTarget {
  uint8_t monType;
  uint8_t killsReq;
  const char *name;
};
struct QuestRewardItem {
  int16_t defIndex;
  uint8_t itemLevel;
}; // -1 = no item
struct QuestClientInfo {
  uint16_t guardType;
  uint8_t questType; // 0=kill, 1=travel
  int targetCount;
  QuestTarget targets[3];
  const char *loreText;
  const char *questName;
  const char *location; // e.g. "Lorencia", "Dungeon 2"
  uint8_t recommendedLevel;
  uint32_t zenReward;
  uint32_t xpReward;
  QuestRewardItem dkReward;     // DK weapon reward
  QuestRewardItem dwReward;     // DW weapon reward
  QuestRewardItem orbReward;    // DK skill orb
  QuestRewardItem scrollReward; // DW spell scroll
};
static constexpr int QUEST_COUNT = 18;
static const QuestClientInfo g_questDefs[QUEST_COUNT] = {
    // Quest 0 (Kill): Lieutenant Kael — Spider + Budge Dragon
    // DK: Kris +0, DW: Skull Staff +0, Orb: Falling Slash, Scroll: Fire Ball
    {248,
     0,
     2,
     {{3, 10, "Spider"}, {2, 5, "Budge Dragon"}, {}},
     "Welcome, adventurer. Spiders crawl\n"
     "through the sewers and Budge Dragons\n"
     "terrorize the farmers. Clear out 10\n"
     "Spiders and 5 Budge Dragons, and I'll\n"
     "see you're properly equipped.",
     "Clearing the Outskirts",
     "Lorencia",
     3,
     5000,
     60000,
     {0, 0},
     {160, 0},
     {404, 0},
     {483, 0}},
    // Quest 1 (Travel): → Corporal Brynn
    {246,
     1,
     0,
     {{}, {}, {}},
     "Well done. Take these weapons — you've\n"
     "earned them. Head to Corporal Brynn\n"
     "near the potion shop. She's dealing\n"
     "with bigger threats.",
     "Report to Corporal Brynn",
     "Lorencia",
     5,
     3000,
     30000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 2 (Kill): Corporal Brynn — Bull Fighter + Hound
    // DK: Short Sword +2, DW: Skull Staff +2, Orb: Lunge, Scroll: Lightning
    {246,
     0,
     2,
     {{0, 8, "Bull Fighter"}, {1, 6, "Hound"}, {}},
     "Kael sent you? Good. Bull Fighters\n"
     "block the eastern road and Hounds\n"
     "roam the plains at night. Slay 8 Bull\n"
     "Fighters and 6 Hounds to secure the area.",
     "Defending the Roads",
     "Lorencia",
     8,
     10000,
     100000,
     {1, 2},
     {160, 2},
     {405, 0},
     {482, 0}},
    // Quest 3 (Travel): → Sergeant Dorian
    {247,
     1,
     0,
     {{}, {}, {}},
     "Impressive work. Sergeant Dorian in\n"
     "the center of town needs help. Tell\n"
     "him Brynn sent you.",
     "Report to Sergeant Dorian",
     "Lorencia",
     10,
     5000,
     50000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 4 (Kill): Sergeant Dorian — Elite BF + Lich
    // DK: Rapier +0, DW: Skull Staff +4, Orb: Uppercut, Scroll: Teleport
    {247,
     0,
     2,
     {{4, 6, "Elite Bull Fighter"}, {6, 5, "Lich"}, {}},
     "Brynn's recruit? Let's see what you've\n"
     "got. Elite Bull Fighters lead raiding\n"
     "parties and Liches strike from the\n"
     "ruins. Eliminate 6 Elite Bull Fighters\n"
     "and 5 Liches.",
     "The Elite Vanguard",
     "Lorencia",
     15,
     15000,
     130000,
     {2, 0},
     {160, 4},
     {406, 0},
     {485, 0}},
    // Quest 5 (Travel): → Warden Aldric
    {245,
     1,
     0,
     {{}, {}, {}},
     "You fight well. Warden Aldric on\n"
     "the west wall guards the frontier.\n"
     "Go lend him your blade.",
     "Report to Warden Aldric",
     "Lorencia",
     18,
     8000,
     80000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 6 (Kill): Warden Aldric — Giant
    // DK: Katana +0, DW: Angelic Staff +0, Orb: Cyclone, Scroll: Meteorite
    {245,
     0,
     1,
     {{7, 5, "Giant"}, {}, {}},
     "Dorian speaks highly of you. Giants\n"
     "crush our watchtowers. Their strength\n"
     "is immense. Fell 5 of them before they\n"
     "reach the walls.",
     "Watchtower Defense",
     "Lorencia",
     20,
     25000,
     200000,
     {3, 0},
     {161, 0},
     {407, 0},
     {481, 0}},
    // Quest 7 (Travel): → Captain Marcus
    {249,
     1,
     0,
     {{}, {}, {}},
     "You're stronger than most soldiers.\n"
     "Captain Marcus at the south gate tracks\n"
     "the undead command. Report to him.",
     "Report to Captain Marcus",
     "Lorencia",
     23,
     10000,
     100000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 8 (Kill): Captain Marcus — Skeleton Warrior + Lich
    // DK: Gladius +0, DW: Angelic Staff +3, Orb: Slash, Scroll: Ice
    {249,
     0,
     2,
     {{14, 5, "Skeleton Warrior"}, {6, 4, "Lich"}, {}},
     "So you're the one clearing the roads.\n"
     "Skeleton Warriors march from the\n"
     "cemetery and Liches command them from\n"
     "the ruins. Destroy 5 Warriors and 4\n"
     "Liches, and I'll give you the finest\n"
     "weapons in our armory.",
     "The Final Stand",
     "Lorencia",
     25,
     50000,
     350000,
     {6, 0},
     {161, 3},
     {408, 0},
     {486, 0}},
    // Quest 9 (Kill): Marcus — Skeleton Warrior + Larva (Dungeon 1 entrance)
    // DK: Small Axe +0, DW: Serpent Staff +0, Orb: Twisting Slash, Scroll:
    // Flame
    {249,
     0,
     2,
     {{14, 10, "Skeleton Warrior"}, {12, 8, "Larva"}, {}},
     "The dungeon beneath Lorencia festers\n"
     "with undead. Skeleton Warriors and\n"
     "Larvae infest the entrance corridors.\n"
     "Descend and destroy 10 Skeleton\n"
     "Warriors and 8 Larvae.",
     "Into the Depths",
     "Dungeon 1",
     30,
     60000,
     400000,
     {5, 0},
     {162, 0},
     {391, 0},
     {484, 0}},
    // Quest 10 (Kill): Marcus — Elite Skeleton + Cyclops (Dungeon 2)
    // DK: Falchion +2, DW: Thunder Staff +0, Orb: Twisting Slash, Scroll:
    // Twister
    {249,
     0,
     2,
     {{16, 8, "Elite Skeleton"}, {17, 6, "Cyclops"}, {}},
     "The second level holds deadlier foes.\n"
     "Elite Skeletons command from the deep\n"
     "corridors and Cyclops crush all who\n"
     "enter. Slay 8 Elite Skeletons and\n"
     "6 Cyclops.",
     "The Deep Corridors",
     "Dungeon 2",
     40,
     80000,
     500000,
     {7, 2},
     {163, 0},
     {391, 0},
     {487, 0}},
    // Quest 11 (Kill): Marcus — Ghost + Gorgon (Dungeon 3)
    // DK: Blade +3, DW: Gorgon Staff +0, Orb: Twisting Slash, Scroll: Evil
    // Spirit
    {249,
     0,
     2,
     {{11, 10, "Ghost"}, {18, 1, "Gorgon"}, {}},
     "At the dungeon's heart lurks the Gorgon\n"
     "-- a creature of terrible power, guarded\n"
     "by restless Ghosts. Banish 10 Ghosts\n"
     "and slay the Gorgon itself.",
     "Heart of Darkness",
     "Dungeon 3",
     55,
     100000,
     700000,
     {8, 3},
     {164, 0},
     {391, 0},
     {488, 0}},
    // ── Devias quest chain (12-17) ──
    // Quest 12 (Kill): Ranger Elise — Worm + Assassin
    // DK: Katana +2, DW: Angelic Staff +2, Orb: Impale, Scroll: Poison
    {310,
     0,
     2,
     {{24, 10, "Worm"}, {21, 8, "Assassin"}, {}},
     "The frozen wilds of Devias are overrun.\n"
     "Worms burrow beneath the snow and\n"
     "Assassins stalk the trade routes. Clear\n"
     "out 10 Worms and 8 Assassins to\n"
     "secure the passage.",
     "Securing the Trade Route",
     "Devias",
     20,
     30000,
     200000,
     {3, 2},
     {161, 2},
     {397, 0},
     {480, 0}},
    // Quest 13 (Travel): → Tracker Nolan
    {311,
     1,
     0,
     {{}, {}, {}},
     "The trade route is safer now. Tracker\n"
     "Nolan scouts the western hunting grounds.\n"
     "Find him — he's tracking something\n"
     "dangerous.",
     "Report to Tracker Nolan",
     "Devias",
     22,
     15000,
     100000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 14 (Kill): Tracker Nolan — Hommerd + Assassin
    // DK: Gladius +2, DW: Serpent Staff +1, Orb: Twisting Slash, Scroll:
    // Teleport
    {311,
     0,
     2,
     {{23, 8, "Hommerd"}, {21, 6, "Assassin"}, {}},
     "Elise sent you? Good timing. Hommerds\n"
     "and Assassins control the central plains.\n"
     "They ambush travelers and raid supply\n"
     "wagons. Slay 8 Hommerds and 6 Assassins.",
     "Clearing the Plains",
     "Devias",
     25,
     50000,
     300000,
     {6, 2},
     {162, 1},
     {391, 0},
     {485, 0}},
    // Quest 15 (Travel): → Warden Hale
    {312,
     1,
     0,
     {{}, {}, {}},
     "You fight like a veteran. Warden Hale\n"
     "patrols the frontier where the Elite\n"
     "Yetis gather. He needs reinforcements.",
     "Report to Warden Hale",
     "Devias",
     28,
     20000,
     120000,
     {-1, 0},
     {-1, 0},
     {-1, 0},
     {-1, 0}},
    // Quest 16 (Kill): Warden Hale — Elite Yeti
    // DK: Serpent Sword +1, DW: Thunder Staff +0, Orb: Twisting Slash, Scroll:
    // Ice
    {312,
     0,
     1,
     {{20, 16, "Elite Yeti"}, {}, {}},
     "The Elite Yetis have grown dangerous.\n"
     "They terrorize the southern reaches,\n"
     "attacking anyone who wanders too far.\n"
     "Slay 16 of these beasts.",
     "The Elite Yeti Menace",
     "Devias",
     32,
     70000,
     450000,
     {8, 1},
     {163, 0},
     {391, 0},
     {486, 0}},
    // Quest 17 (Kill): Warden Hale — Ice Queen (boss)
    // DK: Light Saber +0, DW: Gorgon Staff +0, Orb: Greater Fortitude, Scroll:
    // Twister
    {312,
     0,
     1,
     {{25, 1, "Ice Queen"}, {}, {}},
     "Deep in the frozen wastes, the Ice Queen\n"
     "commands all creatures of Devias. She is\n"
     "the source of this endless winter. Only\n"
     "the strongest warriors survive her wrath.\n"
     "Destroy her.",
     "The Ice Queen",
     "Devias",
     50,
     120000,
     800000,
     {10, 0},
     {164, 0},
     {398, 0},
     {487, 0}},
};

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
  default:
    return "Guard";
  }
}

// Find the kill quest index owned by a specific guard type (-1 if none)
static int GetGuardKillQuest(uint16_t guardType) {
  for (int i = 0; i < QUEST_COUNT; i++) {
    if (g_questDefs[i].guardType == guardType && g_questDefs[i].questType == 0)
      return i;
  }
  return -1;
}

// Find the travel quest index that targets a specific guard type (-1 if none)
static int GetTravelQuestTo(uint16_t guardType) {
  for (int i = 0; i < QUEST_COUNT; i++) {
    if (g_questDefs[i].guardType == guardType && g_questDefs[i].questType == 1)
      return i;
  }
  return -1;
}

// Devias quest chain boundary
static constexpr int DEVIAS_QUEST_START = 12;

// Is this a Devias quest guard?
static bool IsDeviasGuard(uint16_t guardType) {
  return guardType == 310 || guardType == 311 || guardType == 312;
}

// Get the active quest index for a given chain
// For Lorencia guards: use g_questIndex (quests 0-11)
// For Devias guards: use g_deviasQuestIndex (quests 12-17)
static int GetActiveQuestForGuard(uint16_t guardType) {
  return IsDeviasGuard(guardType) ? g_deviasQuestIndex : g_questIndex;
}

// Get the chain-appropriate quest index for determining status of quest qi
static int GetChainIndex(int qi) {
  return (qi >= DEVIAS_QUEST_START) ? g_deviasQuestIndex : g_questIndex;
}

// Draw a weapon reward item box with 3D model + name label
// Returns the height consumed (0 if no valid reward)
static float DrawQuestRewardItem(ImDrawList *dl, float px, float cy,
                                 float panelW, const QuestRewardItem &reward,
                                 const char *classLabel) {
  if (reward.defIndex < 0)
    return 0;
  auto &itemDefs = ItemDatabase::GetItemDefs();
  auto it = itemDefs.find(reward.defIndex);
  if (it == itemDefs.end())
    return 0;

  const auto &def = it->second;
  float boxSize = 44.0f;
  float boxX = px + 22;
  float boxY = cy;

  // Hover detection
  ImVec2 mPos = ImGui::GetIO().MousePos;
  bool hovered = mPos.x >= boxX && mPos.x <= boxX + boxSize && mPos.y >= boxY &&
                 mPos.y <= boxY + boxSize;

  // Dark slot background with hover highlight
  dl->AddRectFilled(
      ImVec2(boxX, boxY), ImVec2(boxX + boxSize, boxY + boxSize),
      hovered ? IM_COL32(35, 32, 25, 220) : IM_COL32(20, 18, 14, 200), 3.0f);
  dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxSize, boxY + boxSize),
              hovered ? IM_COL32(100, 90, 60, 220) : IM_COL32(60, 55, 40, 180),
              3.0f, 0, 1.0f);

  // Queue 3D model render job (hovered = spin)
  const char *modelName = def.modelFile.empty()
                              ? ItemDatabase::GetDropModelName(reward.defIndex)
                              : def.modelFile.c_str();
  if (modelName && modelName[0]) {
    int winH = (int)ImGui::GetIO().DisplaySize.y;
    int questItemY = (int)boxY; // BGFX: top-left origin
    InventoryUI::AddRenderJob({modelName, reward.defIndex, (int)boxX,
                               questItemY, (int)boxSize,
                               (int)boxSize, hovered});
  }

  // Tooltip on hover
  if (hovered)
    InventoryUI::AddPendingItemTooltip(reward.defIndex, reward.itemLevel);

  // Item name + level text to the right of the box
  float textX = boxX + boxSize + 10;
  float textY = boxY + 6;
  char nameBuf[64];
  if (reward.itemLevel > 0)
    snprintf(nameBuf, sizeof(nameBuf), "%s +%d", def.name.c_str(),
             reward.itemLevel);
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", def.name.c_str());
  dl->AddText(ImVec2(textX, textY),
              hovered ? IM_COL32(140, 230, 255, 255)
                      : IM_COL32(100, 200, 255, 255),
              nameBuf);

  // Class label beneath name
  dl->AddText(ImVec2(textX, textY + 16), IM_COL32(140, 135, 120, 200),
              classLabel);

  return boxSize + 4; // height consumed
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

// Town teleport state
static bool g_teleportingToTown = false;
static float g_teleportTimer = 0.0f;
static constexpr float TELEPORT_CAST_TIME = 2.5f; // Seconds of heal anim

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
static TexHandle g_loadingTex = kInvalidTex; // Loading screen texture

// ── Map transition preloader (fullscreen overlay during ChangeMap) ──
static bool g_mapTransitionActive = false;
static int g_mapTransitionPhase = 0; // 0=fade-in, 1=do work, 2=fade-out
static float g_mapTransitionAlpha = 0.0f;
static int g_mapTransitionFrames = 0; // Frames rendered in current phase
static uint8_t g_mapTransMapId = 0;
static uint8_t g_mapTransSpawnX = 0;
static uint8_t g_mapTransSpawnY = 0;

// ── Shadow map state (BGFX) ──
struct ShadowMapState {
  bgfx::TextureHandle colorTex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle depthTex = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
  std::unique_ptr<Shader> depthShader;
};
static ShadowMapState g_shadowMap;

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
  float bloomIntensity = 0.5f;
  float vignetteStrength = 0.15f;
  glm::vec3 colorTint = glm::vec3(1.02f, 1.0f, 0.96f);
  float bloomThreshold = 0.35f;
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
  std::cout << "[PostProcess] Initialized (bloom + vignette + color grading)\n";
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
  bgfx::setViewFrameBuffer(PP_VIEW_COMPOSITE, BGFX_INVALID_HANDLE);
  pp.composite->setTexture(0, "s_scene", pp.sceneColorTex);
  pp.composite->setTexture(1, "s_bloom", pp.bloomTex[0]);
  pp.composite->setVec4("u_ppComposite", glm::vec4(pp.bloomIntensity, pp.vignetteStrength, 0, 0));
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

// Build object occupancy grid for grass exclusion
static std::vector<bool>
BuildObjectOccupancy(const std::vector<ObjectRenderer::ObjectInstance> &insts) {
  std::vector<bool> occ(256 * 256, false);
  for (auto &inst : insts) {
    glm::vec3 p = glm::vec3(inst.modelMatrix[3]);
    int cx = (int)(p.x / 100.0f);
    int cz = (int)(p.z / 100.0f);
    int r = 1;
    if (inst.type >= 115 && inst.type <= 129)
      r = 3; // Buildings/walls
    else if (inst.type >= 30 && inst.type <= 46)
      r = 2; // Stones/statues/tombs
    else if (inst.type >= 65 && inst.type <= 85)
      r = 2; // Walls/bridges/fences
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        int gz = cz + dz, gx = cx + dx;
        if (gz >= 0 && gz < 256 && gx >= 0 && gx < 256)
          occ[gz * 256 + gx] = true;
      }
  }
  return occ;
}

// Returns light properties for a given object type, or nullptr if not a light
static const LightTemplate *GetLightProperties(int type, int mapId = -1) {
  static const LightTemplate fireLightProps = {glm::vec3(1.5f, 0.9f, 0.5f),
                                               800.0f, 150.0f};
  static const LightTemplate bonfireProps = {glm::vec3(1.5f, 0.75f, 0.3f),
                                             1000.0f, 100.0f};
  static const LightTemplate gateProps = {glm::vec3(1.5f, 0.9f, 0.5f), 800.0f,
                                          200.0f};
  static const LightTemplate bridgeProps = {glm::vec3(1.2f, 0.7f, 0.4f), 700.0f,
                                            50.0f};
  static const LightTemplate streetLightProps = {glm::vec3(0.8f, 0.65f, 0.4f),
                                                 450.0f, 250.0f};
  static const LightTemplate candleProps = {glm::vec3(1.2f, 0.7f, 0.3f), 600.0f,
                                            80.0f};
  static const LightTemplate lightFixtureProps = {glm::vec3(1.2f, 0.85f, 0.5f),
                                                  700.0f, 150.0f};

  // Dungeon torches (Main 5.2: tall fire stand + standard lantern)
  static const LightTemplate dungeonTorchProps = {glm::vec3(1.4f, 0.8f, 0.4f),
                                                  700.0f, 200.0f};
  // Lance Trap (type 100): blue lightning glow (Main 5.2: BITMAP_LIGHTNING)
  static const LightTemplate lanceTrapProps = {glm::vec3(0.4f, 0.6f, 1.5f),
                                               500.0f, 50.0f};

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
  case 39: // Lance Trap (dungeon) — blue lightning glow
    return (mapId == 1) ? &lanceTrapProps : nullptr;
  case 150:
    return &candleProps;
  case 78: // StoneMuWall02 — torch/window glow (BlendMesh=3)
    return &streetLightProps;
  case 30: // Devias fireplace (Stone01) — warm fire glow
    return (mapId == 2) ? &fireLightProps : nullptr;
  case 66: // Devias wall fire (SteelWall02) — warm fire glow
    return (mapId == 2) ? &candleProps : nullptr;
  default:
    return nullptr;
  }
}

// ── Game world initialization (called after character select) ──
// Forward declared, defined after main() helpers
static void InitGameWorld(ServerData &serverData);
static void ChangeMap(uint8_t mapId, uint8_t spawnX, uint8_t spawnY);

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
  bgfxInit.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;
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
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOther(window, true);
  ImGui_ImplBgfx_Init(30, "shaders"); // View 30 = ImGui overlay

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
      g_fontRegion =
          io.Fonts->AddFontFromFileTTF(fontPath, 26.0f * contentScale);
    }
    if (!g_fontDefault)
      g_fontDefault = io.Fonts->AddFontDefault(&cfg);
    if (!g_fontBold)
      g_fontBold = g_fontDefault;
    if (!g_fontRegion)
      g_fontRegion = g_fontBold;

    io.Fonts->Build();
  }

  // Initialize modern HUD (centered at 70% scale)
  g_hudCoords.window = window;
  g_hudCoords.SetCenteredScale(0.7f);

  std::string hudAssetPath = "../lab-studio/modern-ui/assets";
  // --- Main Render Loop ---

  // Main Loop logic continues...

  MockData hudData = MockData::CreateDK50();

  // Load Terrain (initial map = Lorencia, mapId 0)
  std::string data_path = g_dataPath;
  const MapConfig &initCfg = *GetMapConfig(g_currentMapId);
  int initWorldId = g_currentMapId + 1;
  g_terrainDataOwned = std::make_unique<TerrainData>(
      TerrainParser::LoadWorld(initWorldId, data_path));
  TerrainData &terrainData = *g_terrainDataOwned;

  // Save original attributes BEFORE bridge reconstruction
  auto originalAttrs = terrainData.mapping.attributes;
  // Bridge reconstruction: modifies mapping.attributes for shader + outputs
  // bridgeZone (all cells near bridge height, for terrain mesh protection).
  std::vector<bool> bridgeZone;
  ReconstructBridgeAttributes(terrainData, initCfg, bridgeZone);

  // Patch tile 255 in bridge zone with nearest valid tile
  if (!bridgeZone.empty()) {
    auto &l1 = terrainData.mapping.layer1;
    auto &l2 = terrainData.mapping.layer2;
    for (int i = 0; i < 256 * 256; i++) {
      if (!bridgeZone[i] || (l1[i] < 255 && l2[i] < 255)) continue;
      int bz = i / 256, bx = i % 256;
      uint8_t best1 = l1[i], best2 = l2[i];
      float bestDist = 999.0f;
      for (int dz = -10; dz <= 10; ++dz) {
        for (int dx = -10; dx <= 10; ++dx) {
          int nz = bz + dz, nx = bx + dx;
          if (nz < 0 || nz >= 256 || nx < 0 || nx >= 256) continue;
          int ni = nz * 256 + nx;
          if (bridgeZone[ni] || l1[ni] >= 255) continue;
          float d = std::sqrt((float)(dz * dz + dx * dx));
          if (d < bestDist) { bestDist = d; best1 = l1[ni]; best2 = l2[ni]; }
        }
      }
      if (bestDist < 999.0f) {
        if (l1[i] >= 255) l1[i] = best1;
        if (l2[i] >= 255) l2[i] = best2;
      }
    }
  }

  // Make terrain data accessible for movement/height
  g_terrainDataPtr = &terrainData;
  RayPicker::Init(&terrainData, &g_camera, &g_npcManager, &g_monsterManager,
                  g_groundItems, MAX_GROUND_ITEMS, &g_objectRenderer);

  // Original attributes for mesh (genuine voids only). bridgeZone protects
  // bridge cells from being skipped/sunk even if originally void.
  g_terrain.Load(terrainData, initWorldId, data_path, originalAttrs,
                 bridgeZone);
  std::cout << "Loaded Map " << initWorldId << " (" << initCfg.regionName
            << "): " << terrainData.heightmap.size() << " height samples, "
            << terrainData.objects.size() << " objects" << std::endl;

  // Load world objects (config-driven path selection)
  g_objectRenderer.Init();
  g_objectRenderer.SetMapId(initCfg.mapId);
  g_objectRenderer.SetTerrainLightmap(terrainData.lightmap);
  g_objectRenderer.SetTerrainMapping(&terrainData.mapping);
  g_objectRenderer.SetTerrainHeightmap(terrainData.heightmap);
  if (initCfg.useNamedObjects) {
    std::string object1_path = data_path + "/Object1";
    g_objectRenderer.LoadObjects(terrainData.objects, object1_path);
  } else {
    std::string objectN_path =
        data_path + "/Object" + std::to_string(initWorldId);
    std::string object1_path = data_path + "/Object1";
    g_objectRenderer.LoadObjectsGeneric(terrainData.objects, objectN_path,
                                        object1_path);
  }
  checkGLError("object renderer load");
  std::cout << "[ObjectRenderer] Loaded " << terrainData.objects.size()
            << " object instances, " << g_objectRenderer.GetModelCount()
            << " unique models" << std::endl;
  g_grass.Init();
  if (initCfg.hasGrass) {
    auto occupancy = BuildObjectOccupancy(g_objectRenderer.GetInstances());
    g_grass.Load(terrainData, initWorldId, data_path, &occupancy);
  }
  checkGLError("grass load");

  // Initialize sky
  g_sky.Init(data_path + "/");
  checkGLError("sky init");

  // Initialize post-processing (bloom + vignette + color grading)
  InitPostProcess();
  // Initialize shadow map (BGRA8 color + D16 depth — depth shader writes gl_FragCoord.z to color)
  {
    g_shadowMap.colorTex = bgfx::createTexture2D(
        SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    g_shadowMap.depthTex = bgfx::createTexture2D(
        SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
        bgfx::TextureFormat::D16, BGFX_TEXTURE_RT_WRITE_ONLY);
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

  // Initialize fire effects and register emitters from fire-type objects
  g_fireEffect.Init(data_path + "/Effect");
  g_vfxManager.Init(data_path);
  g_vfxManager.SetTerrainHeightFunc(
      [](float x, float z) -> float { return g_terrain.GetHeight(x, z); });
  g_vfxManager.SetPlaySoundFunc(
      [](int soundId) { SoundManager::Play(soundId); });
  g_boidManager.Init(data_path);
  g_boidManager.SetTerrainData(&terrainData);
  checkGLError("fire init");
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type, g_currentMapId);
    for (auto &off : offsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      // Offsets are in MU model-local space. The model matrix rotation
      // (R_base * R_angles) maps MU Z→GL Y via the -90°Z/-90°Y base rotations,
      // so rot * off correctly transforms the MU offset to GL world space.
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
      g_fireEffect.AddEmitter(worldPos + rot * off);
    }
  }
  // Register smoke emitters for torch smoke objects (types 131, 132)
  // Main 5.2: CreateFire(1/2) on MODEL_LIGHT01+1/+2
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &smokeOffsets = GetSmokeOffsets(inst.type, g_currentMapId);
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
    const LightTemplate *props = GetLightProperties(inst.type, g_currentMapId);
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
  ChromeGlow::LoadTextures(g_dataPath);
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
    ctx.showQuestLog = &g_showQuestLog;
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
    ctx.learnSkillDuration = LEARN_SKILL_DURATION;
    ctx.mountToggling = &g_mountToggling;
    ctx.mountToggleTimer = &g_mountToggleTimer;
    ctx.mountToggleTime = MOUNT_TOGGLE_TIME;
    ctx.hero = &g_hero;
    ctx.server = &g_server;
    ctx.hudCoords = &g_hudCoords;
    ctx.fontDefault = g_fontDefault;
    ctx.fontRegion = g_fontRegion;
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
    inputCtx.objectRenderer = &g_objectRenderer;
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
    inputCtx.mouseOverUIPanel = &g_mouseOverUIPanel;
    inputCtx.showCommandTerminal = &g_showCommandTerminal;
    inputCtx.dataPath = data_path;
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
    gameState.questIndex = &g_questIndex;
    gameState.questKillCount = g_questKillCount;
    gameState.questRequired = g_questRequired;
    gameState.questTargetCount = &g_questTargetCount;
    gameState.deviasQuestIndex = &g_deviasQuestIndex;
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
      SoundManager::StopMusic();
      g_loadingFrames = 0;
      g_gameState = GameState::LOADING;
      // Load a random loading screen image
      if (!TexValid(g_loadingTex)) {
        int idx = (rand() % 3) + 1;
        char path[256];
        snprintf(path, sizeof(path), "%s/Logo/Loading%02d.OZJ",
                 data_path.c_str(), idx);
        g_loadingTex = TextureLoader::LoadOZJ(path);
        if (!TexValid(g_loadingTex)) {
          snprintf(path, sizeof(path), "%s/Local/loading%02d.ozj",
                   data_path.c_str(), idx);
          g_loadingTex = TextureLoader::LoadOZJ(path);
        }
      }
      std::cout << "[State] -> LOADING (waiting for world data)" << std::endl;
    };
    csCtx.onExit = [&]() { glfwSetWindowShouldClose(window, GLFW_TRUE); };
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


  ImVec4 &clear_color = g_clearColor;
  float lastFrame = 0.0f;
  while (!glfwWindowShouldClose(window)) {
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    glfwPollEvents();

    // Handle window resize: bgfx::reset() updates backbuffer + preserves MSAA
    {
      static int s_lastFbW = 0, s_lastFbH = 0;
      int curFbW, curFbH;
      glfwGetFramebufferSize(window, &curFbW, &curFbH);
      if (curFbW != s_lastFbW || curFbH != s_lastFbH) {
        s_lastFbW = curFbW;
        s_lastFbH = curFbH;
        bgfx::reset(curFbW, curFbH, BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4);
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
        if (TexValid(g_loadingTex)) {
          TexDestroy(g_loadingTex);
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
      // Char select: render directly to backbuffer (no PP).
      // View ordering: shadow (view 8) before scene (view 0)
      {
        bgfx::ViewId order[] = { SHADOW_VIEW, 0, PP_VIEW_BRIGHT, PP_VIEW_BLUR0,
                                  PP_VIEW_BLUR1, PP_VIEW_BLUR2, PP_VIEW_BLUR3,
                                  PP_VIEW_COMPOSITE, 7 };
        bgfx::setViewOrder(0, 9, order);
      }
      bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                          0x000000FF, 1.0f, 0);
      bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
      bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
      bgfx::touch(0);

      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);

      // ImGui frame — must be started before CharacterSelect::Render() because
      // Render() queues ImGui draw commands (buttons, text). Actual GL draw
      // happens at ImGui_ImplOpenGL3_RenderDrawData() after post-processing.
      ImGui_BackendNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      CharacterSelect::Render(winW, winH);

      // Char select: no post-processing (renders directly to backbuffer)

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

    // ── LOADING state: show loading screen ──
    if (g_gameState == GameState::LOADING) {
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                          0x000005FF, 1.0f, 0);
      bgfx::setViewRect(0, 0, 0, uint16_t(fbW), uint16_t(fbH));
      bgfx::touch(0);

      ImGui_BackendNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);

      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // Draw loading screen image (centered, aspect-fit)
      if (TexValid(g_loadingTex)) {
        float imgW = 640.0f, imgH = 480.0f; // OZJ loading images are 640x480
        float scale = std::min((float)winW / imgW, (float)winH / imgH);
        float dispW = imgW * scale;
        float dispH = imgH * scale;
        float x0 = (winW - dispW) * 0.5f;
        float y0 = (winH - dispH) * 0.5f;
        dl->AddImage((ImTextureID)TexImID(g_loadingTex), ImVec2(x0, y0),
                     ImVec2(x0 + dispW, y0 + dispH));
      }

      // Loading text overlay at bottom
      const char *loadText = "Loading...";
      ImVec2 tsz = ImGui::CalcTextSize(loadText);
      dl->AddText(ImVec2(winW * 0.5f - tsz.x * 0.5f, winH * 0.85f),
                  IM_COL32(220, 200, 160, 255), loadText);

      ImGui::Render();
      ImGui_BackendRenderDrawData(ImGui::GetDrawData());
    bgfx::frame();
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
        g_mapTransitionPhase = 2;
        g_mapTransitionFrames = 0;
      } else if (g_mapTransitionPhase == 2) {
        // Phase 2: Fade out
        g_mapTransitionAlpha -= deltaTime * 2.0f; // ~0.5s fade-out
        if (g_mapTransitionAlpha <= 0.0f) {
          g_mapTransitionAlpha = 0.0f;
          g_mapTransitionActive = false;
        }
      }
    }

    InputHandler::ProcessInput(window, deltaTime);
    g_camera.Update(deltaTime);

    // Main 5.2 Lorencia uses static daylight (no day/night cycle)
    g_worldTime += deltaTime * 25.0f; // Still tick for chrome/sun animation
    // Per-map luminosity from config
    g_luminosity = g_mapCfg->luminosity;

    // Push luminosity to all renderers
    g_terrain.SetLuminosity(g_luminosity);
    g_objectRenderer.SetLuminosity(g_luminosity);
    g_hero.SetLuminosity(g_luminosity);
    g_npcManager.SetLuminosity(g_luminosity);
    g_monsterManager.SetLuminosity(g_luminosity);
    g_boidManager.SetLuminosity(g_luminosity);
    g_grass.SetLuminosity(g_luminosity);

    // Send player position to server periodically (~4Hz)
    {
      // Tick potion cooldown
      if (g_potionCooldown > 0.0f)
        g_potionCooldown = std::max(0.0f, g_potionCooldown - deltaTime);

      static float posTimer = 0.0f;
      static int lastGridX = -1, lastGridY = -1;
      posTimer += deltaTime;
      if (posTimer >= 0.25f && !g_mapTransitionActive) {
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
      static bool windStarted = false;
      static float windAngle = 0.0f;
      if (g_mapCfg->hasWind) {
        if (!windStarted) {
          float wx = lp.x + 200.0f;
          SoundManager::Play3DLoop(SOUND_WIND01, wx, lp.y + 50.0f, lp.z, 0.4f);
          windStarted = true;
        }
        windAngle += deltaTime * 0.3f; // ~0.3 rad/s = full circle in ~21s
        float windDist = 200.0f;
        float wx = lp.x + cosf(windAngle) * windDist;
        float wz = lp.z + sinf(windAngle) * windDist;
        SoundManager::UpdateSource3D(SOUND_WIND01, wx, lp.y + 50.0f, wz);
      } else {
        windStarted =
            false; // Reset so wind restarts when returning to Lorencia
      }
    }

    // Update monster manager (state machines, animation)
    g_monsterManager.SetPlayerPosition(g_hero.GetPosition());
    g_monsterManager.SetPlayerDead(g_hero.IsDead());
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
                InventoryUI::ShowNotification(isDK ? "Not enough AG!"
                                                   : "Not enough Mana!");
              }
            } else {
              g_server.SendAttack(serverIdx);
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
                InventoryUI::ShowNotification(isDK ? "Not enough AG!"
                                                   : "Not enough Mana!");
                g_hero.CancelAttack();
              }
            } else if (g_hero.GetActiveSkillId() == 0) {
              // Normal attack auto-re-engage
              g_hero.AttackMonster(targetIdx, mi.position);
            }
          }
        }

        // Self-AoE continuous casting: re-cast when GCD expires + RMB held
        bool isAoESkill =
            (g_rmcSkillId == 8 || g_rmcSkillId == 9 || g_rmcSkillId == 10 ||
             g_rmcSkillId == 12 || g_rmcSkillId == 14 || g_rmcSkillId == 41 ||
             g_rmcSkillId == 42 || g_rmcSkillId == 43);
        if (g_hero.GetAttackState() == AttackState::NONE &&
            g_hero.GetAttackTarget() < 0 && g_rightMouseHeld && isAoESkill &&
            g_hero.GetGlobalCooldown() <= 0.0f) {
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
          g_hero.SetAction(HeroCharacter::ACTION_STOP_MALE);
        else
          g_hero.SetAction(g_hero.weaponIdleAction());
      }
    }

    // Town teleport: play heal animation, then warp to Lorencia safe zone
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

        // If in dungeon, warp to Lorencia city center (grid 137,126)
        if (g_currentMapId != 0) {
          SoundManager::StopAll();
          g_server.SendWarpCommand(0, 137, 126);
          g_hero.SetAction(1);
          g_hero.SetTeleportCooldown();
          // Start transition overlay immediately
          g_mapTransitionActive = true;
          g_mapTransitionPhase = 0;
          g_mapTransitionAlpha = 0.0f;
          g_mapTransitionFrames = 0;
          g_mapTransMapId = 0;
          g_mapTransSpawnX = 137;
          g_mapTransSpawnY = 126;
        } else {
          // Already on Lorencia — teleport to city center (grid 137,126)
          const int S = TerrainParser::TERRAIN_SIZE;
          int startGX = 137, startGZ = 126;
          glm::vec3 spawnPos(12600.0f, 0.0f, 13700.0f);
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
                uint8_t attr =
                    g_terrainDataPtr->mapping.attributes[cz * S + cx];
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
          SoundManager::StopAll();
          g_hero.SetPosition(spawnPos);
          g_hero.SnapToTerrain();
          g_hero.SetAction(1);
          g_camera.SetPosition(g_hero.GetPosition());
          g_server.SendPrecisePosition(spawnPos.x, spawnPos.z);
          g_objectRenderer.ResetDoorStates();
          InventoryUI::ShowRegionName("Lorencia");
          g_hero.SetTeleportCooldown();
        }
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

    // Hero respawn: after death timer expires, respawn in same-map safe zone
    // Dungeon (map 1) has no safe zone → warp to Lorencia
    if (g_hero.ReadyToRespawn()) {
      // Per-map respawn points (grid coordinates)
      int respawnMapId = g_currentMapId;
      int respawnGX = 137, respawnGZ = 126; // Lorencia default
      if (g_currentMapId == 1) {
        // Dungeon: no safe zone, warp back to Lorencia
        respawnMapId = 0;
        respawnGX = 137; respawnGZ = 126;
      } else if (g_currentMapId == 2) {
        // Devias: respawn in Devias town center (near Guild Master)
        respawnGX = 215; respawnGZ = 47;
      }

      glm::vec3 spawnPos((float)respawnGZ * 100.0f, 0.0f,
                         (float)respawnGX * 100.0f);

      if (respawnMapId != g_currentMapId) {
        // Different map: warp + start fade overlay
        g_server.SendWarpCommand(respawnMapId, respawnGX, respawnGZ);
        g_mapTransitionActive = true;
        g_mapTransitionPhase = 0;
        g_mapTransitionAlpha =
            0.8f; // Start mostly black (death screen is dark)
        g_mapTransitionFrames = 0;
        g_mapTransMapId = respawnMapId;
        g_mapTransSpawnX = respawnGX;
        g_mapTransSpawnY = respawnGZ;
      } else {
        // Same map: find walkable tile near respawn point
        const int S = TerrainParser::TERRAIN_SIZE;
        for (int radius = 0; radius < 30; radius++) {
          bool found = false;
          for (int dy = -radius; dy <= radius && !found; dy++) {
            for (int dx = -radius; dx <= radius && !found; dx++) {
              if (radius > 0 && std::abs(dx) != radius &&
                  std::abs(dy) != radius)
                continue;
              int cx = respawnGX + dx, cz = respawnGZ + dy;
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
      }

      g_hero.Respawn(spawnPos);
      g_hero.SnapToTerrain();
      g_camera.SetPosition(g_hero.GetPosition());
      g_serverHP = g_serverMaxHP; // Reset HUD HP
      g_serverMP = g_serverMaxMP; // Reset AG/Mana on respawn

      // Notify server that player is alive (clears session.dead)
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
            float h = g_terrainDataPtr->heightmap[iz * 256 + ix] * 1.5f;
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
        alpha += (g_typeAlphaTarget[type] - alpha) * blend;
      }
      g_objectRenderer.SetTypeAlpha(g_typeAlpha);

      // Door animation: proximity-based swing/slide (Main 5.2:
      // ZzzObject.cpp:3871)
      if (g_mapCfg->hasDoors)
        g_objectRenderer.UpdateDoors(heroPos, deltaTime);

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
        if (g_mapCfg->hasWind)
          SoundManager::PlayLoop(SOUND_WIND01);
        if (g_mapCfg->wildMusic)
          SoundManager::CrossfadeTo(g_dataPath + "/" + g_mapCfg->wildMusic);
        else
          SoundManager::FadeOut();
      }
    }

    // Update music fade transitions
    SoundManager::UpdateMusic(deltaTime);

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
        ? glm::ortho(-1500.0f, 1500.0f, -1500.0f, 1500.0f, 100.0f, 5000.0f)
        : glm::orthoRH_ZO(-1500.0f, 1500.0f, -1500.0f, 1500.0f, 100.0f, 5000.0f);
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

    // Render world objects first (before grass, so tall grass billboards
    // don't block thin fence bar meshes via depth buffer)
    g_objectRenderer.Render(view, projection, g_camera.GetPosition(),
                            currentFrame);

    // Render grass billboards (config-driven)
    if (g_mapCfg->hasGrass) {
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

    // Dungeon trap VFX (Main 5.2: types 39=lance, 40=blade, 51=fire)
    // HiddenMesh=-2, VFX-only — spawned continuously in CharacterAnimation
    if (g_mapCfg->hasDungeonTraps) {
      static float trapVfxTimer = 0.0f;
      trapVfxTimer += deltaTime;
      if (trapVfxTimer >= 0.15f) { // ~7 bursts/sec
        trapVfxTimer -= 0.15f;
        for (auto &inst : g_objectRenderer.GetInstances()) {
          glm::vec3 pos = glm::vec3(inst.modelMatrix[3]);
          if (inst.type == 39) {
            // Lance Trap: lightning sprites (Main 5.2: MODEL_SAW +
            // SOUND_TRAP01)
            pos.y += 30.0f + (float)(rand() % 40);
            g_vfxManager.SpawnBurst(ParticleType::SPELL_LIGHTNING, pos, 2);
          } else if (inst.type == 51) {
            // Fire Trap: fire sprites (Main 5.2: BITMAP_FIRE+1 + SOUND_FLAME)
            pos.y += 20.0f + (float)(rand() % 30);
            g_vfxManager.SpawnBurst(ParticleType::FIRE, pos, 1);
          }
        }
      }
    }

    g_vfxManager.Update(deltaTime);

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
    g_boidManager.Update(deltaTime, g_hero.GetPosition(), 0, currentFrame);
    g_fireEffect.Render(view, projection);
    g_objectRenderer.RenderLightningSprites(view, projection, currentFrame);

    // Render ambient creatures (birds/fish/bats/leaves)
    g_boidManager.RenderShadows(view, projection);
    g_boidManager.Render(view, projection, camPos);
    if (g_mapCfg->hasLeaves)
      g_boidManager.RenderLeaves(view, projection);

    // Update NPC interaction state (guard faces player only when quest dialog
    // is open)
    g_npcManager.SetPlayerPosition(g_hero.GetPosition());
    g_npcManager.SetInteractingNpc(g_questDialogOpen ? g_questDialogNpcIndex
                                                     : -1);
    g_npcManager.SetQuestState(g_questIndex, g_deviasQuestIndex,
                               g_questKillCount, g_questRequired,
                               g_questTargetCount, g_currentMapId);

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
    ImGui::NewFrame();

    // Simplified ImGui HUD
    {
      // Unified bottom HUD bar (HP, QWER, 1234, RMC, AG, XP)
      ImDrawList *dl = ImGui::GetForegroundDrawList();

      // FPS counter (top-left) — smoothed with exponential moving average
      {
        static float smoothedFPS = 60.0f;
        float instantFPS = 1.0f / std::max(deltaTime, 0.001f);
        smoothedFPS +=
            (instantFPS - smoothedFPS) * 0.05f; // ~20-frame smoothing
        char fpsText[16];
        snprintf(fpsText, sizeof(fpsText), "%.0f", smoothedFPS);
        dl->AddText(ImVec2(5, 4), IM_COL32(200, 200, 200, 160), fpsText);
      }

      InventoryUI::RenderQuickbar(dl, g_hudCoords);
      InventoryUI::RenderCastBar(dl);

      // ── Floating damage numbers ──
      FloatingDamageRenderer::UpdateAndRender(
          g_floatingDmg, MAX_FLOATING_DAMAGE, deltaTime, dl, g_fontDefault,
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

    // ── Quest Dialog (guard NPC overlay with quest system) ──
    g_questDialogJustOpened = false;
    if (g_questDialogOpen && !g_questDialogWasOpen) {
      g_questDialogSelected = -1;
      g_questDialogJustOpened = true;
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
          dl->AddRectFilled(bMin2, bMax2,
                            hov ? IM_COL32(45, 40, 28, 230)
                                : IM_COL32(25, 22, 16, 230),
                            3.0f);
          dl->AddRect(bMin2, bMax2, cBorder, 3.0f, 0, 1.0f);
          ImVec2 ls = ImGui::CalcTextSize(label);
          dl->AddText(ImVec2(bx + (bw - ls.x) * 0.5f, by + (bh - ls.y) * 0.5f),
                      textColor, label);
          return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                 !g_questDialogJustOpened;
        };

        float btnW = 80.0f, btnH = 26.0f;

        // Determine dialog state for this guard
        // States: "offer_kill", "travel_accept", "in_progress", "completable",
        //         "handoff", "done", "not_yet", "all_done"
        const char *dialogState = "not_yet";
        const QuestClientInfo *showQuest = nullptr; // Quest to display info for
        const QuestClientInfo *travelQuest =
            nullptr; // Travel quest (for combined travel+accept)

        // Use chain-appropriate quest index based on guard type
        int chainQI = GetActiveQuestForGuard(guardType);
        bool isActiveChain = IsDeviasGuard(guardType) ? (g_currentMapId == 2)
                                                      : (g_currentMapId != 2);

        if (chainQI >= QUEST_COUNT) {
          dialogState = "all_done";
        } else {
          const auto &curQ = g_questDefs[chainQI];
          if (curQ.questType == 1 && curQ.guardType == guardType) {
            // This guard is the travel destination — show combined travel+kill
            // accept
            dialogState = "travel_accept";
            travelQuest = &curQ;
            // The next quest (kill quest) should be the one we show
            if (chainQI + 1 < QUEST_COUNT)
              showQuest = &g_questDefs[chainQI + 1];
          } else if (curQ.questType == 0 && curQ.guardType == guardType) {
            // This guard owns the current kill quest
            bool accepted = isActiveChain && (g_questTargetCount > 0);
            if (!accepted) {
              dialogState = "offer_kill";
              showQuest = &curQ;
            } else {
              bool allDone = true;
              for (int i = 0; i < g_questTargetCount; i++) {
                if (g_questKillCount[i] < g_questRequired[i]) {
                  allDone = false;
                  break;
                }
              }
              if (allDone) {
                dialogState = "completable";
                showQuest = &curQ;
              } else {
                dialogState = "in_progress";
                showQuest = &curQ;
              }
            }
          } else {
            // Not this guard's turn — check relationship
            int myKillQuest = GetGuardKillQuest(guardType);
            if (myKillQuest >= 0 && myKillQuest < chainQI) {
              // Guard's quest is done
              if (curQ.questType == 1) {
                dialogState = "handoff"; // Tell player to go to next guard
              } else {
                dialogState = "done";
              }
            } else {
              dialogState = "not_yet";
            }
          }
        }

        // ── Render dialog panel based on state ──
        float panelW = 340.0f;
        const char *npcName = npcInfo.name.c_str();

        // Compute content for the panel
        const char *dialogText = "";
        const char *titleText = npcName;
        bool showObjectives = false;
        bool showRewards = false;
        bool showAcceptBtn = false;
        bool showCompleteBtn = false;
        bool showCloseBtn = false;

        if (strcmp(dialogState, "offer_kill") == 0 && showQuest) {
          titleText = showQuest->questName;
          dialogText = showQuest->loreText;
          showObjectives = true;
          showRewards = true;
          showAcceptBtn = true;
        } else if (strcmp(dialogState, "travel_accept") == 0 && showQuest) {
          titleText = showQuest->questName;
          // Combined travel greeting + kill quest lore
          dialogText = showQuest->loreText;
          showObjectives = true;
          showRewards = true;
          showAcceptBtn = true;
        } else if (strcmp(dialogState, "in_progress") == 0 && showQuest) {
          titleText = showQuest->questName;
          dialogText = "The task is not yet complete.\nKeep fighting.";
          showObjectives = true;
          showCloseBtn = true;
        } else if (strcmp(dialogState, "completable") == 0 && showQuest) {
          titleText = showQuest->questName;
          dialogText =
              "Well done! You've completed\nthe task. Here is your reward.";
          showObjectives = true;
          showRewards = true;
          showCompleteBtn = true;
        } else if (strcmp(dialogState, "handoff") == 0) {
          // Find the travel destination guard name
          const auto &curQ = g_questDefs[chainQI];
          const char *destName = GetGuardName(curQ.guardType);
          static char handoffBuf[128];
          snprintf(handoffBuf, sizeof(handoffBuf),
                   "I have no more tasks for you.\nSeek out %s.", destName);
          dialogText = handoffBuf;
          showCloseBtn = true;
        } else if (strcmp(dialogState, "done") == 0) {
          dialogText =
              "My tasks are complete.\nGood luck on your journey, warrior.";
          showCloseBtn = true;
        } else if (strcmp(dialogState, "all_done") == 0) {
          dialogText =
              IsDeviasGuard(guardType)
                  ? "Devias is safe once more, warrior.\nThe ice holds no more "
                    "terrors.\nMay the winds carry you to glory."
                  : "Lorencia owes you a great debt,\nwarrior. Every threat "
                    "has been\nvanquished by your hand. May the\nSerenity "
                    "guide your path.";
          showCloseBtn = true;
        } else { // not_yet
          dialogText =
              "I have no task for you yet.\nSpeak to the other guards first.";
          showCloseBtn = true;
        }

        // Calculate panel height — measure everything that will be rendered
        ImVec2 dialogSize = ImGui::CalcTextSize(dialogText);
        float subtitleH = showQuest ? (ImGui::CalcTextSize(npcName).y + 4) : 0;
        float objH = 0;
        if (showObjectives && showQuest)
          objH = 18 + showQuest->targetCount * 18.0f;
        int weaponCount = 0;
        if (showRewards && showQuest && showQuest->questType == 0) {
          if (showQuest->dkReward.defIndex >= 0)
            weaponCount++;
          if (showQuest->dwReward.defIndex >= 0)
            weaponCount++;
          if (showQuest->orbReward.defIndex >= 0)
            weaponCount++;
          if (showQuest->scrollReward.defIndex >= 0)
            weaponCount++;
        }
        float weaponH = weaponCount > 0 ? (weaponCount * 48.0f + 4.0f) : 0;
        float rewardsH = showRewards ? (10 + 18 + 18 + 18 + weaponH) : 0;
        float buttonsH = btnH + 16;
        float infoH = showQuest ? (ImGui::GetFontSize() + 8) : 0;
        float panelH = 14 + 20 + 6 + subtitleH + infoH + 1 + 10 + dialogSize.y +
                       10 + objH + rewardsH + buttonsH + 14;
        float px = (dispSize.x - panelW) * 0.5f;
        float py = (dispSize.y - panelH) * 0.5f;
        g_qdPanelRect[0] = px;
        g_qdPanelRect[1] = py;
        g_qdPanelRect[2] = panelW;
        g_qdPanelRect[3] = panelH;

        // Panel background with double border for parchment feel
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH), cBg,
                          5.0f);
        dl->AddRect(ImVec2(px + 1, py + 1),
                    ImVec2(px + panelW - 1, py + panelH - 1),
                    IM_COL32(40, 35, 25, 100), 4.0f, 0, 1.0f);
        dl->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH), cBorder,
                    5.0f, 0, 1.5f);

        float contentY = py + 14;

        // Title
        {
          ImVec2 ts = ImGui::CalcTextSize(titleText);
          dl->AddText(ImVec2(px + (panelW - ts.x) * 0.5f, contentY), cTitle,
                      titleText);
          contentY += ts.y + 6;
        }

        // Guard name subtitle (if title is quest name)
        if (showQuest) {
          ImVec2 gs = ImGui::CalcTextSize(npcName);
          dl->AddText(ImVec2(px + (panelW - gs.x) * 0.5f, contentY), cTextDim,
                      npcName);
          contentY += gs.y + 4;
          // Location and recommended level
          char infoBuf[64];
          snprintf(infoBuf, sizeof(infoBuf), "%s  |  Lv. %d",
                   showQuest->location, (int)showQuest->recommendedLevel);
          ImVec2 infoSz = ImGui::CalcTextSize(infoBuf);
          ImU32 lvColor = (g_hero.GetLevel() >= showQuest->recommendedLevel)
                              ? IM_COL32(120, 180, 120, 200)
                              : IM_COL32(200, 120, 80, 200);
          dl->AddText(ImVec2(px + (panelW - infoSz.x) * 0.5f, contentY),
                      lvColor, infoBuf);
          contentY += infoSz.y + 4;
        }

        dl->AddLine(ImVec2(px + 16, contentY),
                    ImVec2(px + panelW - 16, contentY), cSep);
        contentY += 10;

        // Dialog text
        dl->AddText(ImVec2(px + 20, contentY), cText, dialogText);
        contentY += dialogSize.y + 10;

        // Objectives
        if (showObjectives && showQuest) {
          dl->AddText(ImVec2(px + 20, contentY), cTextDim, "Objectives");
          contentY += 18;
          bool inProgress = (strcmp(dialogState, "in_progress") == 0 ||
                             strcmp(dialogState, "completable") == 0);
          for (int i = 0; i < showQuest->targetCount; i++) {
            char objBuf[80];
            if (inProgress) {
              snprintf(objBuf, sizeof(objBuf), "  %s  %d / %d",
                       showQuest->targets[i].name, g_questKillCount[i],
                       (int)showQuest->targets[i].killsReq);
              bool done = g_questKillCount[i] >= showQuest->targets[i].killsReq;
              dl->AddText(ImVec2(px + 22, contentY), done ? cGreen : cText,
                          objBuf);
            } else {
              snprintf(objBuf, sizeof(objBuf), "  Slay %d %s",
                       showQuest->targets[i].killsReq,
                       showQuest->targets[i].name);
              dl->AddText(ImVec2(px + 22, contentY), cText, objBuf);
            }
            contentY += 18;
          }
        }

        // Rewards
        if (showRewards && showQuest) {
          contentY += 4;
          dl->AddLine(ImVec2(px + 16, contentY),
                      ImVec2(px + panelW - 16, contentY), cSep);
          contentY += 6;
          dl->AddText(ImVec2(px + 20, contentY), cTextDim, "Rewards");
          contentY += 18;
          // Include travel quest rewards if applicable
          uint32_t totalZen = showQuest->zenReward;
          uint32_t totalXp = showQuest->xpReward;
          if (travelQuest) {
            totalZen += travelQuest->zenReward;
            totalXp += travelQuest->xpReward;
          }

          std::string zenStr = std::to_string(totalZen);
          int n = (int)zenStr.length() - 3;
          while (n > 0) {
            zenStr.insert(n, ",");
            n -= 3;
          }
          char rwBuf[64];
          snprintf(rwBuf, sizeof(rwBuf), "  %s Zen", zenStr.c_str());
          dl->AddText(ImVec2(px + 22, contentY), cGold, rwBuf);
          contentY += 18;
          std::string xpStr = std::to_string(totalXp);
          n = (int)xpStr.length() - 3;
          while (n > 0) {
            xpStr.insert(n, ",");
            n -= 3;
          }
          snprintf(rwBuf, sizeof(rwBuf), "  %s Experience", xpStr.c_str());
          dl->AddText(ImVec2(px + 22, contentY), IM_COL32(180, 140, 255, 255),
                      rwBuf);
          contentY += 18;
          // Item reward 3D models
          if (showQuest->questType == 0) {
            contentY += 4;
            float h1 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                           showQuest->dkReward, "Dark Knight");
            contentY += h1;
            float h2 = DrawQuestRewardItem(dl, px, contentY, panelW,
                                           showQuest->dwReward, "Dark Wizard");
            contentY += h2;
            float h3 = DrawQuestRewardItem(
                dl, px, contentY, panelW, showQuest->orbReward, "DK Skill Orb");
            contentY += h3;
            float h4 =
                DrawQuestRewardItem(dl, px, contentY, panelW,
                                    showQuest->scrollReward, "DW Spell Scroll");
            contentY += h4;
          }
        }

        // Separator before buttons
        contentY += 2;
        dl->AddLine(ImVec2(px + 16, contentY),
                    ImVec2(px + panelW - 16, contentY), cSep);

        // Buttons
        float btnY = py + panelH - btnH - 12.0f;
        if (showAcceptBtn) {
          float gap = 16.0f;
          float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
          float bx2 = bx1 + btnW + gap;
          if (drawButton(bx1, btnY, btnW, btnH, "Accept", cGreen)) {
            g_server.SendQuestAccept(guardType);
            SoundManager::Play3D(SOUND_QUEST_ACCEPT, npcInfo.position.x,
                                 npcInfo.position.y, npcInfo.position.z);
            closeQuestDialog();
          }
          if (drawButton(bx2, btnY, btnW, btnH, "Close", cTextDim)) {
            SoundManager::Play(SOUND_CLICK01);
            closeQuestDialog();
          }
        } else if (showCompleteBtn) {
          float gap = 16.0f;
          float bx1 = px + (panelW - btnW * 2 - gap) * 0.5f;
          float bx2 = bx1 + btnW + gap;
          if (drawButton(bx1, btnY, btnW, btnH, "Complete", cGold)) {
            SoundManager::Play3D(SOUND_QUEST_ACCEPT, npcInfo.position.x,
                                 npcInfo.position.y, npcInfo.position.z);
            g_server.SendQuestComplete(guardType);
            closeQuestDialog();
          }
          if (drawButton(bx2, btnY, btnW, btnH, "Close", cTextDim)) {
            SoundManager::Play(SOUND_CLICK01);
            closeQuestDialog();
          }
        } else {
          if (drawButton(px + (panelW - btnW) * 0.5f, btnY, btnW, btnH, "Close",
                         cTextDim)) {
            SoundManager::Play(SOUND_CLICK01);
            closeQuestDialog();
          }
        }
      }
    }

    // ── Quest Tracker HUD (top-right, minimal) ──
    // Show the map-appropriate active quest
    int trackerQI = (g_currentMapId == 2) ? g_deviasQuestIndex : g_questIndex;
    if (trackerQI < QUEST_COUNT) {
      const auto &curQ = g_questDefs[trackerQI];
      bool showTracker = false;

      if (curQ.questType == 1) {
        // Travel quest — show "Report to [guard name]"
        showTracker = true;
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 dispSize = ImGui::GetIO().DisplaySize;
        float lineH = 15.0f;
        float trackerW = 200.0f;
        float trackerH = 20.0f + lineH;
        float tx = dispSize.x - trackerW - 12.0f;
        float ty = 60.0f;
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + trackerW, ty + trackerH),
                          IM_COL32(0, 0, 0, 120), 3.0f);
        float cy = ty + 4;
        dl->AddText(ImVec2(tx + 6, cy), IM_COL32(220, 185, 50, 220),
                    curQ.questName);
        cy += lineH + 2;
        char buf[64];
        snprintf(buf, sizeof(buf), " Find %s", GetGuardName(curQ.guardType));
        dl->AddText(ImVec2(tx + 6, cy), IM_COL32(180, 180, 180, 200), buf);
      } else if (g_questTargetCount > 0) {
        // Kill quest with accepted targets
        showTracker = true;
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 dispSize = ImGui::GetIO().DisplaySize;
        float lineH = 15.0f;
        bool allDone = true;
        for (int i = 0; i < g_questTargetCount; i++) {
          if (g_questKillCount[i] < g_questRequired[i]) {
            allDone = false;
            break;
          }
        }
        float trackerW = 200.0f;
        float trackerH =
            20.0f + g_questTargetCount * lineH + (allDone ? lineH : 0);
        float tx = dispSize.x - trackerW - 12.0f;
        float ty = 60.0f;
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + trackerW, ty + trackerH),
                          IM_COL32(0, 0, 0, 120), 3.0f);
        float cy = ty + 4;
        dl->AddText(ImVec2(tx + 6, cy), IM_COL32(220, 185, 50, 220),
                    curQ.questName);
        cy += lineH + 2;
        for (int i = 0; i < g_questTargetCount; i++) {
          char buf[48];
          bool done = g_questKillCount[i] >= g_questRequired[i];
          snprintf(buf, sizeof(buf), " %s  %d/%d", curQ.targets[i].name,
                   g_questKillCount[i], g_questRequired[i]);
          dl->AddText(ImVec2(tx + 6, cy),
                      done ? IM_COL32(80, 210, 80, 200)
                           : IM_COL32(180, 180, 180, 200),
                      buf);
          cy += lineH;
        }
        if (allDone) {
          char turnIn[64];
          snprintf(turnIn, sizeof(turnIn), "Return to %s",
                   GetGuardName(curQ.guardType));
          dl->AddText(ImVec2(tx + 6, cy), IM_COL32(220, 185, 50, 220), turnIn);
        }
      }
      (void)showTracker;
    }

    // ── Quest Log Window (L key) — right side, active quests only ──
    if (g_showQuestLog) {
      ImDrawList *ql = ImGui::GetForegroundDrawList();
      ImVec2 dispSz = ImGui::GetIO().DisplaySize;
      ImVec2 mPos = ImGui::GetIO().MousePos;

      // WoW-style colors
      ImU32 qlBg = IM_COL32(12, 10, 8, 240);
      ImU32 qlBorder = IM_COL32(80, 70, 50, 180);
      ImU32 qlTitle = IM_COL32(255, 210, 80, 255);
      ImU32 qlText = IM_COL32(200, 195, 180, 255);
      ImU32 qlDim = IM_COL32(140, 135, 120, 255);
      ImU32 qlGold = IM_COL32(255, 210, 50, 255);
      ImU32 qlGreen = IM_COL32(80, 220, 80, 255);
      ImU32 qlSep = IM_COL32(50, 45, 35, 120);
      ImU32 qlRowHov = IM_COL32(40, 35, 25, 200);
      ImU32 qlPurple = IM_COL32(180, 140, 255, 255);
      ImU32 qlRed = IM_COL32(220, 80, 80, 255);

      static int qlSelectedQuest = -1; // -1 = list view

      float panelW = 340.0f;

      // Quest status: 0=locked, 1=available, 2=in progress, 3=complete, 4=done
      // Uses chain-appropriate index: quests 0-11 → g_questIndex, 12-17 →
      // g_deviasQuestIndex
      auto qlStatus = [&](int qi) -> int {
        int chainIdx = GetChainIndex(qi);
        if (qi < chainIdx)
          return 4; // already completed
        if (qi > chainIdx)
          return 0; // locked
        // Current quest in this chain
        const auto &q = g_questDefs[qi];
        if (q.questType == 1)
          return 2; // travel quest = always in progress
        // Kill quest: only show kill counts if this chain is active on current
        // map
        bool isActiveChain = (qi >= DEVIAS_QUEST_START) ? (g_currentMapId == 2)
                                                        : (g_currentMapId != 2);
        if (!isActiveChain)
          return 1; // available but kill counts are for other chain
        if (g_questTargetCount == 0)
          return 1; // not accepted yet
        bool allDone = true;
        for (int i = 0; i < g_questTargetCount; i++)
          if (g_questKillCount[i] < g_questRequired[i]) {
            allDone = false;
            break;
          }
        return allDone ? 3 : 2;
      };

      // Collect active quests (status 2=in progress, 3=ready to turn in)
      int activeQuests[QUEST_COUNT];
      int activeCount = 0;
      for (int qi = 0; qi < QUEST_COUNT; qi++) {
        int st = qlStatus(qi);
        if (st == 2 || st == 3)
          activeQuests[activeCount++] = qi;
      }

      // Reset selected if it's no longer active
      if (qlSelectedQuest >= 0) {
        int st = qlStatus(qlSelectedQuest);
        if (st != 2 && st != 3)
          qlSelectedQuest = -1;
      }

      // Right side positioning
      float px = dispSz.x - panelW - 20.0f;

      if (qlSelectedQuest >= 0 && qlSelectedQuest < QUEST_COUNT) {
        // ── QUEST DETAIL VIEW ──
        const auto &q = g_questDefs[qlSelectedQuest];
        int st = qlStatus(qlSelectedQuest);

        ImVec2 loreSize = ImGui::CalcTextSize(q.loreText);
        bool isTravel = (q.questType == 1);
        float objH = isTravel ? 18.0f : q.targetCount * 18.0f;
        int qlWeaponCount = 0;
        if (!isTravel) {
          if (q.dkReward.defIndex >= 0)
            qlWeaponCount++;
          if (q.dwReward.defIndex >= 0)
            qlWeaponCount++;
        }
        float qlWeaponH =
            qlWeaponCount > 0 ? (qlWeaponCount * 48.0f + 4.0f) : 0;
        float rewardsH = 10 + 18 + 18 + 18 + qlWeaponH;
        // Guard name subtitle
        float qlSubtitleH =
            ImGui::CalcTextSize(GetGuardName(q.guardType)).y + 4;
        float qlInfoH = ImGui::GetFontSize() + 8; // location + level line
        float hintH = (st == 3 || isTravel) ? 24.0f : 6.0f;
        float abandonH = (st == 2 && !isTravel) ? 34.0f : 0.0f;
        float panelH = 12 + 20 + 6 + qlSubtitleH + qlInfoH + 1 + 10 +
                       loreSize.y + 10 + 18 + objH + rewardsH + hintH +
                       abandonH + 26 + 12 + 14;
        float py = (dispSz.y - panelH) * 0.5f;
        g_qlPanelRect[0] = px;
        g_qlPanelRect[1] = py;
        g_qlPanelRect[2] = panelW;
        g_qlPanelRect[3] = panelH;

        ql->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                          qlBg, 4.0f);
        ql->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH), qlBorder,
                    4.0f, 0, 1.0f);

        float cy = py + 12;
        {
          ImVec2 ts = ImGui::CalcTextSize(q.questName);
          ql->AddText(ImVec2(px + (panelW - ts.x) * 0.5f, cy), qlTitle,
                      q.questName);
          cy += ts.y + 6;
        }

        // Guard name
        const char *guardName = GetGuardName(q.guardType);
        {
          ImVec2 gs = ImGui::CalcTextSize(guardName);
          ql->AddText(ImVec2(px + (panelW - gs.x) * 0.5f, cy), qlDim,
                      guardName);
          cy += gs.y + 4;
        }
        // Location and recommended level
        {
          char infoBuf[64];
          snprintf(infoBuf, sizeof(infoBuf), "%s  |  Lv. %d", q.location,
                   (int)q.recommendedLevel);
          ImVec2 infoSz = ImGui::CalcTextSize(infoBuf);
          ImU32 lvColor = (g_hero.GetLevel() >= q.recommendedLevel)
                              ? IM_COL32(120, 180, 120, 200)
                              : IM_COL32(200, 120, 80, 200);
          ql->AddText(ImVec2(px + (panelW - infoSz.x) * 0.5f, cy), lvColor,
                      infoBuf);
          cy += infoSz.y + 4;
        }

        ql->AddLine(ImVec2(px + 16, cy), ImVec2(px + panelW - 16, cy), qlSep);
        cy += 10;

        ql->AddText(ImVec2(px + 18, cy), qlText, q.loreText);
        cy += loreSize.y + 10;

        // Objectives
        ql->AddText(ImVec2(px + 18, cy), qlDim, "Objective");
        cy += 18;

        if (isTravel) {
          char objBuf[80];
          snprintf(objBuf, sizeof(objBuf), "  Report to %s",
                   GetGuardName(q.guardType));
          ql->AddText(ImVec2(px + 20, cy), qlText, objBuf);
          cy += 18;
        } else {
          for (int i = 0; i < q.targetCount; i++) {
            char objBuf[80];
            snprintf(objBuf, sizeof(objBuf), "  %s  %d / %d", q.targets[i].name,
                     g_questKillCount[i], (int)q.targets[i].killsReq);
            bool done = g_questKillCount[i] >= q.targets[i].killsReq;
            ql->AddText(ImVec2(px + 20, cy), done ? qlGreen : qlText, objBuf);
            cy += 18;
          }
        }

        // Rewards
        cy += 4;
        ql->AddLine(ImVec2(px + 16, cy), ImVec2(px + panelW - 16, cy), qlSep);
        cy += 6;
        ql->AddText(ImVec2(px + 18, cy), qlDim, "Rewards");
        cy += 18;
        {
          std::string zenStr = std::to_string(q.zenReward);
          int n = (int)zenStr.length() - 3;
          while (n > 0) {
            zenStr.insert(n, ",");
            n -= 3;
          }
          char rwBuf[64];
          snprintf(rwBuf, sizeof(rwBuf), "  %s Zen", zenStr.c_str());
          ql->AddText(ImVec2(px + 20, cy), qlGold, rwBuf);
          cy += 18;
          std::string xpStr = std::to_string(q.xpReward);
          n = (int)xpStr.length() - 3;
          while (n > 0) {
            xpStr.insert(n, ",");
            n -= 3;
          }
          snprintf(rwBuf, sizeof(rwBuf), "  %s Experience", xpStr.c_str());
          ql->AddText(ImVec2(px + 20, cy), qlPurple, rwBuf);
          cy += 18;
        }

        // Item reward 3D models
        if (!isTravel) {
          cy += 4;
          float h1 = DrawQuestRewardItem(ql, px, cy, panelW, q.dkReward,
                                         "Dark Knight");
          cy += h1;
          float h2 = DrawQuestRewardItem(ql, px, cy, panelW, q.dwReward,
                                         "Dark Wizard");
          cy += h2;
          float h3 = DrawQuestRewardItem(ql, px, cy, panelW, q.orbReward,
                                         "DK Skill Orb");
          cy += h3;
          float h4 = DrawQuestRewardItem(ql, px, cy, panelW, q.scrollReward,
                                         "DW Spell Scroll");
          cy += h4;
        }

        // Status hint
        cy += 6;
        if (isTravel)
          ql->AddText(ImVec2(px + 18, cy), qlGold, "Travel to the next guard.");
        else if (st == 3)
          ql->AddText(ImVec2(px + 18, cy), qlGold,
                      "Return to complete this quest.");

        // Buttons row: Back + Abandon (if kill quest in progress)
        float btnY2 = py + panelH - 26 - 12;
        float bh = 26;
        if (st == 2 && !isTravel) {
          // Two buttons: Back and Abandon
          float bw = 80;
          float gap = 16.0f;
          float totalW = bw * 2 + gap;
          float bx1 = px + (panelW - totalW) * 0.5f;
          float bx2 = bx1 + bw + gap;

          // Back button
          {
            ImVec2 bMin(bx1, btnY2), bMax(bx1 + bw, btnY2 + bh);
            bool hov = mPos.x >= bMin.x && mPos.x <= bMax.x &&
                       mPos.y >= bMin.y && mPos.y <= bMax.y;
            ql->AddRectFilled(bMin, bMax,
                              hov ? IM_COL32(45, 40, 28, 230)
                                  : IM_COL32(25, 22, 16, 230),
                              3.0f);
            ql->AddRect(bMin, bMax, qlBorder, 3.0f, 0, 1.0f);
            ImVec2 ls = ImGui::CalcTextSize("Back");
            ql->AddText(
                ImVec2(bx1 + (bw - ls.x) * 0.5f, btnY2 + (bh - ls.y) * 0.5f),
                qlDim, "Back");
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
              SoundManager::Play(SOUND_CLICK01);
              qlSelectedQuest = -1;
            }
          }

          // Abandon button
          {
            ImVec2 bMin(bx2, btnY2), bMax(bx2 + bw, btnY2 + bh);
            bool hov = mPos.x >= bMin.x && mPos.x <= bMax.x &&
                       mPos.y >= bMin.y && mPos.y <= bMax.y;
            ql->AddRectFilled(bMin, bMax,
                              hov ? IM_COL32(80, 25, 25, 230)
                                  : IM_COL32(45, 15, 15, 230),
                              3.0f);
            ql->AddRect(bMin, bMax, hov ? qlRed : IM_COL32(120, 50, 50, 180),
                        3.0f, 0, 1.0f);
            ImVec2 ls = ImGui::CalcTextSize("Abandon");
            ql->AddText(
                ImVec2(bx2 + (bw - ls.x) * 0.5f, btnY2 + (bh - ls.y) * 0.5f),
                hov ? qlRed : IM_COL32(180, 100, 100, 255), "Abandon");
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
              SoundManager::Play(SOUND_CLICK01);
              g_server.SendQuestAbandon();
              qlSelectedQuest = -1;
            }
          }
        } else {
          // Single Back button
          float bw = 80;
          float bx = px + (panelW - bw) * 0.5f;
          ImVec2 bMin(bx, btnY2), bMax(bx + bw, btnY2 + bh);
          bool hov = mPos.x >= bMin.x && mPos.x <= bMax.x && mPos.y >= bMin.y &&
                     mPos.y <= bMax.y;
          ql->AddRectFilled(bMin, bMax,
                            hov ? IM_COL32(45, 40, 28, 230)
                                : IM_COL32(25, 22, 16, 230),
                            3.0f);
          ql->AddRect(bMin, bMax, qlBorder, 3.0f, 0, 1.0f);
          ImVec2 ls = ImGui::CalcTextSize("Back");
          ql->AddText(
              ImVec2(bx + (bw - ls.x) * 0.5f, btnY2 + (bh - ls.y) * 0.5f),
              qlDim, "Back");
          if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            SoundManager::Play(SOUND_CLICK01);
            qlSelectedQuest = -1;
          }
        }
      } else {
        // ── QUEST LIST VIEW — only active quests ──
        float rowH = 44.0f;

        if (activeCount == 0) {
          // No active quests
          float panelH = 80.0f;
          float py = (dispSz.y - panelH) * 0.5f;
          g_qlPanelRect[0] = px;
          g_qlPanelRect[1] = py;
          g_qlPanelRect[2] = panelW;
          g_qlPanelRect[3] = panelH;
          ql->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                            qlBg, 4.0f);
          ql->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                      qlBorder, 4.0f, 0, 1.0f);
          float cy = py + 12;
          {
            const char *title = "Quest Log";
            ImVec2 ts = ImGui::CalcTextSize(title);
            ql->AddText(ImVec2(px + (panelW - ts.x) * 0.5f, cy), qlTitle,
                        title);
            cy += ts.y + 6;
          }
          ql->AddLine(ImVec2(px + 14, cy), ImVec2(px + panelW - 14, cy), qlSep);
          cy += 12;
          const char *msg = "No active quests.";
          ImVec2 ms = ImGui::CalcTextSize(msg);
          ql->AddText(ImVec2(px + (panelW - ms.x) * 0.5f, cy), qlDim, msg);
        } else {
          float listH = activeCount * rowH + 8;
          float panelH = 40 + listH + 12;
          float py = (dispSz.y - panelH) * 0.5f;
          g_qlPanelRect[0] = px;
          g_qlPanelRect[1] = py;
          g_qlPanelRect[2] = panelW;
          g_qlPanelRect[3] = panelH;

          ql->AddRectFilled(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                            qlBg, 4.0f);
          ql->AddRect(ImVec2(px, py), ImVec2(px + panelW, py + panelH),
                      qlBorder, 4.0f, 0, 1.0f);

          float cy = py + 12;
          {
            const char *title = "Quest Log";
            ImVec2 ts = ImGui::CalcTextSize(title);
            ql->AddText(ImVec2(px + (panelW - ts.x) * 0.5f, cy), qlTitle,
                        title);
            cy += ts.y + 6;
          }
          ql->AddLine(ImVec2(px + 14, cy), ImVec2(px + panelW - 14, cy), qlSep);
          cy += 8;

          for (int a = 0; a < activeCount; a++) {
            int qi = activeQuests[a];
            const auto &q = g_questDefs[qi];
            int st = qlStatus(qi);

            float rowY = cy;
            float rowX = px + 10;
            float rowW = panelW - 20;
            ImVec2 rMin(rowX, rowY), rMax(rowX + rowW, rowY + rowH);

            bool hov = mPos.x >= rMin.x && mPos.x <= rMax.x &&
                       mPos.y >= rMin.y && mPos.y <= rMax.y;
            if (hov)
              ql->AddRectFilled(rMin, rMax, qlRowHov, 2.0f);

            const char *icon;
            ImU32 iconCol, nameCol;
            bool isTravel = (q.questType == 1);
            if (st == 3) {
              icon = "?";
              iconCol = qlGold;
              nameCol = IM_COL32(230, 210, 140, 255);
            } else if (isTravel) {
              icon = ">";
              iconCol = qlGold;
              nameCol = IM_COL32(230, 210, 140, 255);
            } else {
              icon = "...";
              iconCol = qlDim;
              nameCol = qlText;
            }

            float fontSize = ImGui::GetFontSize();
            float textY = rowY + (rowH - fontSize * 2 - 2) * 0.5f;
            ql->AddText(ImVec2(rowX + 6, textY), iconCol, icon);
            ql->AddText(ImVec2(rowX + 26, textY), nameCol, q.questName);
            // Location + level subtitle
            {
              char locBuf[48];
              snprintf(locBuf, sizeof(locBuf), "%s  Lv. %d", q.location,
                       (int)q.recommendedLevel);
              ImU32 locCol = (g_hero.GetLevel() >= q.recommendedLevel)
                                 ? IM_COL32(100, 150, 100, 180)
                                 : IM_COL32(180, 100, 70, 180);
              ql->AddText(ImVec2(rowX + 26, textY + fontSize + 2), locCol,
                          locBuf);
            }

            // Progress on right
            if (st == 3) {
              const char *done = "Complete";
              ImVec2 dSz = ImGui::CalcTextSize(done);
              ql->AddText(ImVec2(rowX + rowW - dSz.x - 6, textY), qlGold, done);
            } else if (isTravel) {
              const char *trav = "Travel";
              ImVec2 tSz = ImGui::CalcTextSize(trav);
              ql->AddText(ImVec2(rowX + rowW - tSz.x - 6, textY), qlDim, trav);
            } else {
              // Show first incomplete target progress
              for (int i = 0; i < q.targetCount; i++) {
                if (g_questKillCount[i] < q.targets[i].killsReq) {
                  char prog[16];
                  snprintf(prog, sizeof(prog), "%d/%d", g_questKillCount[i],
                           (int)q.targets[i].killsReq);
                  ImVec2 pSz = ImGui::CalcTextSize(prog);
                  ql->AddText(ImVec2(rowX + rowW - pSz.x - 6, textY), qlDim,
                              prog);
                  break;
                }
              }
            }

            ql->AddLine(ImVec2(rowX, rowY + rowH - 1),
                        ImVec2(rowX + rowW, rowY + rowH - 1), qlSep);

            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
              SoundManager::Play(SOUND_CLICK01);
              qlSelectedQuest = qi;
            }

            cy += rowH;
          }
        }
      }
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
      dl->AddText(ImVec2(titleX, py + 14.0f), IM_COL32(255, 210, 80, 255),
                  title);

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
        ImU32 bgCol =
            hovered ? IM_COL32(60, 50, 30, 255) : IM_COL32(35, 30, 20, 255);
        ImU32 borderCol =
            hovered ? IM_COL32(200, 170, 60, 255) : IM_COL32(120, 100, 60, 200);
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
        // Reset all state to prevent bleed-through between characters
        serverData.equipment.clear();
        serverData.npcs.clear();
        serverData.monsters.clear();
        g_monsterManager.ClearMonsters();
        g_npcManager.ClearSpawnedNpcs();
        serverData.hasSpawnPos = false;
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
          if (!TexValid(g_loadingTex)) {
            int idx = (rand() % 3) + 1;
            char path[256];
            snprintf(path, sizeof(path), "%s/Logo/Loading%02d.OZJ",
                     data_path.c_str(), idx);
            g_loadingTex = TextureLoader::LoadOZJ(path);
            if (!TexValid(g_loadingTex)) {
              snprintf(path, sizeof(path), "%s/Local/loading%02d.ozj",
                       data_path.c_str(), idx);
              g_loadingTex = TextureLoader::LoadOZJ(path);
            }
          }
          std::cout << "[State] -> LOADING (waiting for world data)"
                    << std::endl;
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
      float termW = 400.0f, termH = 32.0f;
      float termX = (dispSize.x - termW) * 0.5f;
      float termY = dispSize.y - 185.0f;

      ImGui::SetNextWindowPos(ImVec2(termX, termY));
      ImGui::SetNextWindowSize(ImVec2(termW, termH));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.75f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.47f, 0.39f, 0.24f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
      ImGui::Begin("##CommandTerminal", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoCollapse);

      ImGui::PushItemWidth(termW - 12.0f);
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

        if (cmdLower.find("/warp ") == 0 || cmdLower.find("/move ") == 0) {
          std::string arg = cmdLower.substr(cmdLower.find(' ') + 1);
          while (!arg.empty() && arg[0] == ' ')
            arg.erase(arg.begin());

          uint8_t mapId = 255;
          uint8_t sx = 0, sy = 0;
          if (arg == "lorencia" || arg == "0") {
            mapId = 0;
            sx = 125;
            sy = 125;
          } else if (arg == "dungeon" || arg == "dungeon 1" || arg == "1") {
            mapId = 1;
            sx = 108;
            sy = 247;
          } else if (arg == "dungeon 2" || arg == "dungeon2") {
            mapId = 1;
            sx = 231;
            sy = 126;
          } else if (arg == "dungeon 3" || arg == "dungeon3") {
            mapId = 1;
            sx = 233;
            sy = 23;
          } else if (arg == "devias" || arg == "2") {
            mapId = 2;
            sx = 210;
            sy = 40;
          }

          if (mapId != 255) {
            g_server.SendWarpCommand(mapId, sx, sy);
            SystemMessageLog::Log(MSG_SYSTEM, IM_COL32(100, 255, 100, 255),
                                  "Warping to %s...", arg.c_str());
          } else {
            SystemMessageLog::Log(MSG_SYSTEM, IM_COL32(255, 100, 100, 255),
                                  "Unknown map: %s", arg.c_str());
          }
        } else {
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
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
    }

    // Map coordinate display (top-right corner, always visible)
    {
      glm::vec3 hp = g_hero.GetPosition();
      int coordX = (int)(hp.z / 100.0f);
      int coordY = (int)(hp.x / 100.0f);
      char coordBuf[32];
      snprintf(coordBuf, sizeof(coordBuf), "%d, %d", coordX, coordY);
      ImDrawList *cdl = ImGui::GetForegroundDrawList();
      ImVec2 ds = ImGui::GetIO().DisplaySize;
      ImVec2 ts = ImGui::CalcTextSize(coordBuf);
      float px = ds.x - ts.x - 10.0f;
      float py = 10.0f;
      cdl->AddRectFilled(ImVec2(px - 6, py - 3), ImVec2(px + ts.x + 6, py + ts.y + 3),
                         IM_COL32(0, 0, 0, 120), 4.0f);
      cdl->AddText(ImVec2(px, py), IM_COL32(220, 220, 220, 220), coordBuf);
    }

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
                                       ph, job.hovered, job.itemLevel);
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
      ImGui_BackendNewFrame();
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
      float mx = mp.x, my = mp.y;
      bool over = false;
      if (g_showGameMenu || g_showCommandTerminal) {
        over = true; // full-screen overlay or terminal open
      } else {
        // InventoryUI panels (char info, inventory, shop)
        if (g_showCharInfo && InventoryUI::IsPointInPanel(
                                  mx, my, InventoryUI::GetCharInfoPanelX()))
          over = true;
        if (g_showInventory && InventoryUI::IsPointInPanel(
                                   mx, my, InventoryUI::GetInventoryPanelX()))
          over = true;
        if (g_shopOpen &&
            InventoryUI::IsPointInPanel(mx, my, InventoryUI::GetShopPanelX()))
          over = true;
        // Skill window (centered, roughly 500x400)
        if (g_showSkillWindow) {
          float sw = 492.0f, sh = 400.0f; // approximate
          float sx = (1270.0f - sw) * 0.5f, sy = (720.0f - sh) * 0.5f;
          if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh)
            over = true;
        }
        // Quest dialog
        if (g_questDialogOpen && g_qdPanelRect[2] > 0) {
          if (mx >= g_qdPanelRect[0] &&
              mx < g_qdPanelRect[0] + g_qdPanelRect[2] &&
              my >= g_qdPanelRect[1] &&
              my < g_qdPanelRect[1] + g_qdPanelRect[3])
            over = true;
        }
        // Quest log
        if (g_showQuestLog && g_qlPanelRect[2] > 0) {
          if (mx >= g_qlPanelRect[0] &&
              mx < g_qlPanelRect[0] + g_qlPanelRect[2] &&
              my >= g_qlPanelRect[1] &&
              my < g_qlPanelRect[1] + g_qlPanelRect[3])
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
      int winW, winH;
      glfwGetWindowSize(window, &winW, &winH);
      ImDrawList *dl = ImGui::GetForegroundDrawList();
      uint8_t a = (uint8_t)(g_mapTransitionAlpha * 255.0f);
      dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                        IM_COL32(0, 0, 0, a));
      // Show "Loading..." text when fully opaque
      if (g_mapTransitionAlpha > 0.9f) {
        const char *loadText = "Loading...";
        ImVec2 tsz = ImGui::CalcTextSize(loadText);
        dl->AddText(
            ImVec2(winW * 0.5f - tsz.x * 0.5f, winH * 0.5f - tsz.y * 0.5f),
            IM_COL32(220, 200, 160, (int)(g_mapTransitionAlpha * 255)),
            loadText);
      }
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
  // Show region name (Main 5.2: CUIMapName on map enter)
  // Note: if char is on dungeon map, ChangeMap() will override this with
  // "Dungeon"
  InventoryUI::ShowRegionName(GetMapConfig(g_currentMapId)->regionName);
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
  g_objectRenderer.ResetDoorStates();

  // Choose music based on spawn position terrain attribute (Lorencia initial
  // load)
  {
    glm::vec3 heroPos = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(heroPos.x / 100.0f);
    int gx = (int)(heroPos.z / 100.0f);
    bool inSafeZone =
        (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
        (g_terrainDataPtr->mapping.attributes[gz * S + gx] & 0x01) != 0;
    g_hero.SetInSafeZone(inSafeZone);
    // Start per-map sounds on initial load (config-driven)
    const MapConfig &sndCfg = *GetMapConfig(g_currentMapId);
    if (sndCfg.ambientLoop && !inSafeZone)
      SoundManager::PlayLoop(sndCfg.ambientLoop);
    if (inSafeZone && sndCfg.safeMusic)
      SoundManager::PlayMusic(g_dataPath + "/" + sndCfg.safeMusic);
    else if (!inSafeZone && sndCfg.wildMusic)
      SoundManager::PlayMusic(g_dataPath + "/" + sndCfg.wildMusic);

    // Apply atmosphere from config (clear color, fog, luminosity, post-proc,
    // roof hiding)
    ApplyMapAtmosphere(sndCfg);
  }

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

  // Rebuild roof hiding maps for this map only (no cross-map bleed)
  g_typeAlpha.clear();
  g_typeAlphaTarget.clear();
  for (int i = 0; i < cfg.roofTypeCount; ++i) {
    g_typeAlpha[cfg.roofTypes[i]] = 1.0f;
    g_typeAlphaTarget[cfg.roofTypes[i]] = 1.0f;
  }

  // Set map ID on all subsystems
  g_objectRenderer.SetMapId(cfg.mapId);
  g_boidManager.SetMapId(cfg.mapId);
  g_monsterManager.SetMapId(cfg.mapId);
  g_npcManager.SetMapId(cfg.mapId);
  g_hero.SetMapId(cfg.mapId);

  // Region name display
  InventoryUI::ShowRegionName(cfg.regionName);
}

// ═══════════════════════════════════════════════════════════════════
// ChangeMap — called when server sends MAP_CHANGE packet (0x1C)
// Reloads terrain, objects, grass, fire, lights for the new map.
// ═══════════════════════════════════════════════════════════════════

static void ChangeMap(uint8_t mapId, uint8_t spawnX, uint8_t spawnY) {
  const MapConfig &cfg = *GetMapConfig(mapId);
  std::string data_path = g_dataPath;
  int fileWorldId = mapId + 1; // map 0 → World1, map 1 → World2

  std::cout << "[ChangeMap] Transitioning to map " << (int)mapId << " ("
            << cfg.regionName << ", World" << fileWorldId << ") spawn=("
            << (int)spawnX << "," << (int)spawnY << ")" << std::endl;

  // Stop hero movement
  g_hero.StopMoving();

  // ── Phase 1: Cleanup old world data ──
  g_monsterManager.ClearMonsters();
  g_npcManager.ClearSpawnedNpcs();
  g_fireEffect.ClearEmitters();
  for (int i = 0; i < MAX_GROUND_ITEMS; i++)
    g_groundItems[i].active = false;
  g_objectRenderer.Cleanup();
  g_grass.Cleanup();

  // ── Phase 2: Load new terrain ──
  auto newTerrain = std::make_unique<TerrainData>(
      TerrainParser::LoadWorld(fileWorldId, data_path));
  auto originalAttrs = newTerrain->mapping.attributes;
  std::vector<bool> bridgeZone;
  ReconstructBridgeAttributes(*newTerrain, cfg, bridgeZone);

  // Patch tile 255 in bridge zone
  if (!bridgeZone.empty()) {
    auto &l1 = newTerrain->mapping.layer1;
    auto &l2 = newTerrain->mapping.layer2;
    for (int i = 0; i < 256 * 256; i++) {
      if (!bridgeZone[i] || (l1[i] < 255 && l2[i] < 255)) continue;
      int bz = i / 256, bx = i % 256;
      uint8_t best1 = l1[i], best2 = l2[i];
      float bestDist = 999.0f;
      for (int dz = -10; dz <= 10; ++dz) {
        for (int dx = -10; dx <= 10; ++dx) {
          int nz = bz + dz, nx = bx + dx;
          if (nz < 0 || nz >= 256 || nx < 0 || nx >= 256) continue;
          int ni = nz * 256 + nx;
          if (bridgeZone[ni] || l1[ni] >= 255) continue;
          float d = std::sqrt((float)(dz * dz + dx * dx));
          if (d < bestDist) { bestDist = d; best1 = l1[ni]; best2 = l2[ni]; }
        }
      }
      if (bestDist < 999.0f) {
        if (l1[i] >= 255) l1[i] = best1;
        if (l2[i] >= 255) l2[i] = best2;
      }
    }
  }

  g_terrainDataOwned = std::move(newTerrain);
  g_terrainDataPtr = g_terrainDataOwned.get();

  // ── Phase 3: Reload terrain renderer ──
  g_terrain.Load(*g_terrainDataPtr, fileWorldId, data_path, originalAttrs,
                 bridgeZone);

  // ── Phase 4: Reload objects (config-driven path selection) ──
  g_objectRenderer.Init();
  g_objectRenderer.SetMapId(cfg.mapId);
  g_objectRenderer.SetTerrainLightmap(g_terrainDataPtr->lightmap);
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

  // ── Phase 5: Reload grass (config-driven) ──
  g_grass.Init();
  if (cfg.hasGrass) {
    auto occGrid = BuildObjectOccupancy(g_objectRenderer.GetInstances());
    g_grass.Load(*g_terrainDataPtr, fileWorldId, data_path, &occGrid);
  }

  // Doors (config-driven)
  if (cfg.hasDoors)
    g_objectRenderer.InitDoors();

  // ── Phase 6: Register fire/smoke emitters from new objects ──
  for (auto &inst : g_objectRenderer.GetInstances()) {
    auto &offsets = GetFireOffsets(inst.type, mapId);
    for (auto &off : offsets) {
      glm::vec3 worldPos = glm::vec3(inst.modelMatrix[3]);
      glm::mat3 rot;
      for (int c = 0; c < 3; c++)
        rot[c] = glm::normalize(glm::vec3(inst.modelMatrix[c]));
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

  // Dungeon trap VFX emitters (config-driven)
  if (cfg.hasDungeonTraps) {
    for (auto &inst : g_objectRenderer.GetInstances()) {
      glm::vec3 trapPos = glm::vec3(inst.modelMatrix[3]);
      if (inst.type == 39)
        g_fireEffect.AddEmitter(trapPos + glm::vec3(0.0f, 50.0f, 0.0f));
      else if (inst.type == 51)
        g_fireEffect.AddEmitter(trapPos + glm::vec3(0.0f, 30.0f, 0.0f));
    }
  }

  // ── Phase 7: Collect point lights ──
  g_pointLights.clear();
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

  // ── Phase 8: Re-point subsystems to new terrain data ──
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
  RayPicker::Init(g_terrainDataPtr, &g_camera, &g_npcManager, &g_monsterManager,
                  g_groundItems, MAX_GROUND_ITEMS, &g_objectRenderer);

  // ── Phase 9: Place hero at spawn position ──
  float heroX = (float)spawnY * 100.0f;
  float heroZ = (float)spawnX * 100.0f;
  g_hero.SetPosition(glm::vec3(heroX, 0.0f, heroZ));
  g_hero.SnapToTerrain();
  g_camera.SetPosition(g_hero.GetPosition());
  // Note: do NOT call ResetDoorStates() here — InitDoors() already sets
  // soundPlayed=true and doorCooldown=1.5s to suppress door sounds on load.

  // ── Phase 10: Sound/music transition (config-driven) ──
  SoundManager::StopAll();
  SoundManager::StopMusic();
  {
    glm::vec3 hp = g_hero.GetPosition();
    const int S = TerrainParser::TERRAIN_SIZE;
    int gz = (int)(hp.x / 100.0f), gx = (int)(hp.z / 100.0f);
    bool inSafe =
        (gx >= 0 && gz >= 0 && gx < S && gz < S) &&
        (g_terrainDataPtr->mapping.attributes[gz * S + gx] & 0x01) != 0;
    g_hero.SetInSafeZone(inSafe);
    if (cfg.ambientLoop && !inSafe)
      SoundManager::PlayLoop(cfg.ambientLoop);
    if (inSafe && cfg.safeMusic)
      SoundManager::CrossfadeTo(data_path + "/" + cfg.safeMusic);
    else if (!inSafe && cfg.wildMusic)
      SoundManager::CrossfadeTo(data_path + "/" + cfg.wildMusic);
    else if (!inSafe && cfg.safeMusic)
      SoundManager::CrossfadeTo(data_path + "/" + cfg.safeMusic);
  }

  // ── Phase 11: Apply atmosphere + map state (config-driven) ──
  ApplyMapAtmosphere(cfg);

  std::cout << "[ChangeMap] Map " << (int)mapId
            << " loaded: " << g_objectRenderer.GetInstanceCount()
            << " objects, " << g_pointLights.size() << " lights, "
            << g_fireEffect.GetEmitterCount() << " fire emitters" << std::endl;
}
