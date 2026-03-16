#include "BMDParser.hpp"
#include "BMDUtils.hpp"
#include "MeshBuffers.hpp"
#include "Screenshot.hpp"
#include "Shader.hpp"
#include "TextureLoader.hpp"
#include "ViewerCommon.hpp"
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
#include <fstream>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

static const std::string DATA_PATH = "Data/Player/";
static const int WIN_WIDTH = 1280;
static const int WIN_HEIGHT = 720;

// 0.97d classes: 4 base + 3 second class (MG has no 2nd class)
enum CharClass {
  CLASS_DW = 0,  // Dark Wizard  → Class01
  CLASS_DK,      // Dark Knight  → Class02
  CLASS_ELF,     // Fairy Elf    → Class03
  CLASS_MG,      // Magic Gladiator → Class04
  CLASS_SM,      // Soul Master  → Class201 (DW 2nd)
  CLASS_BK,      // Blade Knight → Class202 (DK 2nd)
  CLASS_ME,      // Muse Elf     → Class203 (ELF 2nd)
  CLASS_COUNT
};

static const char *kClassNames[] = {"Dark Wizard",     "Dark Knight",
                                    "Fairy Elf",       "Magic Gladiator",
                                    "Soul Master (2)", "Blade Knight (2)",
                                    "Muse Elf (2)"};

static const char *kClassSuffix[] = {"Class01", "Class02", "Class03",
                                     "Class04", "Class201", "Class202",
                                     "Class203"};

// Body part slot names and BMD prefix
// PART_HEAD is a separate slot for the base head model (HelmClassXX.bmd),
// rendered underneath "accessory" helms that don't cover the full head.
enum BodyPart { PART_HELM = 0, PART_ARMOR, PART_PANTS, PART_GLOVES, PART_BOOTS, PART_HEAD, PART_COUNT };
static const char *kPartPrefix[] = {"Helm", "Armor", "Pant", "Glove", "Boot", "Helm"};
static const char *kPartNames[] = {"Helm", "Armor", "Pants", "Gloves", "Boots", "Head"};

// --- Armor set definitions (0.97d scope) ---
// File naming: {Part}{fileSuffix}.bmd — e.g. "Male01" → ArmorMale01.bmd
// fileSuffix=nullptr means naked (uses class-specific base model)
struct ArmorSetDef {
  const char *name;
  const char *fileSuffix;   // "Male01", "Elf03", "ElfC01", etc. (nullptr = naked)
  bool dk, dw, elf, mg;     // which base classes can wear this
  bool showHead;             // true = render base head (HelmClassXX) underneath helm
};

// Class availability for 2nd classes: SM inherits DW, BK inherits DK, ME inherits ELF
static bool CanClassWearSet(int classIdx, const ArmorSetDef &set) {
  switch (classIdx) {
  case CLASS_DW: return set.dw;
  case CLASS_DK: return set.dk;
  case CLASS_ELF: return set.elf;
  case CLASS_MG: return set.mg;
  case CLASS_SM: return set.dw;  // Soul Master = DW 2nd class
  case CLASS_BK: return set.dk;  // Blade Knight = DK 2nd class
  case CLASS_ME: return set.elf; // Muse Elf = ELF 2nd class
  default: return false;
  }
}

// 0.97d armor sets: base (0-14) + 2nd class tier (15-20)
// Reference: ZzzOpenData.cpp, item indices, MU wiki
// showHead whitelist from ZzzCharacter.cpp:11718 — MODEL_HELM indices 0,2,10-13
//   Male01=idx0(Bronze), Male03=idx2(Pad), Elf01-04=idx10-13
static const ArmorSetDef kArmorSets[] = {
    // name                suffix      DK    DW    ELF   MG    showHead
    {"Naked",              nullptr,    true, true, true, true, false},
    // --- DK sets (ArmorMale) ---
    {"Bronze",             "Male01",   true, false,false,true, true },
    {"Dragon",             "Male02",   true, false,false,true, false},
    {"Leather",            "Male06",   true, false,false,true, false},
    {"Scale",              "Male07",   true, false,false,true, false},
    {"Brass",              "Male09",   true, false,false,true, false},
    {"Plate",              "Male10",   true, false,false,true, false},
    // --- DW sets (ArmorMale) ---
    {"Pad",                "Male03",   false,true, false,true, true },
    {"Legendary",          "Male04",   false,true, false,true, false},
    {"Bone",               "Male05",   false,true, false,true, false},
    {"Sphinx",             "Male08",   false,true, false,true, false},
    // --- ELF sets (ArmorElf) ---
    {"Vine",               "Elf01",    false,false,true, false,true },
    {"Silk",               "Elf02",    false,false,true, false,true },
    {"Wind",               "Elf03",    false,false,true, false,true },
    {"Spirit",             "Elf04",    false,false,true, false,true },
    {"Guardian",           "Elf05",    false,false,true, false,false},
    // --- 2nd class tier sets (DK/BK) ---
    {"Storm Crow",         "Male16",   true, false,false,false,false},
    {"Black Dragon",       "Male17",   true, false,false,false,false},
    // --- 2nd class tier sets (DW/SM) ---
    {"Dark Phoenix",       "Male18",   false,true, false,false,false},
    {"Grand Soul",         "Male19",   false,true, false,false,false},
    // --- 2nd class tier sets (ELF/ME) ---
    {"Divine",             "ElfC01",   false,false,true, false,false},
    {"Thunder Hawk",       "ElfC02",   false,false,true, false,false},
};
static const int kNumArmorSets = sizeof(kArmorSets) / sizeof(kArmorSets[0]);

// --- Weapon system ---
static const std::string DATA_ITEM_PATH = "Data/Item/";

enum WeaponCategory {
  WCAT_NONE = 0,
  WCAT_SWORD,
  WCAT_AXE,
  WCAT_MACE,
  WCAT_SPEAR,
  WCAT_STAFF,
  WCAT_BOW,
  WCAT_CROSSBOW,
  WCAT_SHIELD,
  WCAT_COUNT
};

static const char *kWeaponCatNames[] = {
    "None", "Sword", "Axe", "Mace", "Spear",
    "Staff", "Bow",  "Crossbow", "Shield"};

// Per-category attachment config (from ZzzCharacter.cpp RenderLinkObject)
struct WeaponCatConfig {
  int bone;          // Attachment bone (28=R Hand, 37=L Hand)
  glm::vec3 rot;     // Euler angles (degrees) for AngleMatrix
  glm::vec3 offset;  // Translation in bone-local space
};

// Calibrated for 0.97d Player.bmd skeleton via runtime bone matrix analysis.
// Weapon BMDs have their own bone rotation (e.g. Sword01 maps +Z→-Y).
// Identity offset rotation (0,0,0) lets the weapon bone handle orientation:
//   Action 5 (2H idle): blade 99% upward (bone -Y ≈ MU +Z)
//   Action 4 (1H idle): blade slightly tilted (natural relaxed grip)
static const WeaponCatConfig kWeaponConfigs[] = {
    // NONE
    {0, {0, 0, 0}, {0, 0, 0}},
    // SWORD: right hand bone 33 (knife_gdf)
    {33, {0, 0, 0}, {0, 0, 0}},
    // AXE: same as sword
    {33, {0, 0, 0}, {0, 0, 0}},
    // MACE: same as sword
    {33, {0, 0, 0}, {0, 0, 0}},
    // SPEAR: right hand bone
    {33, {0, 0, 0}, {0, 0, 0}},
    // STAFF: left hand bone 42 (hand_bofdgne01)
    {42, {0, 0, 0}, {0, 0, 0}},
    // BOW: left hand bone — may need different rotation
    {42, {0, 0, 0}, {0, 0, 0}},
    // CROSSBOW: same as bow
    {42, {0, 0, 0}, {0, 0, 0}},
    // SHIELD: left hand bone
    {42, {0, 0, 0}, {0, 0, 0}},
};

// Weapon file lists per category (0.97d scope — core numbered items)
static const std::vector<const char *> kWeaponFiles[] = {
    // NONE
    {},
    // SWORD (01-20)
    {"Sword01.bmd", "Sword02.bmd", "Sword03.bmd", "Sword04.bmd",
     "Sword05.bmd", "Sword06.bmd", "Sword07.bmd", "Sword08.bmd",
     "Sword09.bmd", "Sword10.bmd", "Sword11.bmd", "Sword12.bmd",
     "Sword13.bmd", "Sword14.bmd", "Sword15.bmd", "Sword16.bmd",
     "Sword17.bmd", "Sword18.bmd", "Sword19.bmd", "Sword20.bmd"},
    // AXE (01-09)
    {"Axe01.bmd", "Axe02.bmd", "Axe03.bmd", "Axe04.bmd", "Axe05.bmd",
     "Axe06.bmd", "Axe07.bmd", "Axe08.bmd", "Axe09.bmd"},
    // MACE (01-14)
    {"Mace01.bmd", "Mace02.bmd", "Mace03.bmd", "Mace04.bmd", "Mace05.bmd",
     "Mace06.bmd", "Mace07.bmd", "Mace08.bmd", "Mace09.bmd", "Mace10.bmd",
     "Mace11.bmd", "Mace12.bmd", "Mace13.bmd", "Mace14.bmd"},
    // SPEAR (01-10)
    {"Spear01.bmd", "Spear02.bmd", "Spear03.bmd", "Spear04.bmd",
     "Spear05.bmd", "Spear06.bmd", "Spear07.bmd", "Spear08.bmd",
     "Spear09.bmd", "Spear10.bmd"},
    // STAFF (01-12)
    {"Staff01.bmd", "Staff02.bmd", "Staff03.bmd", "Staff04.bmd",
     "Staff05.bmd", "Staff06.bmd", "Staff07.bmd", "Staff08.bmd",
     "Staff09.bmd", "Staff10.bmd", "Staff11.bmd", "Staff12.bmd"},
    // BOW (01-07)
    {"Bow01.bmd", "Bow02.bmd", "Bow03.bmd", "Bow04.bmd", "Bow05.bmd",
     "Bow06.bmd", "Bow07.bmd"},
    // CROSSBOW (01-07)
    {"CrossBow01.bmd", "CrossBow02.bmd", "CrossBow03.bmd", "CrossBow04.bmd",
     "CrossBow05.bmd", "CrossBow06.bmd", "CrossBow07.bmd"},
    // SHIELD (01-15)
    {"Shield01.bmd", "Shield02.bmd", "Shield03.bmd", "Shield04.bmd",
     "Shield05.bmd", "Shield06.bmd", "Shield07.bmd", "Shield08.bmd",
     "Shield09.bmd", "Shield10.bmd", "Shield11.bmd", "Shield12.bmd",
     "Shield13.bmd", "Shield14.bmd", "Shield15.bmd"},
};

// Per-class weapon availability
// DK/BK: Sword, Axe, Mace, Spear, Shield
// DW/SM: Staff, Shield
// ELF/ME: Bow, Crossbow, Spear, Shield
// MG: Sword, Axe, Mace, Spear (one-hand, no shield)
static bool CanClassUseWeapon(int classIdx, int wcat) {
  if (wcat == WCAT_NONE)
    return true;
  bool isDK = (classIdx == CLASS_DK || classIdx == CLASS_BK);
  bool isDW = (classIdx == CLASS_DW || classIdx == CLASS_SM);
  bool isElf = (classIdx == CLASS_ELF || classIdx == CLASS_ME);
  bool isMG = (classIdx == CLASS_MG);
  switch (wcat) {
  case WCAT_SWORD:
  case WCAT_AXE:
  case WCAT_MACE:
    return isDK || isMG;
  case WCAT_SPEAR:
    return isDK || isElf || isMG;
  case WCAT_STAFF:
    return isDW;
  case WCAT_BOW:
  case WCAT_CROSSBOW:
    return isElf;
  case WCAT_SHIELD:
    return isDK || isDW || isElf;
  default:
    return false;
  }
}

// Animation category for UI grouping
struct AnimEntry {
  const char *name;
  int actionIndex;
};

struct AnimCategory {
  const char *name;
  std::vector<AnimEntry> entries;
};

// Female classes: ELF and ME (Muse Elf = ELF 2nd class)
static bool IsFemaleClass(int classIdx) {
  return classIdx == CLASS_ELF || classIdx == CLASS_ME;
}

// Build animation categories with correct indices from _enum.h.
// Skills tab is class-aware: DK=sword, DW=magic, ELF=elf cast, MG=hybrid.
// Reference: _enum.h (PLAYER_SET=0, sequential), SkillManager.h (AT_SKILL_*)
static std::vector<AnimCategory> BuildAnimCategories(int classIdx) {
  bool female = IsFemaleClass(classIdx);
  bool isDK = (classIdx == CLASS_DK || classIdx == CLASS_BK);
  bool isDW = (classIdx == CLASS_DW || classIdx == CLASS_SM);
  bool isElf = (classIdx == CLASS_ELF || classIdx == CLASS_ME);
  bool isMG = (classIdx == CLASS_MG);

  std::vector<AnimCategory> cats;

  // --- Idle/Stop ---
  {
    AnimCategory cat{"Idle", {}};
    cat.entries.push_back({female ? "Stop (Female)" : "Stop (Male)",
                           female ? 2 : 1});
    cat.entries.push_back({"Sword", 4});
    cat.entries.push_back({"Two-Hand Sword", 5});
    cat.entries.push_back({"Spear", 6});
    cat.entries.push_back({"Scythe/Staff", 7});
    cat.entries.push_back({"Bow", 8});
    cat.entries.push_back({"Crossbow", 9});
    cat.entries.push_back({"Flying", 11});
    cats.push_back(cat);
  }

  // --- Walk ---
  {
    AnimCategory cat{"Walk", {}};
    cat.entries.push_back({female ? "Walk (Female)" : "Walk (Male)",
                           female ? 16 : 15});
    cat.entries.push_back({"Sword", 17});
    cat.entries.push_back({"Two-Hand Sword", 18});
    cat.entries.push_back({"Spear", 19});
    cat.entries.push_back({"Scythe/Staff", 20});
    cat.entries.push_back({"Bow", 21});
    cat.entries.push_back({"Crossbow", 22});
    cats.push_back(cat);
  }

  // --- Run ---
  {
    AnimCategory cat{"Run", {}};
    cat.entries.push_back({"Run", 25});
    cat.entries.push_back({"Sword", 26});
    cat.entries.push_back({"Dual Wield", 27});
    cat.entries.push_back({"Two-Hand Sword", 28});
    cat.entries.push_back({"Spear", 29});
    cat.entries.push_back({"Bow", 30});
    cat.entries.push_back({"Crossbow", 31});
    cat.entries.push_back({"Fly", 34});
    cats.push_back(cat);
  }

  // --- Combat ---
  {
    AnimCategory cat{"Combat", {}};
    cat.entries.push_back({"Fist", 38});
    cat.entries.push_back({"Sword R1", 39});
    cat.entries.push_back({"Sword R2", 40});
    cat.entries.push_back({"Sword L1", 41});
    cat.entries.push_back({"Sword L2", 42});
    cat.entries.push_back({"Two-Hand 1", 43});
    cat.entries.push_back({"Two-Hand 2", 44});
    cat.entries.push_back({"Two-Hand 3", 45});
    cat.entries.push_back({"Spear", 46});
    cat.entries.push_back({"Scythe 1", 47});
    cat.entries.push_back({"Scythe 2", 48});
    cat.entries.push_back({"Scythe 3", 49});
    cat.entries.push_back({"Bow", 50});
    cat.entries.push_back({"Crossbow", 51});
    cats.push_back(cat);
  }

  // --- Skills (class-specific) ---
  // DK/BK: sword skills. DW/SM: magic. ELF/ME: elf cast. MG: sword + magic.
  // Reference: SkillManager.h AT_SKILL_*, ZzzCharacter.cpp SetPlayerAttack
  {
    AnimCategory cat{"Skills", {}};

    // DK sword skills (also available to MG)
    // Reference: _enum.h PLAYER_ATTACK_SKILL_SWORD1=60..SWORD5=64
    if (isDK || isMG) {
      cat.entries.push_back({"Falling Slash", 60});
      cat.entries.push_back({"Lunge", 61});
      cat.entries.push_back({"Uppercut", 62});
      cat.entries.push_back({"Cyclone", 63});
      cat.entries.push_back({"Slash", 64});
      cat.entries.push_back({"Twisting Slash", 65}); // PLAYER_ATTACK_SKILL_WHEEL
      cat.entries.push_back({"Rageful Blow", 66});   // PLAYER_ATTACK_SKILL_FURY_STRIKE
      cat.entries.push_back({"Spear Skill", 70});    // PLAYER_ATTACK_SKILL_SPEAR
      cat.entries.push_back({"Death Stab", 71});     // PLAYER_ATTACK_ONETOONE
    }

    // DW magic skills (also available to MG except Teleport)
    // Action indices from _enum.h (0.97d, no YDG_ADD_SKILL_RIDING_ANIMATIONS ifdef)
    if (isDW || isMG) {
      cat.entries.push_back({"Energy Ball", 146});   // PLAYER_SKILL_HAND1
      cat.entries.push_back({"Magic Cast 2", 147});  // PLAYER_SKILL_HAND2
      cat.entries.push_back({"Staff Cast 1", 148});  // PLAYER_SKILL_WEAPON1
      cat.entries.push_back({"Staff Cast 2", 149});  // PLAYER_SKILL_WEAPON2
      cat.entries.push_back({"Aqua Beam", 152});     // PLAYER_SKILL_FLASH
      cat.entries.push_back({"Inferno", 153});       // PLAYER_SKILL_INFERNO
      cat.entries.push_back({"Hell Fire", 154});     // PLAYER_SKILL_HELL
    }

    // Teleport — DW/SM only (not MG)
    if (isDW) {
      cat.entries.push_back({"Teleport", 151});      // PLAYER_SKILL_TELEPORT
    }

    // ELF/ME skills
    if (isElf) {
      cat.entries.push_back({"Heal", 67});           // PLAYER_SKILL_VITALITY
      cat.entries.push_back({"Elf Buff", 150});      // PLAYER_SKILL_ELF1 (Greater Def/Dmg)
      cat.entries.push_back({"Penetration", 50});    // bow attack anim
      cat.entries.push_back({"Ice Arrow", 51});      // crossbow attack anim
    }

    cats.push_back(cat);
  }

  // --- Emotes (male/female variants) ---
  {
    AnimCategory cat{"Emotes", {}};
    cat.entries.push_back({"Defense", 186});
    cat.entries.push_back({"Greeting", female ? 188 : 187});
    cat.entries.push_back({"Goodbye", female ? 190 : 189});
    cat.entries.push_back({"Clap", female ? 192 : 191});
    cat.entries.push_back({"Cheer", female ? 194 : 193});
    cat.entries.push_back({"Direction", female ? 196 : 195});
    cat.entries.push_back({"Gesture", female ? 198 : 197});
    cat.entries.push_back({"Cry", female ? 202 : 201});
    cat.entries.push_back({"Awkward", female ? 204 : 203});
    cat.entries.push_back({"See", female ? 206 : 205});
    cat.entries.push_back({"Win", female ? 208 : 207});
    cat.entries.push_back({"Smile", female ? 210 : 209});
    cat.entries.push_back({"Sleep", female ? 212 : 211});
    cat.entries.push_back({"Cold", female ? 214 : 213});
    cat.entries.push_back({"Again", female ? 216 : 215});
    cat.entries.push_back({"Respect", 217});
    cat.entries.push_back({"Salute", 218});
    cat.entries.push_back({"Scissors", 219});
    cat.entries.push_back({"Rock", 220});
    cat.entries.push_back({"Paper", 221});
    cats.push_back(cat);
  }

  // --- Other ---
  {
    AnimCategory cat{"Other", {}};
    cat.entries.push_back({"Shock", 230});
    cat.entries.push_back({"Die 1", 231});
    cat.entries.push_back({"Die 2", 232});
    cat.entries.push_back({"Sit 1", female ? 235 : 233});
    cat.entries.push_back({"Sit 2", female ? 236 : 234});
    cat.entries.push_back({"Healing", female ? 238 : 237});
    cat.entries.push_back({"Pose", female ? 240 : 239});
    cats.push_back(cat);
  }

  return cats;
}

class CharacterViewer {
public:
  bool autoScreenshot = false; // --screenshots mode

  void Run() {
    if (!InitWindow())
      return;

    ActivateMacOSApp();

    // Init ImGui with BGFX backend
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplBgfx_Init(30, "shaders"); // view ID 30 for ImGui

    shader = Shader::Load("vs_model.bin", "fs_model.bin");

    animCategories = BuildAnimCategories(currentClass);

    // Load skeleton + default class
    LoadSkeleton();
    if (!skeleton) {
      std::cerr << "[CharViewer] Failed to load Player.bmd skeleton"
                << std::endl;
      ImGui_ImplBgfx_Shutdown();
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      glfwDestroyWindow(window);
      glfwTerminate();
      return;
    }

    LoadClass(currentClass);

    // Auto-screenshot mode: equip weapon, capture from multiple angles
    if (autoScreenshot) {
      RunAutoScreenshots();
      UnloadParts();
      ImGui_ImplBgfx_Shutdown();
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      bgfx::shutdown();
      glfwDestroyWindow(window);
      glfwTerminate();
      return;
    }

    while (!glfwWindowShouldClose(window)) {
      float now = glfwGetTime();
      deltaTime = now - lastFrame;
      lastFrame = now;

      glfwPollEvents();
      RenderScene();
      Screenshot::TickRecording(window);
      RenderUI();
      bgfx::frame();
    }

    UnloadParts();
    ImGui_ImplBgfx_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  GLFWwindow *window = nullptr;
  std::unique_ptr<Shader> shader;

  // Skeleton (Player.bmd — bones + actions only, zero meshes)
  std::unique_ptr<BMDData> skeleton;
  int totalActions = 0;

  // 5 body part slots
  struct BodyPartSlot {
    std::unique_ptr<BMDData> bmd;
    std::vector<MeshBuffers> meshBuffers;
    std::string filename;
    bool loaded = false;
  };
  BodyPartSlot parts[PART_COUNT];

  // Class + armor selection
  int currentClass = CLASS_DK;
  int currentArmorSet = 0; // index into kArmorSets (0 = Naked)

  // Animation state
  int currentAction = 0;
  float animFrame = 0.0f;
  float animSpeed = 4.0f;
  bool animPlaying = true;
  int currentNumKeys = 0;

  // Animation categories
  std::vector<AnimCategory> animCategories;

  // Orbit camera
  OrbitCamera camera;

  // Mouse
  bool dragging = false;
  double lastMouseX = 0, lastMouseY = 0;

  // Timing
  float deltaTime = 0.0f;
  float lastFrame = 0.0f;

  // GIF recording
  int gifFrameTarget = 72;
  float gifScaleSetting = 0.5f;
  int gifFpsSetting = 12;

  // Weapon state
  int weaponCategory = WCAT_NONE;
  int weaponIndex = 0;
  std::unique_ptr<BMDData> weaponBmd;
  std::vector<MeshBuffers> weaponMeshBuffers;
  // Editable weapon config (for live tweaking via debug sliders)
  glm::vec3 weaponRot{0, 0, 0};
  glm::vec3 weaponOffset{0, 0, 0};
  int weaponBone = 33;

  // BodyAngle: character facing rotation applied to root bone (Main 5.2 match)
  glm::vec3 bodyAngle{0, 0, 0};

  // --- Init ---

  bool InitWindow() {
    if (!glfwInit())
      return false;

    // BGFX manages the GPU — no OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "MU Character Viewer",
                              nullptr, nullptr);
    if (!window)
      return false;

    // Initialize BGFX
    bgfx::renderFrame(); // Single-threaded mode: call before bgfx::init

    bgfx::Init init;
    init.type = bgfx::RendererType::Metal;
#ifdef __APPLE__
    init.platformData.nwh = glfwGetCocoaWindow(window);
#endif

    int initW, initH;
    glfwGetFramebufferSize(window, &initW, &initH);
    init.resolution.width = initW;
    init.resolution.height = initH;
    init.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;

    if (!bgfx::init(init)) {
      std::cerr << "[CharViewer] Failed to initialize BGFX" << std::endl;
      glfwDestroyWindow(window);
      glfwTerminate();
      return false;
    }

    bgfx::setDebug(BGFX_DEBUG_NONE);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                        0x262E38FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, uint16_t(initW), uint16_t(initH));
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

    // Prime the Metal backbuffer
    for (int i = 0; i < 3; ++i) {
      bgfx::touch(0);
      bgfx::frame();
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);

    return true;
  }

  // --- Skeleton ---

  void LoadSkeleton() {
    std::string path = DATA_PATH + "player.bmd";
    skeleton = BMDParser::Parse(path);
    if (skeleton) {
      totalActions = (int)skeleton->Actions.size();
      std::cout << "[CharViewer] Player.bmd: " << skeleton->Bones.size()
                << " bones, " << totalActions << " actions, "
                << skeleton->Meshes.size() << " meshes" << std::endl;

      // Set initial action keyframes
      if (totalActions > 0) {
        currentNumKeys = skeleton->Actions[0].NumAnimationKeys;
      }
    }
  }

  // --- Body parts ---

  void UnloadParts() {
    for (int i = 0; i < PART_COUNT; ++i) {
      CleanupMeshBuffers(parts[i].meshBuffers);
      parts[i].bmd.reset();
      parts[i].filename.clear();
      parts[i].loaded = false;
    }
    UnloadWeapon();
  }

  void LoadClass(int classIdx) {
    currentClass = classIdx;

    // Rebuild animation categories for class-specific skills + male/female variants
    animCategories = BuildAnimCategories(classIdx);

    // Reset to Naked if current armor set not available for new class
    if (!CanClassWearSet(classIdx, kArmorSets[currentArmorSet]))
      currentArmorSet = 0; // Naked is always available

    // Reset weapon if new class can't use current weapon category
    if (!CanClassUseWeapon(classIdx, weaponCategory)) {
      LoadWeapon(WCAT_NONE, 0);
    }

    LoadArmorSet(currentArmorSet);

    // Reset animation to class-appropriate idle
    animFrame = 0.0f;
    SetAction(IsFemaleClass(classIdx) ? 2 : 1);
  }

  // Build filename for a body part: either ClassXX (naked) or Male##/Elf##/ElfC## (armor)
  std::string BuildPartFilename(int partIdx, int armorSetIdx) const {
    const auto &set = kArmorSets[armorSetIdx];

    if (!set.fileSuffix) {
      // Naked — use class-specific base model
      return std::string(kPartPrefix[partIdx]) + kClassSuffix[currentClass] +
             ".bmd";
    }

    // Armor set — e.g. "Armor" + "Male01" + ".bmd"
    return std::string(kPartPrefix[partIdx]) + set.fileSuffix + ".bmd";
  }

  void LoadArmorSet(int armorSetIdx) {
    UnloadParts();
    currentArmorSet = armorSetIdx;

    auto bones = ComputeBoneMatrices(skeleton.get(), 0, 0, bodyAngle);
    AABB totalAABB{};

    // Load 5 equipment body parts (Helm, Armor, Pants, Gloves, Boots)
    for (int p = 0; p < PART_BOOTS + 1; ++p) {
      std::string filename = BuildPartFilename(p, armorSetIdx);
      std::string fullPath = DATA_PATH + filename;

      auto bmd = BMDParser::Parse(fullPath);
      if (!bmd) {
        std::cerr << "[CharViewer] Failed to load: " << filename << std::endl;
        continue;
      }

      std::cout << "[CharViewer] Loaded " << filename << ": "
                << bmd->Meshes.size() << " meshes" << std::endl;

      for (auto &mesh : bmd->Meshes) {
        UploadMeshWithBones(mesh, DATA_PATH, bones, parts[p].meshBuffers,
                            totalAABB, true);
      }

      parts[p].bmd = std::move(bmd);
      parts[p].filename = filename;
      parts[p].loaded = true;
    }

    // Load base head model (HelmClassXX.bmd) for accessory helms that show
    // the head underneath. Naked doesn't need this — its helm IS the head.
    // Reference: ZzzCharacter.cpp:11718 SetCharacterScale() whitelist
    const auto &set = kArmorSets[armorSetIdx];
    if (set.fileSuffix && set.showHead) {
      std::string headFile =
          std::string("Helm") + kClassSuffix[currentClass] + ".bmd";
      std::string headPath = DATA_PATH + headFile;

      auto headBmd = BMDParser::Parse(headPath);
      if (headBmd) {
        std::cout << "[CharViewer] Head: " << headFile << " ("
                  << headBmd->Meshes.size() << " meshes)" << std::endl;
        for (auto &mesh : headBmd->Meshes) {
          UploadMeshWithBones(mesh, DATA_PATH, bones,
                              parts[PART_HEAD].meshBuffers, totalAABB, true);
        }
        parts[PART_HEAD].bmd = std::move(headBmd);
        parts[PART_HEAD].filename = headFile;
        parts[PART_HEAD].loaded = true;
      }
    }

    AutoFrame(totalAABB);
    UpdateWindowTitle();
  }

  void UpdateWindowTitle() {
    std::string title = "MU Character Viewer - " +
                        std::string(kClassNames[currentClass]) + " [" +
                        kArmorSets[currentArmorSet].name + "]";
    glfwSetWindowTitle(window, title.c_str());
  }

  void AutoFrame(const AABB &aabb) {
    glm::vec3 c = aabb.center();
    camera.center = glm::vec3(c.x, c.z, -c.y);
    float radius = aabb.radius();
    if (radius < 0.001f)
      radius = 100.0f;

    camera.distance = radius * 2.6f;
    camera.yaw = 180.0f;
    camera.pitch = -15.0f;
  }

  void SetAction(int action) {
    if (action < 0 || action >= totalActions)
      action = 0;
    currentAction = action;
    animFrame = 0.0f;
    currentNumKeys = skeleton->Actions[action].NumAnimationKeys;
  }

  // --- Weapon ---

  // Switch to weapon-appropriate idle action (matches original game behavior)
  // Reference: _enum.h PLAYER_STOP_SWORD=4, PLAYER_STOP_SPEAR=6, etc.
  void AutoSwitchWeaponAction(int wcat) {
    int idleAction = IsFemaleClass(currentClass) ? 2 : 1; // default unarmed
    switch (wcat) {
    case WCAT_SWORD:
    case WCAT_AXE:
    case WCAT_MACE:
    case WCAT_SHIELD:
      idleAction = 4;
      break; // PLAYER_STOP_SWORD
    case WCAT_SPEAR:
      idleAction = 6;
      break; // PLAYER_STOP_SPEAR
    case WCAT_STAFF:
      idleAction = 7;
      break; // PLAYER_STOP_SCYTHE
    case WCAT_BOW:
      idleAction = 8;
      break; // PLAYER_STOP_BOW
    case WCAT_CROSSBOW:
      idleAction = 9;
      break; // PLAYER_STOP_CROSSBOW
    }
    SetAction(idleAction);
  }

  void UnloadWeapon() {
    CleanupMeshBuffers(weaponMeshBuffers);
    weaponBmd.reset();
  }

  void LoadWeapon(int category, int index) {
    UnloadWeapon();
    weaponCategory = category;
    weaponIndex = index;

    if (category == WCAT_NONE) {
      AutoSwitchWeaponAction(WCAT_NONE);
      return;
    }

    const auto &files = kWeaponFiles[category];
    if (index < 0 || index >= (int)files.size())
      return;

    std::string path = DATA_ITEM_PATH + files[index];
    weaponBmd = BMDParser::Parse(path);
    if (!weaponBmd) {
      std::cerr << "[CharViewer] Failed to load weapon: " << files[index]
                << std::endl;
      return;
    }

    // Upload weapon meshes as dynamic (for per-frame re-skinning)
    AABB weaponAABB{};
    std::vector<BoneWorldMatrix> wBones;
    if (!weaponBmd->Bones.empty()) {
      wBones = ComputeBoneMatrices(weaponBmd.get());
    } else {
      BoneWorldMatrix identity{};
      identity[0] = {1, 0, 0, 0};
      identity[1] = {0, 1, 0, 0};
      identity[2] = {0, 0, 1, 0};
      wBones.push_back(identity);
    }

    for (auto &mesh : weaponBmd->Meshes) {
      UploadMeshWithBones(mesh, DATA_ITEM_PATH, wBones, weaponMeshBuffers,
                          weaponAABB, true);
    }

    // Apply category defaults to debug sliders
    const auto &cfg = kWeaponConfigs[category];
    weaponRot = cfg.rot;
    weaponOffset = cfg.offset;
    weaponBone = cfg.bone;

    // Diagnostic: dump bone 33 matrix for weapon attachment analysis
    {
      auto charBones = ComputeBoneMatricesInterpolated(skeleton.get(), 4, 0.0f);
      if (weaponBone < (int)charBones.size()) {
        auto &bm = charBones[weaponBone];
        printf("\n=== BONE %d MATRIX (Action 4, Frame 0) ===\n", weaponBone);
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", bm[0][0], bm[0][1], bm[0][2], bm[0][3]);
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", bm[1][0], bm[1][1], bm[1][2], bm[1][3]);
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", bm[2][0], bm[2][1], bm[2][2], bm[2][3]);
        // Show what each axis maps to
        glm::vec3 bX(bm[0][0], bm[1][0], bm[2][0]);
        glm::vec3 bY(bm[0][1], bm[1][1], bm[2][1]);
        glm::vec3 bZ(bm[0][2], bm[1][2], bm[2][2]);
        glm::vec3 pos(bm[0][3], bm[1][3], bm[2][3]);
        printf("  Bone X-axis: (%.4f, %.4f, %.4f)\n", bX.x, bX.y, bX.z);
        printf("  Bone Y-axis: (%.4f, %.4f, %.4f)\n", bY.x, bY.y, bY.z);
        printf("  Bone Z-axis: (%.4f, %.4f, %.4f)\n", bZ.x, bZ.y, bZ.z);
        printf("  Position:    (%.4f, %.4f, %.4f)\n", pos.x, pos.y, pos.z);
        // After model matrix (-90° X): MU(x,y,z) → GL(x,-z,y)
        printf("  GL-space X-axis: (%.4f, %.4f, %.4f)\n", bX.x, -bX.z, bX.y);
        printf("  GL-space Y-axis: (%.4f, %.4f, %.4f)\n", bY.x, -bY.z, bY.y);
        printf("  GL-space Z-axis: (%.4f, %.4f, %.4f)\n", bZ.x, -bZ.z, bZ.y);
      }

      // Also dump weapon bone 0
      if (weaponBmd && !weaponBmd->Bones.empty()) {
        auto wBones = ComputeBoneMatrices(weaponBmd.get());
        auto &wb = wBones[0];
        printf("=== WEAPON BONE 0 MATRIX ===\n");
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", wb[0][0], wb[0][1], wb[0][2], wb[0][3]);
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", wb[1][0], wb[1][1], wb[1][2], wb[1][3]);
        printf("  [%.6f, %.6f, %.6f, %.6f]\n", wb[2][0], wb[2][1], wb[2][2], wb[2][3]);
      }
      printf("===========================\n\n");
    }

    std::cout << "[CharViewer] Weapon: " << files[index] << " ("
              << weaponBmd->Meshes.size() << " meshes, "
              << weaponBmd->Bones.size() << " bones, bone=" << weaponBone
              << ")" << std::endl;

    // Auto-switch to weapon-appropriate idle animation
    // Original game changes pose when weapon is equipped
    AutoSwitchWeaponAction(category);
  }

  // --- Helper: set all model uniforms before a bgfx::submit ---
  void SetModelUniforms(const glm::vec3 &eye) {
    shader->setVec4("u_params", glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));       // objectAlpha, blendMeshLight
    shader->setVec4("u_params2", glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));      // luminosity, pointLights(0)
    shader->setVec4("u_lightPos", glm::vec4(eye + glm::vec3(0, 200, 0), 0.0f));
    shader->setVec4("u_lightColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_viewPos", glm::vec4(eye, 0.0f));
    shader->setVec4("u_terrainLight", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_baseTint", glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shader->setVec4("u_fogParams", glm::vec4(0.0f));
    shader->setVec4("u_fogColor", glm::vec4(0.0f));
    shader->setVec4("u_texCoordOffset", glm::vec4(0.0f));
    shader->setVec4("u_glowColor", glm::vec4(0.0f));
    shader->setVec4("u_shadowParams", glm::vec4(0.0f));
  }

  // --- Helper: submit a single MeshBuffers draw ---
  void SubmitMesh(const MeshBuffers &mb, const glm::mat4 &modelMat, const glm::vec3 &eye) {
    bgfx::setTransform(glm::value_ptr(modelMat));

    if (mb.isDynamic)
      bgfx::setVertexBuffer(0, mb.dynVbo);
    else
      bgfx::setVertexBuffer(0, mb.vbo);
    bgfx::setIndexBuffer(mb.ebo);

    shader->setTexture(0, "s_texColor", mb.texture);
    SetModelUniforms(eye);

    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_MSAA;

    if (mb.noneBlend) {
      // No blending, opaque
    } else if (mb.bright) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
      state &= ~(uint64_t)BGFX_STATE_WRITE_Z;
    } else if (mb.hasAlpha) {
      state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }

    bgfx::setState(state);
    bgfx::submit(0, shader->program);
  }

  // --- Rendering ---

  void RenderScene() {
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    bgfx::setViewRect(0, 0, 0, uint16_t(fbWidth), uint16_t(fbHeight));
    bgfx::touch(0);

    // Advance animation
    if (animPlaying && currentNumKeys > 1) {
      animFrame += animSpeed * deltaTime;
      if (animFrame >= (float)currentNumKeys)
        animFrame = std::fmod(animFrame, (float)currentNumKeys);
    }

    // Compute skeleton bones for current frame (with BodyAngle on root bone)
    auto bones = ComputeBoneMatricesInterpolated(skeleton.get(), currentAction,
                                                 animFrame, bodyAngle);

    // Re-skin all body part meshes
    for (int p = 0; p < PART_COUNT; ++p) {
      if (!parts[p].loaded)
        continue;
      for (int mi = 0; mi < (int)parts[p].meshBuffers.size() &&
                        mi < (int)parts[p].bmd->Meshes.size();
           ++mi) {
        RetransformMeshWithBones(parts[p].bmd->Meshes[mi], bones,
                                 parts[p].meshBuffers[mi]);
      }
    }

    int winW_, winH_;
    glfwGetWindowSize(window, &winW_, &winH_);
    float dpiScale = (winW_ > 0) ? (float)fbWidth / (float)winW_ : 1.0f;
    int panelPx = (int)(280.0f * dpiScale);
    int sceneW = fbWidth - panelPx;
    if (sceneW < 1) sceneW = 1;

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)sceneW / (float)fbHeight, 0.1f,
        100000.0f);
    glm::mat4 view = camera.GetViewMatrix();
    // MU Z-up → GL Y-up
    glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                  glm::vec3(1.0f, 0.0f, 0.0f));

    bgfx::setViewRect(0, uint16_t(panelPx), 0, uint16_t(sceneW), uint16_t(fbHeight));
    bgfx::setViewTransform(0, glm::value_ptr(view), glm::value_ptr(projection));

    glm::vec3 eye = camera.GetEyePosition();

    // Draw all body parts
    for (int p = 0; p < PART_COUNT; ++p) {
      for (auto &mb : parts[p].meshBuffers) {
        if (mb.indexCount == 0 || mb.hidden)
          continue;
        SubmitMesh(mb, modelMat, eye);
      }
    }

    // Draw weapon attached to bone
    // Original engine chain (ZzzCharacter.cpp RenderLinkObject + ZzzBMD.cpp):
    //   ParentMatrix = CharBone[LinkBone] * OffsetMatrix(AngleMatrix + translation)
    //   Animation(Parent=true): BoneMatrix[i] = ParentMatrix * WeaponBoneLocal[i]
    //   Transform(): vertex = BoneMatrix[vertex.Node] * vertex.Position
    // Full chain: CharBone * Offset * WeaponBone[node] * rawVertex
    if (weaponBmd && !weaponMeshBuffers.empty() &&
        weaponBone >= 0 && weaponBone < (int)bones.size()) {

      // Build offset matrix from debug sliders using shared AngleMatrix
      BoneWorldMatrix offsetMat =
          MuMath::BuildWeaponOffsetMatrix(weaponRot, weaponOffset);

      // parentMat = CharBone[attachBone] * OffsetMatrix
      BoneWorldMatrix parentMat;
      MuMath::ConcatTransforms(
          (const float(*)[4])bones[weaponBone].data(),
          (const float(*)[4])offsetMat.data(),
          (float(*)[4])parentMat.data());

      // Compute weapon bone matrices with parentMat as root parent
      // This mirrors original Animation(Parent=true): root bone gets
      // BoneMatrix[0] = parentMat * weaponBoneLocal[0]
      auto wLocalBones = ComputeBoneMatrices(weaponBmd.get());
      std::vector<BoneWorldMatrix> wFinalBones(wLocalBones.size());
      for (int bi = 0; bi < (int)wLocalBones.size(); ++bi) {
        MuMath::ConcatTransforms(
            (const float(*)[4])parentMat.data(),
            (const float(*)[4])wLocalBones[bi].data(),
            (float(*)[4])wFinalBones[bi].data());
      }

      // Re-skin and draw each weapon mesh using final bone matrices
      for (int mi = 0; mi < (int)weaponMeshBuffers.size() &&
                        mi < (int)weaponBmd->Meshes.size();
           ++mi) {
        auto &mesh = weaponBmd->Meshes[mi];
        auto &mb = weaponMeshBuffers[mi];
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
            glm::vec3 srcNorm =
                (tri.NormalIndex[v] < mesh.NumNormals)
                    ? mesh.Normals[tri.NormalIndex[v]].Normal
                    : glm::vec3(0, 0, 1);

            // Transform by weapon bone matrix (includes parent attachment)
            int boneIdx = srcVert.Node;
            if (boneIdx >= 0 && boneIdx < (int)wFinalBones.size()) {
              vv.pos = MuMath::TransformPoint(
                  (const float(*)[4])wFinalBones[boneIdx].data(), srcPos);
              vv.normal = MuMath::RotateVector(
                  (const float(*)[4])wFinalBones[boneIdx].data(), srcNorm);
            } else {
              // Fallback: use parentMat directly
              vv.pos = MuMath::TransformPoint(
                  (const float(*)[4])parentMat.data(), srcPos);
              vv.normal = MuMath::RotateVector(
                  (const float(*)[4])parentMat.data(), srcNorm);
            }
            vv.tex =
                (tri.TexCoordIndex[v] < mesh.NumTexCoords)
                    ? glm::vec2(mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordU,
                                mesh.TexCoords[tri.TexCoordIndex[v]].TexCoordV)
                    : glm::vec2(0);
            verts.push_back(vv);
          }
        }

        // Update dynamic VBO with re-skinned weapon vertices
        bgfx::update(mb.dynVbo, 0,
                      bgfx::copy(verts.data(),
                                 (uint32_t)(verts.size() * sizeof(ViewerVertex))));

        SubmitMesh(mb, modelMat, eye);
      }
    }
  }

  void RenderUI() {
    ImGui_ImplBgfx_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(280, (float)winH));
    ImGui::Begin("Character", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    // --- Class selection ---
    ImGui::Text("Class:");
    for (int i = 0; i < CLASS_COUNT; ++i) {
      if (ImGui::RadioButton(kClassNames[i], currentClass == i)) {
        LoadClass(i);
      }
    }

    // --- BodyAngle (Main 5.2: character root bone rotation) ---
    ImGui::Separator();
    ImGui::Text("BodyAngle:");
    ImGui::DragFloat3("##bodyangle", &bodyAngle.x, 1.0f, -360.0f, 360.0f,
                      "%.0f");

    // --- Armor set selection ---
    ImGui::Separator();
    ImGui::Text("Armor Set:");
    if (ImGui::BeginCombo("##armor", kArmorSets[currentArmorSet].name)) {
      for (int i = 0; i < kNumArmorSets; ++i) {
        if (!CanClassWearSet(currentClass, kArmorSets[i]))
          continue;
        bool selected = (currentArmorSet == i);
        if (ImGui::Selectable(kArmorSets[i].name, selected)) {
          LoadArmorSet(i);
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    // --- Weapon selection ---
    ImGui::Separator();
    ImGui::Text("Weapon:");

    // Category combo (filtered by class)
    const char *catLabel = kWeaponCatNames[weaponCategory];
    if (ImGui::BeginCombo("##wcat", catLabel)) {
      for (int c = 0; c < WCAT_COUNT; ++c) {
        if (!CanClassUseWeapon(currentClass, c))
          continue;
        bool selected = (weaponCategory == c);
        if (ImGui::Selectable(kWeaponCatNames[c], selected)) {
          if (c != weaponCategory) {
            LoadWeapon(c, 0); // Load first weapon of new category
          }
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    // Model combo (filtered by category)
    if (weaponCategory != WCAT_NONE) {
      const auto &files = kWeaponFiles[weaponCategory];
      const char *modelLabel =
          (weaponIndex < (int)files.size()) ? files[weaponIndex] : "None";
      if (ImGui::BeginCombo("##wmodel", modelLabel)) {
        for (int i = 0; i < (int)files.size(); ++i) {
          bool selected = (weaponIndex == i);
          if (ImGui::Selectable(files[i], selected)) {
            if (i != weaponIndex) {
              LoadWeapon(weaponCategory, i);
            }
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      // Debug sliders for attachment tuning
      if (ImGui::TreeNode("Attachment")) {
        // --- Preset selector ---
        struct OffsetPreset {
          const char *name;
          int bone;
          glm::vec3 rot;
          glm::vec3 off;
        };
        static const OffsetPreset presets[] = {
            {"Identity", 33, {0, 0, 0}, {0, 0, 0}},
            {"Main5.2 Melee", 33, {70, 0, 90}, {-20, 5, 40}},
            {"Main5.2 Melee (L)", 42, {145, 0, 275}, {0, 10, -30}},
            {"Main5.2 Bow", 42, {0, 20, 180}, {-10, 8, 40}},
            {"Main5.2 Shield", 42, {70, 0, 90}, {-10, 0, 0}},
            {"Main5.2 Shield+6", 42, {30, 0, 90}, {-15, 0, -25}},
            {"rot(90,0,0)", 33, {90, 0, 0}, {0, 0, 0}},
            {"rot(-90,0,0)", 33, {-90, 0, 0}, {0, 0, 0}},
            {"rot(0,90,0)", 33, {0, 90, 0}, {0, 0, 0}},
            {"rot(0,0,90)", 33, {0, 0, 90}, {0, 0, 0}},
            {"rot(180,0,0)", 33, {180, 0, 0}, {0, 0, 0}},
            {"rot(0,180,0)", 33, {0, 180, 0}, {0, 0, 0}},
            {"rot(0,0,180)", 33, {0, 0, 180}, {0, 0, 0}},
        };
        static int selectedPreset = 0;
        if (ImGui::Combo("Preset", &selectedPreset,
                         [](void *data, int idx) -> const char * {
                           return ((const OffsetPreset *)data)[idx].name;
                         },
                         (void *)presets,
                         sizeof(presets) / sizeof(presets[0]))) {
          auto &p = presets[selectedPreset];
          weaponBone = p.bone;
          weaponRot = p.rot;
          weaponOffset = p.off;
        }

        ImGui::Separator();
        ImGui::SliderInt("Bone", &weaponBone, 0,
                         (int)skeleton->Bones.size() - 1);
        ImGui::DragFloat3("Rotation", &weaponRot.x, 1.0f, -360.0f, 360.0f,
                          "%.0f");
        ImGui::DragFloat3("Offset", &weaponOffset.x, 1.0f, -200.0f, 200.0f,
                          "%.0f");
        if (ImGui::Button("Reset to Defaults")) {
          const auto &cfg = kWeaponConfigs[weaponCategory];
          weaponRot = cfg.rot;
          weaponOffset = cfg.offset;
          weaponBone = cfg.bone;
          selectedPreset = 0;
        }
        ImGui::TreePop();
      }
    }

    // --- Body parts info ---
    ImGui::Separator();
    ImGui::Text("Body Parts:");
    int totalMeshes = 0, totalTris = 0;
    for (int p = 0; p < PART_COUNT; ++p) {
      if (parts[p].loaded) {
        int tris = 0;
        for (auto &m : parts[p].bmd->Meshes)
          tris += m.NumTriangles;
        totalMeshes += (int)parts[p].bmd->Meshes.size();
        totalTris += tris;
        ImGui::BulletText("%s: %s (%d tri)", kPartNames[p],
                          parts[p].filename.c_str(), tris);
      } else {
        ImGui::BulletText("%s: (missing)", kPartNames[p]);
      }
    }

    ImGui::Separator();
    ImGui::Text("Bones: %d", (int)skeleton->Bones.size());
    ImGui::Text("Actions: %d", totalActions);
    ImGui::Text("Meshes: %d | Tris: %d", totalMeshes, totalTris);

    // --- Animation categories (tab bar) ---
    ImGui::Separator();
    ImGui::Text("Animation:");

    if (ImGui::BeginTabBar("AnimTabs")) {
      for (auto &cat : animCategories) {
        if (ImGui::BeginTabItem(cat.name)) {
          for (auto &entry : cat.entries) {
            // Skip entries that exceed available actions
            if (entry.actionIndex >= totalActions)
              continue;

            bool selected = (currentAction == entry.actionIndex);
            char label[64];
            snprintf(label, sizeof(label), "%s [%d]", entry.name,
                     entry.actionIndex);
            if (ImGui::Selectable(label, selected)) {
              SetAction(entry.actionIndex);
            }
          }
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }

    // --- Raw action slider (fallback) ---
    ImGui::Separator();
    int actionVal = currentAction;
    if (ImGui::SliderInt("Action##raw", &actionVal, 0,
                         std::max(0, totalActions - 1))) {
      SetAction(actionVal);
    }

    // --- Playback controls ---
    ImGui::Checkbox("Play", &animPlaying);
    ImGui::SameLine();
    ImGui::SliderFloat("Speed", &animSpeed, 0.5f, 20.0f, "%.1f");

    float frameVal = animFrame;
    if (ImGui::SliderFloat("Frame", &frameVal, 0.0f,
                            (float)std::max(1, currentNumKeys - 1), "%.1f")) {
      animFrame = frameVal;
    }
    ImGui::Text("Keys: %d", currentNumKeys);

    // --- GIF recording ---
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
        int skip = 25 / gifFpsSetting;
        Screenshot::StartRecording(window, "screenshots/char_capture.gif",
                                   gifFrameTarget, 100 / gifFpsSetting,
                                   gifScaleSetting, skip - 1);
      }
    }

    ImGui::Separator();
    ImGui::TextWrapped("LMB drag: Rotate\nScroll: Zoom\nESC: Quit");

    ImGui::End();
    ImGui::Render();
    ImGui_ImplBgfx_RenderDrawData(ImGui::GetDrawData());
  }

  // --- GLFW Callbacks ---

  static void ScrollCallback(GLFWwindow *w, double xoff, double yoff) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_ScrollCallback(w, xoff, yoff);
    if (ImGui::GetIO().WantCaptureMouse)
      return;
    self->camera.distance -= (float)yoff * self->camera.distance * 0.15f;
    self->camera.distance = glm::clamp(self->camera.distance, 1.0f, 50000.0f);
  }

  static void MouseButtonCallback(GLFWwindow *w, int button, int action,
                                  int mods) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
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
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_CursorPosCallback(w, x, y);
    if (self->dragging && !ImGui::GetIO().WantCaptureMouse) {
      float dx = (float)(x - self->lastMouseX);
      float dy = (float)(y - self->lastMouseY);
      self->lastMouseX = x;
      self->lastMouseY = y;
      self->camera.yaw += dx * 0.3f;
      self->camera.pitch += dy * 0.3f;
      self->camera.pitch = glm::clamp(self->camera.pitch, -89.0f, 89.0f);
    }
  }

  static void KeyCallback(GLFWwindow *w, int key, int scancode, int action,
                          int mods) {
    auto *self = static_cast<CharacterViewer *>(glfwGetWindowUserPointer(w));
    ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);
    if (ImGui::GetIO().WantCaptureKeyboard)
      return;
    if (action == GLFW_PRESS) {
      if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(w, true);
      if (key == GLFW_KEY_P)
        Screenshot::Capture(w, "screenshots/char_screenshot.jpg");
    }
  }

  static void CharCallback(GLFWwindow *w, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(w, c);
  }

  // --- Auto-screenshot mode ---
  void RunAutoScreenshots() {
    // Load Sword01 for analysis
    auto sw1 = BMDParser::Parse(DATA_ITEM_PATH + std::string("Sword01.bmd"));

    printf("\n=== WEAPON BONE DIAGNOSTICS ===\n");

    // Dump weapon BMD's own bone matrix (this is what the old diagnostic was missing!)
    if (sw1) {
      auto wBones = ComputeBoneMatrices(sw1.get());
      printf("Sword01: %d bones, %d actions\n",
             (int)sw1->Bones.size(), (int)sw1->Actions.size());
      for (int bi = 0; bi < (int)wBones.size() && bi < 3; ++bi) {
        auto &wb = wBones[bi];
        printf("  WeaponBone[%d] matrix:\n", bi);
        printf("    Row0: [%.4f, %.4f, %.4f, %.4f]\n", wb[0][0], wb[0][1], wb[0][2], wb[0][3]);
        printf("    Row1: [%.4f, %.4f, %.4f, %.4f]\n", wb[1][0], wb[1][1], wb[1][2], wb[1][3]);
        printf("    Row2: [%.4f, %.4f, %.4f, %.4f]\n", wb[2][0], wb[2][1], wb[2][2], wb[2][3]);
        // What does this bone do to +Z (blade direction)?
        glm::vec3 zInBone = MuMath::RotateVector(
            (const float(*)[4])wb.data(), glm::vec3(0, 0, 1));
        printf("    +Z maps to: (%.4f, %.4f, %.4f)\n", zInBone.x, zInBone.y, zInBone.z);
      }
      // Dump bone names if available
      for (int bi = 0; bi < (int)sw1->Bones.size() && bi < 3; ++bi) {
        printf("  Bone[%d]: dummy=%d parent=%d name='%s'\n",
               bi, sw1->Bones[bi].Dummy, sw1->Bones[bi].Parent,
               sw1->Bones[bi].Name);
      }

      // Vertex extents
      glm::vec3 vmin(1e9), vmax(-1e9);
      for (auto &mesh : sw1->Meshes) {
        for (int i = 0; i < mesh.NumVertices; ++i) {
          vmin = glm::min(vmin, mesh.Vertices[i].Position);
          vmax = glm::max(vmax, mesh.Vertices[i].Position);
        }
      }
      printf("  Vertex extents: min(%.2f,%.2f,%.2f) max(%.2f,%.2f,%.2f)\n",
             vmin.x, vmin.y, vmin.z, vmax.x, vmax.y, vmax.z);
    }

    // Dump skeleton bone names for right arm area
    printf("\n=== SKELETON BONE NAMES (right arm candidates) ===\n");
    for (int bi = 0; bi < (int)skeleton->Bones.size(); ++bi) {
      auto &bone = skeleton->Bones[bi];
      if (bone.Dummy) continue;
      printf("  Bone[%2d] parent=%2d name='%s'\n", bi, bone.Parent, bone.Name);
    }

    // Compare blade direction at action 5 for different bone + rotation combos
    printf("\n=== BLADE DIRECTION COMPARISON (Action 5 = 2H idle) ===\n");
    auto bones5 = ComputeBoneMatricesInterpolated(skeleton.get(), 5, 0.0f);
    auto wBones = sw1 ? ComputeBoneMatrices(sw1.get()) : std::vector<BoneWorldMatrix>{};

    struct TestConfig {
      int bone;
      glm::vec3 rot;
      glm::vec3 offset;
      const char *label;
    };
    TestConfig tests[] = {
        {33, {90, 0, 0}, {0, 0, 0}, "Bone33 rot(90,0,0) [CURRENT]"},
        {33, {-90, 0, 0}, {0, 0, 0}, "Bone33 rot(-90,0,0) [OLD]"},
        {33, {70, 0, 90}, {-20, 5, 40}, "Bone33 rot(70,0,90) offs(-20,5,40) [ORIG REF]"},
        {28, {70, 0, 90}, {-20, 5, 40}, "Bone28 rot(70,0,90) offs(-20,5,40) [ORIG BONE+ROT]"},
        {33, {0, 0, 0}, {0, 0, 0}, "Bone33 rot(0,0,0) identity"},
        {33, {0, 0, 180}, {0, 0, 0}, "Bone33 rot(0,0,180)"},
        {33, {180, 0, 0}, {0, 0, 0}, "Bone33 rot(180,0,0)"},
        {33, {0, 90, 0}, {0, 0, 0}, "Bone33 rot(0,90,0)"},
        {33, {0, -90, 0}, {0, 0, 0}, "Bone33 rot(0,-90,0)"},
        {33, {0, 0, 90}, {0, 0, 0}, "Bone33 rot(0,0,90)"},
        {33, {0, 0, -90}, {0, 0, 0}, "Bone33 rot(0,0,-90)"},
    };

    for (auto &t : tests) {
      if (t.bone >= (int)bones5.size()) {
        printf("  %-50s  BONE OUT OF RANGE\n", t.label);
        continue;
      }
      auto &bm = bones5[t.bone];
      BoneWorldMatrix oMat = MuMath::BuildWeaponOffsetMatrix(t.rot, t.offset);
      BoneWorldMatrix pMat;
      MuMath::ConcatTransforms((const float(*)[4])bm.data(),
                                (const float(*)[4])oMat.data(),
                                (float(*)[4])pMat.data());
      if (!wBones.empty()) {
        BoneWorldMatrix fullMat;
        MuMath::ConcatTransforms((const float(*)[4])pMat.data(),
                                  (const float(*)[4])wBones[0].data(),
                                  (float(*)[4])fullMat.data());
        glm::vec3 tip = MuMath::TransformPoint(
            (const float(*)[4])fullMat.data(), glm::vec3(0, 0, 64.7f));
        glm::vec3 handle = MuMath::TransformPoint(
            (const float(*)[4])fullMat.data(), glm::vec3(0, 0, 0));
        glm::vec3 dir = tip - handle;
        float glY = dir.z;  // GL Y(up) = MU Z
        float pctUp = glY / glm::length(dir) * 100.0f;
        printf("  %-50s  MU(%.1f,%.1f,%.1f) GL_Y(up)=%.1f (%.0f%%)\n",
               t.label, dir.x, dir.y, dir.z, glY, pctUp);
      }
    }

    // Also show action 4 for the most promising rotations
    printf("\n=== BLADE DIRECTION COMPARISON (Action 4 = 1H idle) ===\n");
    auto bones4 = ComputeBoneMatricesInterpolated(skeleton.get(), 4, 0.0f);
    for (auto &t : tests) {
      if (t.bone >= (int)bones4.size()) continue;
      auto &bm = bones4[t.bone];
      BoneWorldMatrix oMat = MuMath::BuildWeaponOffsetMatrix(t.rot, t.offset);
      BoneWorldMatrix pMat;
      MuMath::ConcatTransforms((const float(*)[4])bm.data(),
                                (const float(*)[4])oMat.data(),
                                (float(*)[4])pMat.data());
      if (!wBones.empty()) {
        BoneWorldMatrix fullMat;
        MuMath::ConcatTransforms((const float(*)[4])pMat.data(),
                                  (const float(*)[4])wBones[0].data(),
                                  (float(*)[4])fullMat.data());
        glm::vec3 tip = MuMath::TransformPoint(
            (const float(*)[4])fullMat.data(), glm::vec3(0, 0, 64.7f));
        glm::vec3 handle = MuMath::TransformPoint(
            (const float(*)[4])fullMat.data(), glm::vec3(0, 0, 0));
        glm::vec3 dir = tip - handle;
        float glY = dir.z;
        float pctUp = glY / glm::length(dir) * 100.0f;
        printf("  %-50s  MU(%.1f,%.1f,%.1f) GL_Y(up)=%.1f (%.0f%%)\n",
               t.label, dir.x, dir.y, dir.z, glY, pctUp);
      }
    }
    printf("=== END DIAGNOSTICS ===\n\n");

    struct AngleShot {
      float yaw, pitch;
      int action;
      int weaponIdx;
      const char *name;
    };
    AngleShot shots[] = {
        // Sword05 (curved 2H) in Two-Hand idle
        {180.0f, 0.0f, 5, 4, "2h_sw05_front"},
        {270.0f, 0.0f, 5, 4, "2h_sw05_right"},
        {  0.0f, 0.0f, 5, 4, "2h_sw05_back"},
        // Sword01 (straight 1H) in Two-Hand idle — clearer blade direction
        {180.0f, 0.0f, 5, 0, "2h_sw01_front"},
        {270.0f, 0.0f, 5, 0, "2h_sw01_right"},
        // Sword01 in One-Hand idle
        {180.0f, 0.0f, 4, 0, "1h_sw01_front"},
        {270.0f, 0.0f, 4, 0, "1h_sw01_right"},
    };
    int lastWeaponIdx = -1;

    // Pause animation at frame 0 for clean capture
    animPlaying = false;
    animFrame = 0.0f;

    // Warm up a few frames to let BGFX state settle
    for (int i = 0; i < 5; ++i) {
      glfwPollEvents();
      deltaTime = 0.016f;
      RenderScene();
      RenderUI();
      bgfx::frame();
    }

    for (auto &shot : shots) {
      // Switch weapon model if needed
      if (shot.weaponIdx != lastWeaponIdx) {
        UnloadWeapon();
        LoadWeapon(WCAT_SWORD, shot.weaponIdx);
        lastWeaponIdx = shot.weaponIdx;
        // Extra frames to let new weapon mesh settle
        for (int i = 0; i < 3; ++i) {
          glfwPollEvents();
          deltaTime = 0.016f;
          RenderScene();
          RenderUI();
          bgfx::frame();
        }
      }

      camera.yaw = shot.yaw;
      camera.pitch = shot.pitch;
      if (shot.action >= 0)
        SetAction(shot.action);

      // Render 3 frames at this angle/action to stabilize
      for (int i = 0; i < 3; ++i) {
        glfwPollEvents();
        deltaTime = 0.016f;
        RenderScene();
        RenderUI();
        bgfx::frame();
      }

      std::string fname =
          std::string("charviewer_") + shot.name + ".jpg";
      Screenshot::Capture(window, fname);
      printf("[AutoScreenshot] Saved %s (action=%d weapon=%d yaw=%.0f pitch=%.0f)\n",
             fname.c_str(), shot.action, shot.weaponIdx, shot.yaw, shot.pitch);
    }

    printf("[AutoScreenshot] Done — %zu screenshots saved to screenshots/\n",
           sizeof(shots) / sizeof(shots[0]));
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
  CharacterViewer viewer;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--screenshots")
      viewer.autoScreenshot = true;
  }
  viewer.Run();
  return 0;
}
