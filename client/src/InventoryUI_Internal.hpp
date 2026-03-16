#pragma once
// Internal header shared between InventoryUI.cpp, InventoryUITooltip.cpp,
// and InventoryUISkills.cpp.  NOT a public API — lives in src/, not include/.

#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "TextureLoader.hpp"
#include "imgui.h"
#include <string>
#include <vector>

// ─── Shared constants (inline constexpr — safe in headers) ──────────────────

inline constexpr float g_uiPanelScale = 1.5f;
inline constexpr float BASE_PANEL_W = 190.0f;
inline constexpr float BASE_PANEL_H = 429.0f;
inline constexpr float PANEL_W = BASE_PANEL_W * g_uiPanelScale;
inline constexpr float PANEL_H = BASE_PANEL_H * g_uiPanelScale;
inline constexpr float PANEL_Y = 20.0f;
inline constexpr float PANEL_X_RIGHT = 1270.0f - PANEL_W;

inline constexpr int SKILL_ICON_COLS = 25;
inline constexpr float SKILL_ICON_W = 20.0f;
inline constexpr float SKILL_ICON_H = 28.0f;
inline constexpr float SKILL_TEX_SIZE = 512.0f;

inline constexpr int NUM_DK_SKILLS = 8;
inline constexpr int NUM_DW_SPELLS = 14;

// ─── Shared structs ─────────────────────────────────────────────────────────

struct SkillDef {
  uint8_t skillId;
  const char *name;
  int resourceCost; // AG for DK, Mana for DW
  int levelReq;
  int damageBonus;
  const char *desc;
  int energyReq; // Energy stat required to learn
};

struct PendingTooltipLine {
  ImU32 color;
  std::string text;
  uint8_t flags = 0; // 1=center, 2=separator, 4=small gap after
};

struct PendingTooltip {
  bool active = false;
  ImVec2 pos;
  float w = 0, h = 0;
  std::vector<PendingTooltipLine> lines;
  ImU32 borderColor = IM_COL32(120, 120, 200, 200);
};

struct DeferredCooldown {
  float x, y, w, h; // screen-space rect
  char text[8];
};

// ─── Shared state (defined in InventoryUI.cpp) ─────────────────────────────

extern const InventoryUIContext *s_ctx;
extern PendingTooltip g_pendingTooltip;
extern TexHandle g_texSkillIcons;
extern bool g_isDragging;
extern int16_t g_dragDefIndex;
extern int g_dragFromSlot;
extern int g_dragFromEquipSlot;
extern int g_dragFromSkillSlot;
extern int g_dragFromPotionSlot;
extern bool g_dragFromRmcSlot;
extern std::vector<DeferredCooldown> g_deferredCooldowns;

extern const SkillDef g_dkSkills[];
extern const SkillDef g_dwSpells[];

// ─── Shared helper functions (defined in InventoryUI.cpp) ───────────────────

const SkillDef *GetClassSkills(uint8_t classCode, int &outCount);

// These live inside namespace InventoryUI in the .cpp files
namespace InventoryUI {
int GetEquipSlotForCategory(uint8_t category, bool leftHand = false);
const DropDef *GetEquippedDropDef(int equipSlot);
} // namespace InventoryUI

void BeginPendingTooltip(float tw, float th);
void AddPendingTooltipLine(ImU32 color, const std::string &text,
                           uint8_t flags = 0);
void AddTooltipSeparator();

void DrawStyledPanel(ImDrawList *dl, float x0, float y0, float x1, float y1,
                     float rounding = 5.0f);
void DrawStyledSlot(ImDrawList *dl, ImVec2 p0, ImVec2 p1, bool hovered = false,
                    float rounding = 3.0f);
void DrawStyledBar(ImDrawList *dl, float x, float y, float w, float h,
                   float frac, ImU32 topColor, ImU32 botColor,
                   const char *label);
void DrawShadowText(ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text,
                    int shadowOffset = 1);
void DrawOrb(ImDrawList *dl, float cx, float cy, float radius, float frac,
             ImU32 fillTop, ImU32 fillBot, ImU32 emptyColor, ImU32 frameColor,
             const char *label);

// ─── Diablo-style HUD layout constants (shared across Render / Click / Drop) ─
namespace HudLayout {
// Orb dimensions (virtual coords in 1280x720 space, rendered at 0.7 scale)
inline constexpr float ORB_RADIUS = 56.0f;
inline constexpr float ORB_FRAME = 5.0f; // ornate ring thickness

// Slot dimensions
inline constexpr float SLOT = 44.0f;
inline constexpr float GAP = 5.0f;

// Menu button dimensions (used for both virtual-coord and screen-pixel sizing)
inline constexpr float BTN = 36.0f;
inline constexpr float BTN_GAP = 3.0f;

// Vertical positions (virtual Y at 0.7 scale — bottom of screen)
// Raised to give more gap between HUD and screen bottom
inline constexpr float ROW_VY = 750.0f; // Slot row top

// XP bar rendering constants (screen-pixel based, NOT virtual coords)
// These are used directly with window dimensions in RenderQuickbar
inline constexpr float XP_SCREEN_H = 10.0f;     // bar height in px
inline constexpr float XP_SCREEN_PAD = 20.0f;   // padding from screen edges
inline constexpr float XP_SCREEN_BOTTOM = 4.0f; // gap from screen bottom
inline constexpr int XP_SEGMENTS = 10;          // fragmented bar segments
inline constexpr float XP_SEG_GAP = 2.0f;       // gap between segments

// CISTM button screen-pixel constants
inline constexpr int MBTN_COUNT = 5;
inline constexpr float MBTN_SCREEN_BTN = 28.0f; // button size in screen px
inline constexpr float MBTN_SCREEN_GAP = 3.0f;  // gap between buttons
inline constexpr float MBTN_SCREEN_TOTAL_W =
    MBTN_SCREEN_BTN * MBTN_COUNT + MBTN_SCREEN_GAP * (MBTN_COUNT - 1);
inline constexpr float MBTN_SCREEN_RIGHT_PAD = 20.0f;  // from right screen edge
inline constexpr float MBTN_SCREEN_BOTTOM_PAD = 20.0f; // from XP bar top

// Horizontal layout — slots centered, orbs flanking
// Total slot area: 4 pots + gap + 4 skills + gap + RMC = 9 slots + 2 extra gaps
inline constexpr float SLOTS_W =
    (SLOT + GAP) * 4 + GAP + (SLOT + GAP) * 4 + GAP + SLOT;
inline constexpr float PANEL_PAD = 14.0f; // padding inside the center panel
inline constexpr float PANEL_W = SLOTS_W + PANEL_PAD * 2;

// Center everything at screen center
inline constexpr float CENTER_X = 640.0f; // 1280/2
inline constexpr float PANEL_LEFT = CENTER_X - PANEL_W * 0.5f;
inline constexpr float PANEL_RIGHT = CENTER_X + PANEL_W * 0.5f;

// Orbs sit just outside the panel
inline constexpr float ORB_GAP = 8.0f;
inline constexpr float HP_ORB_CX = PANEL_LEFT - ORB_GAP - ORB_RADIUS;
inline constexpr float MP_ORB_CX = PANEL_RIGHT + ORB_GAP + ORB_RADIUS;
inline constexpr float ORB_CY =
    ROW_VY + SLOT * 0.5f; // vertically centered on slots

// Slot positions within the panel (relative to panel left)
inline constexpr float SLOT_START_VX = PANEL_LEFT + PANEL_PAD;
// Potion slots at SLOT_START_VX + i*(SLOT+GAP)
// Skill slots at SLOT_START_VX + 4*(SLOT+GAP) + GAP + i*(SLOT+GAP)
// RMC slot at SLOT_START_VX + 4*(SLOT+GAP) + GAP + 4*(SLOT+GAP) + GAP

// Full HUD extent (for background/hit testing)
inline constexpr float HUD_LEFT = HP_ORB_CX - ORB_RADIUS - ORB_FRAME;
inline constexpr float HUD_RIGHT = MP_ORB_CX + ORB_RADIUS + ORB_FRAME;

// Helper: get potion slot X for index 0-3
inline constexpr float PotionSlotX(int i) {
  return SLOT_START_VX + i * (SLOT + GAP);
}
// Helper: get skill slot X for index 0-3
inline constexpr float SkillSlotX(int i) {
  return SLOT_START_VX + 4 * (SLOT + GAP) + GAP + i * (SLOT + GAP);
}
// Helper: get RMC slot X
inline constexpr float RmcSlotX() {
  return SLOT_START_VX + 4 * (SLOT + GAP) + GAP + 4 * (SLOT + GAP) + GAP;
}
} // namespace HudLayout
