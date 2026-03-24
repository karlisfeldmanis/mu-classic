#include "ClientPacketHandler.hpp"
#include "HeroCharacter.hpp"
#include "InputHandler.hpp"
#include "InventoryUI_Internal.hpp"
#include "ItemDatabase.hpp"
#include "ServerConnection.hpp"
#include "SoundManager.hpp"
#include "TextureLoader.hpp"
#include "UITexture.hpp"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// ─── Shared state (external linkage — declared in InventoryUI_Internal.hpp) ──

static InventoryUIContext s_ctxStore;
const InventoryUIContext *s_ctx = &s_ctxStore;

// Potion cooldown constant
static constexpr float POTION_COOLDOWN_TIME = 30.0f;

// Equipment layout rects (virtual coords)
static const EquipSlotRect g_equipLayoutRects[] = {
    {8, 15, 44, 46, 46},    // Pet
    {2, 75, 44, 46, 46},    // Helm
    {7, 120, 44, 61, 46},   // Wings
    {0, 15, 87, 46, 66},    // R.Hand
    {3, 75, 87, 46, 66},    // Armor
    {1, 135, 87, 46, 66},   // L.Hand
    {9, 54, 87, 28, 28},    // Pendant
    {10, 54, 150, 28, 28},  // Ring 1
    {11, 114, 150, 28, 28}, // Ring 2
    {5, 15, 150, 46, 46},   // Gloves
    {4, 75, 150, 46, 46},   // Pants
    {6, 135, 150, 46, 46}   // Boots
};

// Slot background textures
static TexHandle g_slotBackgrounds[12] = {
    kInvalidTex, kInvalidTex, kInvalidTex, kInvalidTex,
    kInvalidTex, kInvalidTex, kInvalidTex, kInvalidTex,
    kInvalidTex, kInvalidTex, kInvalidTex, kInvalidTex};
static UITexture g_texInventoryBg;
TexHandle g_texSkillIcons = kInvalidTex; // Skill.OZJ sprite sheet (shared)
TexHandle g_texAuraIcons[4] = {kInvalidTex, kInvalidTex, kInvalidTex, kInvalidTex};

// Render queue for deferred 3D item rendering
static std::vector<ItemRenderJob> g_renderQueue;

// Deferred text overlays (rendered AFTER 3D items in second ImGui pass)
struct DeferredOverlay {
  float x, y; // screen coords
  ImU32 color;
  char text[8];
};
static std::vector<DeferredOverlay> g_deferredOverlays;

// DeferredCooldown struct defined in InventoryUI_Internal.hpp
std::vector<DeferredCooldown> g_deferredCooldowns;

// Drag state
int g_dragFromSlot = -1;
int g_dragFromEquipSlot = -1;
int16_t g_dragDefIndex = -2;
static uint8_t g_dragQuantity = 0;
static uint8_t g_dragItemLevel = 0;
bool g_isDragging = false;
int g_dragFromPotionSlot = -1;
int g_dragFromSkillSlot = -1;
bool g_dragFromRmcSlot = false;
static int g_dragFromShopSlot =
    -1; // Primary slot in s_shopGrid when dragging from shop

// SkillDef struct defined in InventoryUI_Internal.hpp

// DK skills (AG cost)
const SkillDef g_dkSkills[] = {
    {19, "Falling Slash", 9, 15, "Downward slash attack"},
    {20, "Lunge", 9, 15, "Forward thrust attack"},
    {21, "Uppercut", 8, 15, "Upward strike attack"},
    {22, "Cyclone", 9, 18, "Spinning attack"},
    {23, "Slash", 10, 20, "Horizontal slash"},
    {41, "Twisting Slash", 10, 25, "AoE spinning slash"},
    {42, "Rageful Blow", 20, 60, "Powerful ground strike"},
    {43, "Death Stab", 12, 70, "Piercing stab attack"},
};
// NUM_DK_SKILLS defined in InventoryUI_Internal.hpp

// DW spells (Mana cost) — OpenMU Version075
const SkillDef g_dwSpells[] = {
    {17, "Energy Ball", 1, 8, "Basic energy projectile"},
    {4, "Fire Ball", 3, 22, "Fireball projectile"},
    {1, "Poison", 42, 20, "Poison magic"},
    {3, "Lightning", 15, 30, "Lightning bolt"},
    {2, "Meteorite", 12, 40, "Falling meteorite"},
    {7, "Ice", 38, 35, "Ice magic"},
    {5, "Flame", 50, 50, "Fire AoE"},
    {8, "Twister", 60, 55, "Twisting wind AoE"},
    {6, "Teleport", 30, 0, "Teleport to location"},
    {9, "Evil Spirit", 90, 80, "Dark spirit AoE"},
    {12, "Aqua Beam", 140, 90, "Water beam attack"},
    {10, "Hellfire", 160, 100, "Massive fire AoE"},
    {13, "Cometfall", 90, 120, "Sky-strike AoE"},
    {14, "Inferno", 200, 150, "Ring of explosions"},
};
// NUM_DW_SPELLS defined in InventoryUI_Internal.hpp

// Elf skills (Mana cost) — OpenMU Version075 (0.97d scope)
const SkillDef g_elfSkills[] = {
    {26, "Heal", 20, 0, "Heal target player"},
    {27, "Greater Defense", 30, 0, "Buff target defense"},
    {28, "Greater Damage", 40, 0, "Buff target damage"},
    {30, "Summon Goblin", 40, 0, "Summon a Goblin ally"},
    {31, "Summon Stone Golem", 70, 0, "Summon a Stone Golem"},
    {32, "Summon Assassin", 110, 0, "Summon an Assassin"},
    {33, "Summon Elite Yeti", 160, 0, "Summon an Elite Yeti"},
    {34, "Summon Dark Knight", 200, 0, "Summon a Dark Knight"},
    {35, "Summon Bali", 250, 0, "Summon the mighty Bali"},
    {29, "Triple Shot", 0, 0, "Fires three arrows at once"},
};
// NUM_ELF_SKILLS defined in InventoryUI_Internal.hpp

// Helper to get the skill list for current class
const SkillDef *GetClassSkills(uint8_t classCode, int &outCount) {
  if (classCode == 0) { // DW
    outCount = NUM_DW_SPELLS;
    return g_dwSpells;
  }
  if (classCode == 32) { // ELF
    outCount = NUM_ELF_SKILLS;
    return g_elfSkills;
  }
  outCount = NUM_DK_SKILLS;
  return g_dkSkills;
}

// Skill icon constants and PendingTooltip defined in InventoryUI_Internal.hpp
PendingTooltip g_pendingTooltip;

// Shop grid (mirrors inventory bag grid for NPC shop display)
static constexpr int SHOP_GRID_COLS = 8;
static constexpr int SHOP_GRID_MAX_ROWS = 15;
static constexpr int SHOP_GRID_MAX_SLOTS = SHOP_GRID_COLS * SHOP_GRID_MAX_ROWS;

struct ShopGridSlot {
  int16_t defIndex = -2;
  uint8_t itemLevel = 0;
  uint32_t buyPrice = 0;
  uint8_t stackQty = 0; // Display stack size (20 for potions/ammo, 0 for non-stackable)
  bool occupied = false;
  bool primary = false;
};

static ShopGridSlot s_shopGrid[SHOP_GRID_MAX_SLOTS];
static int s_shopGridRows = 0;
static bool s_shopGridDirty = true;
static size_t s_lastShopItemCount = 0; // Detect new SHOP_LIST arrivals
static int16_t s_lastShopFirstDef = -1; // Detect shop content change (same count)

// Screen notification (center of screen, fades out)
static std::string s_notifyText;
static float s_notifyTimer = 0.0f;
static constexpr float NOTIFY_DURATION = 1.0f;

// Region name display (Main 5.2: CUIMapName 4-state machine)
enum class RegionNameState { HIDE, FADEIN, SHOW, FADEOUT };
static RegionNameState s_regionState = RegionNameState::HIDE;
static std::string s_regionName;
static float s_regionAlpha = 0.0f;
static float s_regionShowTimer = 0.0f;
// Main 5.2: UIMN_ALPHA_VARIATION = 0.015 per frame at 25fps
static constexpr float REGION_FADEIN_SPEED = 0.375f; // ~2.7s fade in
static constexpr float REGION_FADEOUT_SPEED = 2.0f;  // ~0.5s fade out
static constexpr float REGION_SHOW_TIME = 2.0f;      // 2 seconds hold
// Main 5.2: UIMN_IMG_WIDTH=166, UIMN_IMG_HEIGHT=90 (OZT pre-rendered map name
// image)
static TexHandle s_mapNameTexture = kInvalidTex;
static int s_mapNameTexW = 0, s_mapNameTexH = 0;

// ─── Shared helpers (external linkage — declared in InventoryUI_Internal.hpp)
// ─

void BeginPendingTooltip(float tw, float th) {
  ImVec2 mp = ImGui::GetIO().MousePos;
  ImVec2 tPos(mp.x + 15, mp.y + 15);
  float winW = ImGui::GetIO().DisplaySize.x;
  float winH = ImGui::GetIO().DisplaySize.y;
  if (tPos.x + tw > winW)
    tPos.x = winW - tw - 5;
  if (tPos.y + th > winH)
    tPos.y = winH - th - 5;
  g_pendingTooltip.active = true;
  g_pendingTooltip.pos = tPos;
  g_pendingTooltip.w = tw;
  g_pendingTooltip.h = th;
  g_pendingTooltip.lines.clear();
}

void AddPendingTooltipLine(ImU32 color, const std::string &text,
                           uint8_t flags) {
  g_pendingTooltip.lines.push_back({color, text, flags});
}

void AddTooltipSeparator() { g_pendingTooltip.lines.push_back({0, "---", 2}); }

void DrawStyledPanel(ImDrawList *dl, float x0, float y0, float x1, float y1,
                     float rounding) {
  dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1),
                              IM_COL32(8, 8, 18, 245),   // top-left
                              IM_COL32(8, 8, 18, 245),   // top-right
                              IM_COL32(18, 18, 32, 240), // bottom-right
                              IM_COL32(18, 18, 32, 240)  // bottom-left
  );
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(5, 5, 10, 255), rounding,
              0, 2.0f);
  dl->AddRect(ImVec2(x0 + 2, y0 + 2), ImVec2(x1 - 2, y1 - 2),
              IM_COL32(80, 70, 45, 140),
              rounding > 2 ? rounding - 1 : rounding);
}

void DrawStyledSlot(ImDrawList *dl, ImVec2 p0, ImVec2 p1, bool hovered,
                    float rounding) {
  dl->AddRectFilled(p0, p1, IM_COL32(12, 12, 22, 220), rounding);
  dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p0.y + 1),
              IM_COL32(0, 0, 0, 100));
  dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p0.x + 1, p1.y - 1),
              IM_COL32(0, 0, 0, 100));
  ImU32 borderCol =
      hovered ? IM_COL32(180, 160, 90, 200) : IM_COL32(65, 60, 45, 200);
  dl->AddRect(p0, p1, borderCol, rounding);
}

void DrawStyledBar(ImDrawList *dl, float x, float y, float w, float h,
                   float frac, ImU32 topColor, ImU32 botColor,
                   const char *label) {
  frac = std::clamp(frac, 0.0f, 1.0f);
  ImVec2 p0(x, y), p1(x + w, y + h);
  dl->AddRectFilled(p0, p1, IM_COL32(8, 8, 15, 220), 3.0f);
  if (frac > 0.01f) {
    dl->AddRectFilledMultiColor(p0, ImVec2(x + w * frac, y + h), topColor,
                                topColor, botColor, botColor);
    dl->AddLine(ImVec2(x + 1, y + 1), ImVec2(x + w * frac - 1, y + 1),
                IM_COL32(255, 255, 255, 40));
  }
  dl->AddRect(p0, p1, IM_COL32(65, 60, 45, 180), 3.0f);
  ImVec2 tsz = ImGui::CalcTextSize(label);
  float tx = x + (w - tsz.x) * 0.5f;
  float ty = y + (h - tsz.y) * 0.5f;
  dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 230), label);
  dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 230), label);
}

void DrawShadowText(ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text,
                    int shadowOffset) {
  dl->AddText(ImVec2(pos.x + shadowOffset, pos.y + shadowOffset),
              IM_COL32(0, 0, 0, 230), text);
  dl->AddText(pos, color, text);
}

void DrawOrb(ImDrawList *dl, float cx, float cy, float radius, float frac,
             ImU32 fillTop, ImU32 fillBot, ImU32 emptyColor, ImU32 frameColor,
             const char *label) {
  frac = std::clamp(frac, 0.0f, 1.0f);

  // 1. Empty orb background (dark interior)
  dl->AddCircleFilled(ImVec2(cx, cy), radius - 2, emptyColor, 64);

  // 2. Filled portion — clip from bottom up
  if (frac > 0.01f) {
    float fillH = radius * 2.0f * frac;
    float clipTop = cy + radius - fillH;
    dl->PushClipRect(ImVec2(cx - radius, clipTop),
                     ImVec2(cx + radius, cy + radius + 1), true);
    // Gradient fill from top to bottom of the fill portion
    // Use two half-circles for the gradient effect
    float midY = clipTop + fillH * 0.5f;
    // Top half of fill
    dl->AddCircleFilled(ImVec2(cx, cy), radius - 3, fillTop, 64);
    // Bottom gradient overlay
    dl->PushClipRect(ImVec2(cx - radius, midY),
                     ImVec2(cx + radius, cy + radius + 1), true);
    dl->AddCircleFilled(ImVec2(cx, cy), radius - 3, fillBot, 64);
    dl->PopClipRect();
    dl->PopClipRect();

    // Specular highlight — small bright arc near top of fill
    float hlY = clipTop + 4.0f;
    dl->PushClipRect(ImVec2(cx - radius * 0.5f, hlY),
                     ImVec2(cx + radius * 0.5f, hlY + 8.0f), true);
    dl->AddCircleFilled(ImVec2(cx, cy), radius - 6, IM_COL32(255, 255, 255, 35),
                        64);
    dl->PopClipRect();
  }

  // 3. Ornate frame ring (outer)
  dl->AddCircle(ImVec2(cx, cy), radius, IM_COL32(15, 12, 8, 255), 64, 4.0f);
  dl->AddCircle(ImVec2(cx, cy), radius - 1, frameColor, 64, 2.0f);
  dl->AddCircle(ImVec2(cx, cy), radius + 1, IM_COL32(30, 25, 15, 200), 64,
                1.0f);

  // 4. Inner shadow ring
  dl->AddCircle(ImVec2(cx, cy), radius - 4, IM_COL32(0, 0, 0, 60), 64, 1.5f);

  // 5. Text overlay centered in orb
  if (label && label[0]) {
    ImVec2 tsz = ImGui::CalcTextSize(label);
    float tx = cx - tsz.x * 0.5f;
    float ty = cy - tsz.y * 0.5f + 6.0f; // slightly below center
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 220), label);
    dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 230), label);
  }
}

// ─── Internal helpers (unnamed namespace) ───────────────────────────────────

namespace {

// Close button with hover highlight — used by all panels
static void DrawCloseButton(ImDrawList *dl, const UICoords &c, float px,
                            float py) {
  float bx = BASE_PANEL_W - 24;
  float by = 6;
  float bw = 16, bh = 14;
  ImVec2 bMin(c.ToScreenX(px + bx * g_uiPanelScale),
              c.ToScreenY(py + by * g_uiPanelScale));
  ImVec2 bMax(c.ToScreenX(px + (bx + bw) * g_uiPanelScale),
              c.ToScreenY(py + (by + bh) * g_uiPanelScale));
  ImVec2 mp = ImGui::GetIO().MousePos;
  bool hovered =
      mp.x >= bMin.x && mp.x < bMax.x && mp.y >= bMin.y && mp.y < bMax.y;
  ImU32 bgCol =
      hovered ? IM_COL32(200, 40, 40, 255) : IM_COL32(100, 20, 20, 200);
  dl->AddRectFilled(bMin, bMax, bgCol, 2.0f);
  if (hovered)
    dl->AddRect(bMin, bMax, IM_COL32(255, 100, 100, 255), 2.0f);
  ImVec2 xSize = ImGui::CalcTextSize("X");
  ImVec2 xPos(bMin.x + (bMax.x - bMin.x) * 0.5f - xSize.x * 0.5f,
              bMin.y + (bMax.y - bMin.y) * 0.5f - xSize.y * 0.5f);
  dl->AddText(xPos, IM_COL32(255, 255, 255, 255), "X");
}

// Helper: draw textured quad at virtual coords (handles scaling + OZT V-flip)
static void DrawPanelImage(ImDrawList *dl, const UICoords &c,
                           const UITexture &tex, float px, float py, float relX,
                           float relY, float vw, float vh) {
  if (!TexValid(tex.id))
    return;
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = vw * g_uiPanelScale;
  float sh = vh * g_uiPanelScale;

  ImVec2 pMin(c.ToScreenX(vx), c.ToScreenY(vy));
  ImVec2 pMax(c.ToScreenX(vx + sw), c.ToScreenY(vy + sh));
  ImVec2 uvMin(0, 0), uvMax(1, 1);
  if (tex.isOZT) {
    uvMin.y = 1.0f;
    uvMax.y = 0.0f;
  } // V-flip for OZT
  dl->AddImage((ImTextureID)TexImID(tex.id), pMin, pMax, uvMin, uvMax);
}

// Helper: draw text with shadow (handles scaling)
static void DrawPanelText(ImDrawList *dl, const UICoords &c, float px, float py,
                          float relX, float relY, const char *text, ImU32 color,
                          ImFont *font = nullptr) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sx = c.ToScreenX(vx), sy = c.ToScreenY(vy);

  if (font) {
    float fs = font->LegacySize * ImGui::GetIO().FontGlobalScale;
    dl->AddText(font, fs, ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(font, fs, ImVec2(sx, sy), color, text);
  } else {
    dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(ImVec2(sx, sy), color, text);
  }
}

// Helper: draw right-aligned text (handles scaling)
static void DrawPanelTextRight(ImDrawList *dl, const UICoords &c, float px,
                               float py, float relX, float relY, float width,
                               const char *text, ImU32 color) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = width * g_uiPanelScale;
  ImVec2 sz = ImGui::CalcTextSize(text);
  float sx = c.ToScreenX(vx + sw) - sz.x;
  float sy = c.ToScreenY(vy);
  dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 180), text);
  dl->AddText(ImVec2(sx, sy), color, text);
}

// Helper: draw centered text horizontally (handles scaling)
static void DrawPanelTextCentered(ImDrawList *dl, const UICoords &c, float px,
                                  float py, float relX, float relY, float width,
                                  const char *text, ImU32 color,
                                  ImFont *font = nullptr) {
  float vx = px + relX * g_uiPanelScale;
  float vy = py + relY * g_uiPanelScale;
  float sw = width * g_uiPanelScale;

  ImVec2 sz;
  float fs = 0;
  if (font) {
    fs = font->LegacySize * ImGui::GetIO().FontGlobalScale;
    sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
  } else {
    sz = ImGui::CalcTextSize(text);
  }

  float sx = c.ToScreenX(vx + sw * 0.5f) - sz.x * 0.5f;
  float sy = c.ToScreenY(vy);

  // Strong 2px shadow for panel titles
  if (font) {
    dl->AddText(font, fs, ImVec2(sx + 2, sy + 2), IM_COL32(0, 0, 0, 230), text);
    dl->AddText(font, fs, ImVec2(sx, sy), color, text);
  } else {
    dl->AddText(ImVec2(sx + 2, sy + 2), IM_COL32(0, 0, 0, 230), text);
    dl->AddText(ImVec2(sx, sy), color, text);
  }
}

static void RebuildShopGrid() {
  for (int i = 0; i < SHOP_GRID_MAX_SLOTS; i++)
    s_shopGrid[i] = {};

  auto &defs = ItemDatabase::GetItemDefs();
  int usedRows = 0;

  for (size_t i = 0; i < s_ctx->shopItems->size(); i++) {
    auto &si = (*s_ctx->shopItems)[i];
    auto it = defs.find(si.defIndex);
    if (it == defs.end())
      continue;

    int iw = it->second.width;
    int ih = it->second.height;
    bool placed = false;

    for (int row = 0; row <= SHOP_GRID_MAX_ROWS - ih && !placed; row++) {
      for (int col = 0; col <= SHOP_GRID_COLS - iw && !placed; col++) {
        bool fits = true;
        for (int dy = 0; dy < ih && fits; dy++)
          for (int dx = 0; dx < iw && fits; dx++)
            if (s_shopGrid[(row + dy) * SHOP_GRID_COLS + (col + dx)].occupied)
              fits = false;
        if (!fits)
          continue;

        // Determine stack quantity for display
        uint8_t cat = si.defIndex / 32, idx = si.defIndex % 32;
        bool isAmmo = (cat == 4 && (idx == 7 || idx == 15));
        bool isPotion = (cat == 14 && iw == 1 && ih == 1);
        uint8_t stkQty = isAmmo ? 255 : (isPotion ? 20 : 0);

        for (int dy = 0; dy < ih; dy++) {
          for (int dx = 0; dx < iw; dx++) {
            int slot = (row + dy) * SHOP_GRID_COLS + (col + dx);
            s_shopGrid[slot].occupied = true;
            s_shopGrid[slot].primary = (dy == 0 && dx == 0);
            s_shopGrid[slot].defIndex = si.defIndex;
            s_shopGrid[slot].itemLevel = si.itemLevel;
            s_shopGrid[slot].buyPrice = si.buyPrice;
            s_shopGrid[slot].stackQty = stkQty;
          }
        }
        if (row + ih > usedRows)
          usedRows = row + ih;
        placed = true;
      }
    }
  }

  s_shopGridRows = usedRows;
  s_shopGridDirty = false;
}

static bool CanEquipItem(int16_t defIdx) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return false;
  const auto &def = it->second;

  if (*s_ctx->serverLevel < def.levelReq) {
    std::cout << "[UI] Level requirement not met (" << *s_ctx->serverLevel
              << "/" << def.levelReq << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverStr < def.reqStr) {
    std::cout << "[UI] Strength requirement not met (" << *s_ctx->serverStr
              << "/" << def.reqStr << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverDex < def.reqDex) {
    std::cout << "[UI] Dexterity requirement not met (" << *s_ctx->serverDex
              << "/" << def.reqDex << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverVit < def.reqVit) {
    std::cout << "[UI] Vitality requirement not met (" << *s_ctx->serverVit
              << "/" << def.reqVit << ")" << std::endl;
    return false;
  }
  if (*s_ctx->serverEne < def.reqEne) {
    std::cout << "[UI] Energy requirement not met (" << *s_ctx->serverEne << "/"
              << def.reqEne << ")" << std::endl;
    return false;
  }

  // Class check: bit_mask = 1 << (char_class >> 4)
  // Mapping: 0(DW) -> bit 0, 16(DK) -> bit 1, 32(Elf) -> bit 2
  int bitIndex = s_ctx->hero->GetClass() >> 4;
  if (!(def.classFlags & (1 << bitIndex))) {
    std::cout << "[UI] This item cannot be equipped by your class! (Class:"
              << (int)s_ctx->hero->GetClass() << " Bit:" << bitIndex
              << " Flags:0x" << std::hex << def.classFlags << std::dec << ")"
              << std::endl;
    return false;
  }

  return true;
}

static bool CheckBagFit(int16_t defIdx, int targetSlot, int ignoreSlot = -1) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return false;
  int w = it->second.width;
  int h = it->second.height;
  int targetRow = targetSlot / 8;
  int targetCol = targetSlot % 8;

  if (targetCol + w > 8 || targetRow + h > 8)
    return false;

  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (targetRow + hh) * 8 + (targetCol + ww);
      if (s == ignoreSlot)
        continue;
      if (s_ctx->inventory[s].occupied) {
        return false;
      }
    }
  }
  return true;
}

} // anonymous namespace


// ─── InventoryUI namespace implementation ───────────────────────────────────

namespace InventoryUI {

void Init(const InventoryUIContext &ctx) {
  s_ctxStore = ctx;
  s_ctx = &s_ctxStore;

  // Load map name OZT images (Main 5.2: Local/[Language]/ImgsMapName/)
  if (!TexValid(s_mapNameTexture)) {
    s_mapNameTexture =
        TextureLoader::LoadOZT("Data/Local/Eng/ImgsMapName/lorencia.OZT");
    if (TexValid(s_mapNameTexture)) {
      std::cout << "[UI] Loaded map name image: " << s_mapNameTexW << "x"
                << s_mapNameTexH << std::endl;
    }
  }
}

void ClearBagItem(int slot) {
  if (slot < 0 || slot >= INVENTORY_SLOTS)
    return;
  if (!s_ctx->inventory[slot].occupied)
    return;

  int primarySlot = slot;
  if (!s_ctx->inventory[slot].primary) {
    // Search backward or use stored defIndex to find root
  }

  int16_t defIdx = s_ctx->inventory[primarySlot].defIndex;
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIdx);
  if (it != g_itemDefs.end()) {
    int w = it->second.width;
    int h = it->second.height;
    int r = primarySlot / 8;
    int c = primarySlot % 8;
    for (int hh = 0; hh < h; hh++) {
      for (int ww = 0; ww < w; ww++) {
        int s = (r + hh) * 8 + (c + ww);
        if (s < INVENTORY_SLOTS) {
          s_ctx->inventory[s] = {};
        }
      }
    }
  } else {
    s_ctx->inventory[primarySlot] = {};
  }
}

void ConsumeQuickSlotItem(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= 4)
    return;
  int16_t defIdx = s_ctx->potionBar[slotIndex];
  if (defIdx <= 0) {
    std::cout << "[QuickSlot] Slot " << slotIndex << " empty (defIdx=" << defIdx
              << ")" << std::endl;
    return;
  }

  // Cooldown check
  if (*s_ctx->potionCooldown > 0.0f) {
    std::cout << "[QuickSlot] Cooldown active (" << *s_ctx->potionCooldown
              << "s remaining)" << std::endl;
    return;
  }

  // Search for the first instance of this item in inventory
  int foundSlot = -1;
  for (int i = 0; i < INVENTORY_SLOTS; i++) {
    if (s_ctx->inventory[i].occupied && s_ctx->inventory[i].primary &&
        s_ctx->inventory[i].defIndex == defIdx) {
      foundSlot = i;
      break;
    }
  }

  if (foundSlot != -1) {
    auto &g_itemDefs = ItemDatabase::GetItemDefs();
    auto it = g_itemDefs.find(defIdx);
    if (it == g_itemDefs.end())
      return;
    const auto &def = it->second;

    // Mount toggle from quickslot (not consumed, no cooldown)
    if (def.category == 13 && (def.itemIndex == 2 || def.itemIndex == 3)) {
      if (s_ctx->mountToggling && !*s_ctx->mountToggling) {
        s_ctx->hero->StopMoving();
        if (!s_ctx->hero->IsMounted()) {
          // Prepare mount index so timer completion will call EquipMount
          s_ctx->hero->SetMountIndex(def.itemIndex);
        }
        *s_ctx->mountToggling = true;
        *s_ctx->mountToggleTimer = s_ctx->mountToggleTime;
      }
      return;
    }

    if (def.category == 14) {
      // HP potions (itemIndex 0-3)
      if (def.itemIndex >= 0 && def.itemIndex <= 3) {
        if (*s_ctx->serverHP >= *s_ctx->serverMaxHP) {
          ShowNotification("HP is full!");
          return;
        }
      }
      // Mana potions (itemIndex 4-6)
      else if (def.itemIndex >= 4 && def.itemIndex <= 6) {
        bool isDK = s_ctx->hero && s_ctx->hero->GetClass() == 16;
        if (isDK) {
          int curAG = s_ctx->serverAG ? *s_ctx->serverAG : 0;
          // AG max not directly available — let server validate
          ShowNotification("Using mana potion...");
        } else {
          if (*s_ctx->serverMP >= *s_ctx->serverMaxMP) {
            ShowNotification("Mana is full!");
            return;
          }
        }
      } else {
        return; // Unknown potion
      }

      s_ctx->server->SendItemUse((uint8_t)foundSlot);
      SoundManager::Play(SOUND_DRINK01);
      *s_ctx->potionCooldown = POTION_COOLDOWN_TIME;
      std::cout << "[QuickSlot] Requested to use "
                << ItemDatabase::GetItemNameByDef(defIdx) << " from slot "
                << foundSlot << std::endl;
    }
  } else {
    std::cout << "[QuickSlot] No " << ItemDatabase::GetItemNameByDef(defIdx)
              << " found in inventory!" << std::endl;
  }
}

void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  auto it = g_itemDefs.find(defIdx);
  if (it == g_itemDefs.end())
    return;
  int w = it->second.width;
  int h = it->second.height;
  int r = slot / 8;
  int c = slot % 8;

  // Defensive: check if entire footprint is within bounds and free
  if (c + w > 8 || r + h > 8)
    return;

  // Pass 1: check occupancy
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      if (s >= INVENTORY_SLOTS || s_ctx->inventory[s].occupied)
        return;
    }
  }

  // Pass 2: mark slots
  for (int hh = 0; hh < h; hh++) {
    for (int ww = 0; ww < w; ww++) {
      int s = (r + hh) * 8 + (c + ww);
      s_ctx->inventory[s].occupied = true;
      s_ctx->inventory[s].primary = (hh == 0 && ww == 0);
      s_ctx->inventory[s].defIndex = defIdx;
      if (s_ctx->inventory[s].primary) {
        s_ctx->inventory[s].quantity = qty;
        s_ctx->inventory[s].itemLevel = lvl;
      }
    }
  }
}

void RecalcEquipmentStats() {
  int totalDmgMin = 0, totalDmgMax = 0, totalDef = 0;
  for (int s = 0; s < 12; ++s) {
    if (!s_ctx->equipSlots[s].equipped)
      continue;
    int16_t defIdx = ItemDatabase::GetDefIndexFromCategory(
        s_ctx->equipSlots[s].category, s_ctx->equipSlots[s].itemIndex);
    auto *info = ItemDatabase::GetDropInfo(defIdx);
    if (info) {
      totalDmgMin += info->dmgMin;
      totalDmgMax += info->dmgMax;
      totalDef += info->defense;
    }
  }
  s_ctx->hero->SetWeaponBonus(totalDmgMin, totalDmgMax);
  s_ctx->hero->SetDefenseBonus(totalDef);
}

// Get the equip slot index for a given item category (-1 if none)
int GetEquipSlotForCategory(uint8_t category, bool isAlternativeHand) {
  switch (category) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
    return isAlternativeHand ? 1
                             : 0; // Weapons → R.Hand or L.Hand if alternative
  case 6:
    return 1; // Shield → L.Hand
  case 7:
    return 2; // Helm
  case 8:
    return 3; // Armor
  case 9:
    return 4; // Pants
  case 10:
    return 5; // Gloves
  case 11:
    return 6; // Boots
  case 12:
    return 7; // Wings
  default:
    return -1;
  }
}

// Get the equipped item's DropDef for comparison (nullptr if slot empty)
const DropDef *GetEquippedDropDef(int equipSlot) {
  if (equipSlot < 0 || equipSlot >= 12)
    return nullptr;
  if (!s_ctx->equipSlots[equipSlot].equipped)
    return nullptr;
  int16_t di = ItemDatabase::GetDefIndexFromCategory(
      s_ctx->equipSlots[equipSlot].category,
      s_ctx->equipSlots[equipSlot].itemIndex);
  return ItemDatabase::GetDropInfo(di);
}

void UpdateAndRenderNotification(float deltaTime) {
  if (s_notifyTimer <= 0.0f)
    return;
  s_notifyTimer -= deltaTime;
  if (s_notifyTimer <= 0.0f) {
    s_notifyTimer = 0.0f;
    return;
  }

  float alpha = (s_notifyTimer < 0.5f) ? (s_notifyTimer / 0.5f) : 1.0f;
  uint8_t a = (uint8_t)(alpha * 255);

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;

  if (s_ctx->fontDefault)
    ImGui::PushFont(s_ctx->fontDefault);
  ImVec2 textSize = ImGui::CalcTextSize(s_notifyText.c_str());
  float px = (displaySize.x - textSize.x) * 0.5f;
  float py = displaySize.y * 0.65f; // Above HUD bar

  // Background box
  float pad = 8.0f;
  dl->AddRectFilled(ImVec2(px - pad, py - pad),
                    ImVec2(px + textSize.x + pad, py + textSize.y + pad),
                    IM_COL32(10, 10, 20, (uint8_t)(a * 0.8f)), 4.0f);
  dl->AddText(ImVec2(px, py), IM_COL32(255, 80, 80, a), s_notifyText.c_str());
  if (s_ctx->fontDefault)
    ImGui::PopFont();
}

int GetSkillResourceCost(uint8_t skillId) {
  for (int i = 0; i < NUM_DK_SKILLS; i++) {
    if (g_dkSkills[i].skillId == skillId)
      return g_dkSkills[i].resourceCost;
  }
  for (int i = 0; i < NUM_DW_SPELLS; i++) {
    if (g_dwSpells[i].skillId == skillId)
      return g_dwSpells[i].resourceCost;
  }
  for (int i = 0; i < NUM_ELF_SKILLS; i++) {
    if (g_elfSkills[i].skillId == skillId)
      return g_elfSkills[i].resourceCost;
  }
  return 0;
}

// Legacy wrapper for compatibility
int GetSkillAGCost(uint8_t skillId) { return GetSkillResourceCost(skillId); }

void ShowNotification(const char *msg) {
  s_notifyText = msg;
  s_notifyTimer = NOTIFY_DURATION;
}

bool HasActiveNotification() { return s_notifyTimer > 0.0f; }

// ─── Region Name Display (Main 5.2: CUIMapName) ─────────────────────────────

void ShowRegionName(const char *name) {
  s_regionName = name;
  s_regionState = RegionNameState::FADEIN;
  s_regionAlpha = 0.0f;
  s_regionShowTimer = 0.0f;

  // Load the correct map name OZT texture for this region
  // Main 5.2: Local/[Language]/ImgsMapName/ — one OZT per map
  const char *oztFile = nullptr;
  if (strcmp(name, "Lorencia") == 0)
    oztFile = "Data/Local/Eng/ImgsMapName/lorencia.OZT";
  else if (strcmp(name, "Devias") == 0)
    oztFile = "Data/Local/Eng/ImgsMapName/devias.OZT";
  else if (strcmp(name, "Dungeon") == 0)
    oztFile =
        "Data/Local/Eng/ImgsMapName/dungeun.OZT"; // Original typo in assets
  else if (strcmp(name, "Noria") == 0)
    oztFile = "Data/Local/Eng/ImgsMapName/noria.OZT";

  if (oztFile) {
    if (TexValid(s_mapNameTexture)) {
      TexDestroy(s_mapNameTexture);
    }
    s_mapNameTexture = TextureLoader::LoadOZT(oztFile);
    if (TexValid(s_mapNameTexture)) {
      // Get dimensions by re-loading raw data (lightweight, only on map enter)
      int tw = 0, th = 0;
      TextureLoader::LoadOZTRaw(oztFile, tw, th);
      s_mapNameTexW = tw;
      s_mapNameTexH = th;
    }
  }
}

bool HasActiveRegionName() { return s_regionState != RegionNameState::HIDE; }

void UpdateAndRenderRegionName(float deltaTime) {
  if (s_regionState == RegionNameState::HIDE)
    return;

  // State machine (Main 5.2: HIDE → FADEIN → SHOW → FADEOUT → HIDE)
  switch (s_regionState) {
  case RegionNameState::FADEIN:
    s_regionAlpha += REGION_FADEIN_SPEED * deltaTime;
    if (s_regionAlpha >= 1.0f) {
      s_regionAlpha = 1.0f;
      s_regionState = RegionNameState::SHOW;
      s_regionShowTimer = REGION_SHOW_TIME;
    }
    break;
  case RegionNameState::SHOW:
    s_regionShowTimer -= deltaTime;
    if (s_regionShowTimer <= 0.0f) {
      s_regionState = RegionNameState::FADEOUT;
    }
    break;
  case RegionNameState::FADEOUT:
    s_regionAlpha -= REGION_FADEOUT_SPEED * deltaTime;
    if (s_regionAlpha <= 0.0f) {
      s_regionAlpha = 0.0f;
      s_regionState = RegionNameState::HIDE;
      return;
    }
    break;
  default:
    return;
  }

  // Render — Main 5.2: pre-rendered OZT image centered, alpha-blended
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  uint8_t a = (uint8_t)(s_regionAlpha * 255);

  if (TexValid(s_mapNameTexture) && s_mapNameTexW > 0 && s_mapNameTexH > 0) {
    // Main 5.2: UIMN_IMG_WIDTH=166, UIMN_IMG_HEIGHT=90
    float scale = displaySize.x / 1400.0f;
    float imgW = (float)s_mapNameTexW * scale;
    float imgH = (float)s_mapNameTexH * scale;
    float px = (displaySize.x - imgW) * 0.5f;
    // Just above HUD
    float py = displaySize.y * 0.80f - imgH;

    ImVec4 tintCol(1.0f, 1.0f, 1.0f, s_regionAlpha);
    dl->AddImage((ImTextureID)TexImID(s_mapNameTexture), ImVec2(px, py),
                 ImVec2(px + imgW, py + imgH), ImVec2(0, 0), ImVec2(1, 1),
                 ImGui::ColorConvertFloat4ToU32(tintCol));
  } else {
    // Fallback: text rendering if OZT not loaded
    ImFont *font = s_ctx->fontRegion ? s_ctx->fontRegion : s_ctx->fontDefault;
    if (font)
      ImGui::PushFont(font);
    ImVec2 textSize = ImGui::CalcTextSize(s_regionName.c_str());
    float px = (displaySize.x - textSize.x) * 0.5f;
    float py = displaySize.y * 0.72f - textSize.y;
    dl->AddText(ImVec2(px, py), IM_COL32(255, 220, 160, a),
                s_regionName.c_str());
    if (font)
      ImGui::PopFont();
  }
}

void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c) {
  float px = GetCharInfoPanelX(), py = PANEL_Y;
  float pw = PanelW();
  char buf[256];

  float W = BASE_PANEL_W; // internal layout width (unscaled)
  float margin = 10;
  float rowH = 18;      // stat row height (compact)
  float rowGap = 1;     // gap between rows
  float sectionGap = 6; // extra gap before each section header

  // ─── Class identification ──────────────────────────────────────────
  uint8_t classCode = s_ctx->hero ? s_ctx->hero->GetClass() : 16;
  bool isDK  = (classCode == 16);
  bool isDW  = (classCode == 0);
  bool isELF = (classCode == 32);
  bool isMG  = (classCode == 48);
  bool showMagicSpeed = isDW || isMG;

  // WoW-style class colors
  ImU32 classColor;
  const char *className;
  if (isDK)       { classColor = IM_COL32(200, 70, 55, 255);  className = "Dark Knight"; }
  else if (isDW)  { classColor = IM_COL32(100, 170, 255, 255); className = "Dark Wizard"; }
  else if (isELF) { classColor = IM_COL32(100, 210, 120, 255); className = "Elf"; }
  else            { classColor = IM_COL32(190, 120, 255, 255); className = "Magic Gladiator"; }

  // WoW palette
  const ImU32 colSection = IM_COL32(200, 170, 60, 220);
  const ImU32 colLabel   = IM_COL32(160, 160, 180, 255);
  const ImU32 colValue   = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen   = IM_COL32(100, 255, 100, 255);
  const ImU32 colSepLine = IM_COL32(100, 85, 50, 140);
  const ImU32 colRowA    = IM_COL32(20, 20, 35, 180);
  const ImU32 colRowB    = IM_COL32(28, 28, 45, 180);
  const ImU32 colDim     = IM_COL32(140, 140, 130, 180);
  const ImU32 colBuff    = IM_COL32(255, 220, 100, 255);

  // Pet & buff state
  bool hasPet = s_ctx->hero && s_ctx->hero->IsPetActive();
  uint8_t petIdx = hasPet ? s_ctx->hero->GetPetItemIndex() : 0;
  bool hasPoints = *s_ctx->serverLevelUpPoints > 0;
  const auto *buffs = ClientPacketHandler::GetActiveBuffs();
  bool hasDefBuff = buffs && buffs[0].active;
  bool hasDmgBuff = buffs && buffs[1].active;
  bool hasAnyBuff = hasDefBuff || hasDmgBuff || hasPet;

  // ─── Pre-compute panel height ──────────────────────────────────────
  float contentH = 62; // header + XP bar
  contentH += 16 + 4 * (rowH + rowGap); // Attributes
  if (hasPoints) contentH += 16;
  contentH += sectionGap + 16; // Offense header
  contentH += (showMagicSpeed ? 6 : 5) * (rowH + rowGap); // Offense rows
  contentH += sectionGap + 16; // Defense header
  contentH += 2 * (rowH + rowGap); // Defense rows
  if (hasAnyBuff) {
    contentH += sectionGap + 16; // Buffs header
    if (hasDefBuff) contentH += (rowH + rowGap);
    if (hasDmgBuff) contentH += (rowH + rowGap);
    if (hasPet)     contentH += (rowH + rowGap);
  }
  contentH += sectionGap + 18 * 2 + 4; // Resources (HP + MP bars)
  contentH += sectionGap + 16 + 4 * (rowH + rowGap); // Class Scaling
  contentH += 8; // bottom padding
  float ph = contentH * g_uiPanelScale;

  DrawStyledPanel(dl, c.ToScreenX(px), c.ToScreenY(py), c.ToScreenX(px + pw),
                  c.ToScreenY(py + ph));
  DrawCloseButton(dl, c, px, py);

  // ─── Header: class-colored name + level ────────────────────────────
  DrawPanelTextCentered(dl, c, px, py, 0, 8, W, s_ctx->characterName,
                        classColor, s_ctx->fontBold);

  snprintf(buf, sizeof(buf), "Level %d  %s", *s_ctx->serverLevel, className);
  DrawPanelTextCentered(dl, c, px, py, 0, 28, W, buf, colLabel);

  // Class color accent line
  {
    float lx0 = c.ToScreenX(px + (margin + 20) * g_uiPanelScale);
    float lx1 = c.ToScreenX(px + (W - margin - 20) * g_uiPanelScale);
    float ly  = c.ToScreenY(py + 43 * g_uiPanelScale);
    uint8_t cr = (classColor >> 0) & 0xFF;
    uint8_t cg = (classColor >> 8) & 0xFF;
    uint8_t cb = (classColor >> 16) & 0xFF;
    dl->AddLine(ImVec2(lx0, ly), ImVec2(lx1, ly),
                IM_COL32(cr, cg, cb, 100), 1.5f);
  }

  // ─── XP progress bar ──────────────────────────────────────────────
  float xpFrac = 0.0f;
  uint64_t nextXp = s_ctx->hero->GetNextExperience();
  uint64_t curXp  = (uint64_t)*s_ctx->serverXP;
  uint64_t prevXp = s_ctx->hero->CalcXPForLevel(*s_ctx->serverLevel);
  if (nextXp > prevXp)
    xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
  xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);
  {
    float bx = margin, by = 46, bw = W - margin * 2, bh = 10;
    snprintf(buf, sizeof(buf), "%.1f%%", xpFrac * 100.0f);
    float sx0 = c.ToScreenX(px + bx * g_uiPanelScale);
    float sy0 = c.ToScreenY(py + by * g_uiPanelScale);
    float sx1 = c.ToScreenX(px + (bx + bw) * g_uiPanelScale);
    float sy1 = c.ToScreenY(py + (by + bh) * g_uiPanelScale);
    DrawStyledBar(dl, sx0, sy0, sx1 - sx0, sy1 - sy0, xpFrac,
                  IM_COL32(60, 200, 100, 230), IM_COL32(30, 140, 60, 230), buf);
  }

  // ─── Section header helper ─────────────────────────────────────────
  auto drawSectionHeader = [&](float relY, const char *text) {
    float sx0 = c.ToScreenX(px + margin * g_uiPanelScale);
    float sx1 = c.ToScreenX(px + (W - margin) * g_uiPanelScale);
    float sy  = c.ToScreenY(py + (relY + 6) * g_uiPanelScale);
    ImVec2 tsz = ImGui::CalcTextSize(text);
    float tw = tsz.x + 12;
    float cx0 = (sx0 + sx1 - tw) * 0.5f;
    float cx1 = cx0 + tw;
    dl->AddLine(ImVec2(sx0, sy), ImVec2(cx0 - 4, sy), colSepLine);
    dl->AddLine(ImVec2(cx1 + 4, sy), ImVec2(sx1, sy), colSepLine);
    DrawPanelTextCentered(dl, c, px, py, 0, relY, W, text, colSection);
  };

  // ─── Stat row helper ───────────────────────────────────────────────
  int rowIdx = 0;
  auto drawStatRow = [&](float relY, const char *label, const char *value,
                         ImU32 valColor = IM_COL32(255, 255, 255, 255)) {
    float rx0 = c.ToScreenX(px + margin * g_uiPanelScale);
    float ry0 = c.ToScreenY(py + relY * g_uiPanelScale);
    float rx1 = c.ToScreenX(px + (W - margin) * g_uiPanelScale);
    float ry1 = c.ToScreenY(py + (relY + rowH) * g_uiPanelScale);
    dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1),
                      (rowIdx & 1) ? colRowB : colRowA, 2.0f);
    DrawPanelText(dl, c, px, py, margin + 6, relY + 2, label, colLabel);
    DrawPanelTextRight(dl, c, px, py, margin + 6, relY + 2, W - margin * 2 - 34,
                       value, valColor);
    rowIdx++;
  };

  // ─── Attributes ────────────────────────────────────────────────────
  float curY = 62;
  drawSectionHeader(curY, "Attributes");
  curY += 16;

  const char *statLabels[] = {"Strength", "Agility", "Vitality", "Energy"};
  int statValues[] = {*s_ctx->serverStr, *s_ctx->serverDex, *s_ctx->serverVit,
                      *s_ctx->serverEne};
  rowIdx = 0;
  for (int i = 0; i < 4; i++) {
    float ry = curY + i * (rowH + rowGap);
    snprintf(buf, sizeof(buf), "%d", statValues[i]);
    drawStatRow(ry, statLabels[i], buf);

    if (hasPoints) {
      float btnX = c.ToScreenX(px + (W - margin - 16) * g_uiPanelScale);
      float btnY = c.ToScreenY(py + (ry + 2) * g_uiPanelScale);
      float btnS = c.ToScreenY(py + (ry + rowH - 2) * g_uiPanelScale) - btnY;
      ImVec2 bp0(btnX, btnY), bp1(btnX + btnS, btnY + btnS);
      ImVec2 mp = ImGui::GetIO().MousePos;
      bool hov = mp.x >= bp0.x && mp.x < bp1.x && mp.y >= bp0.y && mp.y < bp1.y;
      dl->AddRectFilled(bp0, bp1,
          hov ? IM_COL32(80, 200, 80, 255) : IM_COL32(50, 140, 50, 230), 3.0f);
      dl->AddRect(bp0, bp1,
          hov ? IM_COL32(130, 255, 130, 180) : IM_COL32(40, 100, 40, 180), 3.0f);
      ImVec2 ps = ImGui::CalcTextSize("+");
      float ptx = btnX + (btnS - ps.x) * 0.5f;
      float pty = btnY + (btnS - ps.y) * 0.5f;
      dl->AddText(ImVec2(ptx + 1, pty + 1), IM_COL32(0, 0, 0, 200), "+");
      dl->AddText(ImVec2(ptx, pty), colValue, "+");
    }
  }
  curY += 4 * (rowH + rowGap);

  if (hasPoints) {
    snprintf(buf, sizeof(buf), "%d points", *s_ctx->serverLevelUpPoints);
    DrawPanelTextCentered(dl, c, px, py, 0, curY + 1, W, buf, colGreen);
    curY += 16;
  }

  // ─── Offense ───────────────────────────────────────────────────────
  curY += sectionGap;
  drawSectionHeader(curY, "Offense");
  curY += 16;

  // Use HeroCharacter computed damage (proper per-class formulas)
  int dMin = s_ctx->hero->GetDamageMin();
  int dMax = s_ctx->hero->GetDamageMax();

  rowIdx = 0;
  const char *dmgLabel = "Damage";
  if (isDW) dmgLabel = "Wizardry";
  else if (isELF && s_ctx->hero->GetWeaponCategory() == 4) dmgLabel = "Ranged Dmg";
  snprintf(buf, sizeof(buf), "%d - %d", dMin, dMax);
  drawStatRow(curY, dmgLabel, buf);
  curY += rowH + rowGap;

  snprintf(buf, sizeof(buf), "%d", *s_ctx->serverAttackSpeed);
  drawStatRow(curY, "Attack Speed", buf);
  curY += rowH + rowGap;

  if (showMagicSpeed) {
    snprintf(buf, sizeof(buf), "%d", *s_ctx->serverMagicSpeed);
    drawStatRow(curY, "Magic Speed", buf, IM_COL32(150, 180, 255, 255));
    curY += rowH + rowGap;
  }

  snprintf(buf, sizeof(buf), "%d", s_ctx->hero->GetAttackSuccessRate());
  drawStatRow(curY, "Attack Rate", buf);
  curY += rowH + rowGap;

  drawStatRow(curY, "Critical", "5%", IM_COL32(100, 200, 255, 255));
  curY += rowH + rowGap;

  drawStatRow(curY, "Excellent", "1%", colGreen);
  curY += rowH + rowGap;

  // ─── Defense ───────────────────────────────────────────────────────
  curY += sectionGap;
  drawSectionHeader(curY, "Defense");
  curY += 16;

  int totalDef = s_ctx->hero->GetDefense();
  int equipDef = s_ctx->hero->GetDefenseBonus();
  rowIdx = 0;
  if (equipDef > 0)
    snprintf(buf, sizeof(buf), "%d (+%d)", totalDef - equipDef, equipDef);
  else
    snprintf(buf, sizeof(buf), "%d", totalDef);
  drawStatRow(curY, "Defense", buf);
  curY += rowH + rowGap;

  snprintf(buf, sizeof(buf), "%d", s_ctx->hero->GetDefenseSuccessRate());
  drawStatRow(curY, "Defense Rate", buf);
  curY += rowH + rowGap;

  // ─── Active Buffs (Elf auras + Pet bonuses) ────────────────────────
  if (hasAnyBuff) {
    curY += sectionGap;
    drawSectionHeader(curY, "Active Buffs");
    curY += 16;
    rowIdx = 0;

    if (hasDefBuff) {
      snprintf(buf, sizeof(buf), "+%d Defense", buffs[0].value);
      drawStatRow(curY, "Greater Def", buf, colBuff);
      curY += rowH + rowGap;
    }
    if (hasDmgBuff) {
      snprintf(buf, sizeof(buf), "+%d Damage", buffs[1].value);
      drawStatRow(curY, "Greater Dmg", buf, colBuff);
      curY += rowH + rowGap;
    }
    if (hasPet) {
      if (petIdx == 0) // Guardian Angel
        drawStatRow(curY, "Angel", "20% Absorb", IM_COL32(180, 220, 255, 255));
      else // Imp
        drawStatRow(curY, "Imp", "+30% Damage", IM_COL32(255, 160, 100, 255));
      curY += rowH + rowGap;
    }
  }

  // ─── Resources ─────────────────────────────────────────────────────
  curY += sectionGap;

  int curHP = s_ctx->hero->GetHP();
  int maxHP = s_ctx->hero->GetMaxHP();
  int curMP = isDK ? s_ctx->hero->GetAG() : s_ctx->hero->GetMana();
  int maxMP = isDK ? s_ctx->hero->GetMaxAG() : s_ctx->hero->GetMaxMana();
  float hpFrac = (maxHP > 0) ? (float)curHP / maxHP : 0.0f;
  float mpFrac = (maxMP > 0) ? (float)curMP / maxMP : 0.0f;

  // HP bar
  {
    float bx = margin, bw = W - margin * 2, bh = 14;
    snprintf(buf, sizeof(buf), "HP  %d / %d", std::max(curHP, 0), maxHP);
    float sx0 = c.ToScreenX(px + bx * g_uiPanelScale);
    float sy0 = c.ToScreenY(py + curY * g_uiPanelScale);
    float sx1 = c.ToScreenX(px + (bx + bw) * g_uiPanelScale);
    float sy1 = c.ToScreenY(py + (curY + bh) * g_uiPanelScale);
    DrawStyledBar(dl, sx0, sy0, sx1 - sx0, sy1 - sy0, hpFrac,
                  IM_COL32(220, 50, 50, 230), IM_COL32(160, 30, 30, 230), buf);
  }
  curY += 18;

  // AG/Mana bar (class-themed colors)
  {
    const char *resLabel = isDK ? "AG" : "Mana";
    float bx = margin, bw = W - margin * 2, bh = 14;
    snprintf(buf, sizeof(buf), "%s  %d / %d", resLabel, std::max(curMP, 0), maxMP);
    float sx0 = c.ToScreenX(px + bx * g_uiPanelScale);
    float sy0 = c.ToScreenY(py + curY * g_uiPanelScale);
    float sx1 = c.ToScreenX(px + (bx + bw) * g_uiPanelScale);
    float sy1 = c.ToScreenY(py + (curY + bh) * g_uiPanelScale);
    ImU32 barTop, barBot;
    if (isDK)       { barTop = IM_COL32(180, 160, 40, 230);  barBot = IM_COL32(130, 115, 20, 230); }
    else if (isDW)  { barTop = IM_COL32(40, 100, 220, 230);  barBot = IM_COL32(25, 60, 160, 230); }
    else if (isELF) { barTop = IM_COL32(40, 180, 120, 230);  barBot = IM_COL32(25, 120, 80, 230); }
    else            { barTop = IM_COL32(140, 60, 200, 230);  barBot = IM_COL32(90, 35, 140, 230); }
    DrawStyledBar(dl, sx0, sy0, sx1 - sx0, sy1 - sy0, mpFrac, barTop, barBot, buf);
  }
  curY += 18;

  // ─── Class Scaling (what stats do for this class) ──────────────────
  curY += sectionGap;
  drawSectionHeader(curY, "Stat Scaling");
  curY += 16;

  struct ScalingRow { const char *stat; const char *effect; };
  ScalingRow scaling[4];

  if (isDK) {
    scaling[0] = {"STR", "+1 Dmg / 4-6 pts"};
    scaling[1] = {"AGI", "+1 Def / 3 pts"};
    scaling[2] = {"VIT", "+3 HP / point"};
    scaling[3] = {"ENE", "+1.0 AG / point"};
  } else if (isDW) {
    scaling[0] = {"STR", "+1 Dmg / 4-6 pts"};
    scaling[1] = {"AGI", "+1 Def / 4 pts"};
    scaling[2] = {"VIT", "+2 HP / point"};
    scaling[3] = {"ENE", "Wiz/4-9, +2 Mana"};
  } else if (isELF) {
    scaling[0] = {"STR", "+1 Dmg / 8-14 pts"};
    scaling[1] = {"AGI", "Dmg/4-7, Def/10"};
    scaling[2] = {"VIT", "+2 HP / point"};
    scaling[3] = {"ENE", "+1.5 Mana / point"};
  } else { // MG
    scaling[0] = {"STR", "+1 Dmg / 4-6 pts"};
    scaling[1] = {"AGI", "+1 Def / 4 pts"};
    scaling[2] = {"VIT", "+2 HP / point"};
    scaling[3] = {"ENE", "Dmg/8-12, +2 Mana"};
  }

  rowIdx = 0;
  for (int i = 0; i < 4; i++) {
    float ry = curY + i * (rowH + rowGap);
    drawStatRow(ry, scaling[i].stat, scaling[i].effect, colDim);
  }
}

void RenderInventoryPanel(ImDrawList *dl, const UICoords &c) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  float px = GetInventoryPanelX(), py = PANEL_Y;
  float pw = PanelW(), ph = PanelH();
  ImVec2 mp = ImGui::GetIO().MousePos;

  // Colors
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colHeader = IM_COL32(200, 180, 120, 255);
  const ImU32 colSlotBg =
      IM_COL32(0, 0, 0, 60); // subtle background, grid visible
  const ImU32 colSlotBr = IM_COL32(80, 75, 60, 255);
  const ImU32 colGold = IM_COL32(255, 215, 0, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colDragHi = IM_COL32(255, 255, 0, 100);
  char buf[256];

  DrawStyledPanel(dl, c.ToScreenX(px), c.ToScreenY(py), c.ToScreenX(px + pw),
                  c.ToScreenY(py + ph));

  // Title
  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Inventory",
                        colTitle, s_ctx->fontDefault);

  DrawCloseButton(dl, c, px, py);

  // Equipment Layout
  for (auto &ep : g_equipLayoutRects) {
    float vx = px + ep.x * g_uiPanelScale;
    float vy = py + ep.y * g_uiPanelScale;
    float sw = ep.w * g_uiPanelScale;
    float sh = ep.h * g_uiPanelScale;

    ImVec2 sMin(c.ToScreenX(vx), c.ToScreenY(vy));
    ImVec2 sMax(c.ToScreenX(vx + sw), c.ToScreenY(vy + sh));

    bool hoverSlot =
        mp.x >= sMin.x && mp.x < sMax.x && mp.y >= sMin.y && mp.y < sMax.y;

    // Always draw slot background fill and silhouette (visible behind 3D items)
    dl->AddRectFilled(sMin, sMax, colSlotBg, 3.0f);
    if (TexValid(g_slotBackgrounds[ep.slot])) {
      dl->AddImage((ImTextureID)TexImID(g_slotBackgrounds[ep.slot]), sMin,
                   sMax, ImVec2(0, 0), ImVec2(1, 1),
                   IM_COL32(255, 255, 255, 200));
    }

    dl->AddRect(sMin, sMax, hoverSlot && g_isDragging ? colDragHi : colSlotBr,
                3.0f);

    bool isBeingDragged = (g_isDragging && g_dragFromEquipSlot == ep.slot);

    if (s_ctx->equipSlots[ep.slot].equipped && !isBeingDragged) {
      std::string modelName = s_ctx->equipSlots[ep.slot].modelFile;
      if (!modelName.empty()) {
        int16_t defIdx = ItemDatabase::GetDefIndexFromCategory(
            s_ctx->equipSlots[ep.slot].category,
            s_ctx->equipSlots[ep.slot].itemIndex);
        int slotY = (int)sMin.y; // BGFX: top-left origin
        g_renderQueue.push_back({modelName, defIdx, (int)sMin.x,
                                 slotY, (int)(sMax.x - sMin.x),
                                 (int)(sMax.y - sMin.y), hoverSlot,
                                 s_ctx->equipSlots[ep.slot].itemLevel});
      }
      if (hoverSlot) {
        AddPendingItemTooltip(ItemDatabase::GetDefIndexFromCategory(
                                  s_ctx->equipSlots[ep.slot].category,
                                  s_ctx->equipSlots[ep.slot].itemIndex),
                              s_ctx->equipSlots[ep.slot].itemLevel, 0,
                              s_ctx->equipSlots[ep.slot].optionFlags);
      }

      // Arrow/bolt quantity overlay on equipment slot (only for ammo, not weapons/armor)
      uint8_t eqCat = s_ctx->equipSlots[ep.slot].category;
      uint8_t eqIdx = s_ctx->equipSlots[ep.slot].itemIndex;
      bool isEquipAmmo = (eqCat == 4 && (eqIdx == 7 || eqIdx == 15));
      if (isEquipAmmo && s_ctx->equipSlots[ep.slot].quantity > 0) {
        DeferredOverlay ov;
        snprintf(ov.text, sizeof(ov.text), "%d",
                 s_ctx->equipSlots[ep.slot].quantity);
        ImVec2 qSize = ImGui::CalcTextSize(ov.text);
        ov.x = sMax.x - qSize.x - 2;
        ov.y = sMin.y + 1;
        ov.color = IM_COL32(255, 210, 80, 255);
        g_deferredOverlays.push_back(ov);
      }
    }
  }

  // Bag Grid
  DrawPanelText(dl, c, px, py, 15, 198, "Bag", colHeader);
  float gridRX = 15.0f, gridRY = 208.0f;
  float cellW = 20.0f, cellH = 20.0f;
  float gap = 0.0f;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      float rX = gridRX + col * (cellW + gap);
      float rY = gridRY + row * (cellH + gap);
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      float sW = cellW * g_uiPanelScale;
      float sH = cellH * g_uiPanelScale;

      ImVec2 sMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 sMax(c.ToScreenX(vX + sW), c.ToScreenY(vY + sH));
      dl->AddRectFilled(sMin, sMax, colSlotBg, 1.0f);
      dl->AddRect(sMin, sMax, colSlotBr, 1.0f);
    }
  }

  // Bag Items
  bool processed[INVENTORY_SLOTS] = {false};
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (processed[slot])
        continue;

      bool isThisBeingDragged = (g_isDragging && g_dragFromSlot == slot);

      if (s_ctx->inventory[slot].occupied) {
        auto it = g_itemDefs.find(s_ctx->inventory[slot].defIndex);
        if (it != g_itemDefs.end()) {
          const auto &def = it->second;
          // Mark ENTIRE footprint as processed
          for (int hh = 0; hh < def.height; hh++)
            for (int ww = 0; ww < def.width; ww++)
              if (slot + hh * 8 + ww < INVENTORY_SLOTS)
                processed[slot + hh * 8 + ww] = true;

          if (isThisBeingDragged)
            continue; // Skip rendering visual of the item AT source slot

          float rX = gridRX + col * (cellW + gap);
          float rY = gridRY + row * (cellH + gap);
          float vX = px + rX * g_uiPanelScale;
          float vY = py + rY * g_uiPanelScale;
          ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
          ImVec2 iMax(c.ToScreenX(vX + def.width * cellW * g_uiPanelScale),
                      c.ToScreenY(vY + def.height * cellH * g_uiPanelScale));
          bool hoverItem = mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y &&
                           mp.y < iMax.y;

          if (hoverItem)
            dl->AddRectFilled(iMin, iMax, IM_COL32(255, 255, 255, 30), 2.0f);

          const char *model = def.modelFile.empty()
                                  ? ItemDatabase::GetDropModelName(
                                        s_ctx->inventory[slot].defIndex)
                                  : def.modelFile.c_str();
          if (model && model[0]) {
            int slotY = (int)iMin.y;
            g_renderQueue.push_back({model, s_ctx->inventory[slot].defIndex,
                                     (int)iMin.x, slotY,
                                     (int)(iMax.x - iMin.x),
                                     (int)(iMax.y - iMin.y), hoverItem,
                                     s_ctx->inventory[slot].itemLevel});
          }
          if (hoverItem && !g_isDragging)
            AddPendingItemTooltip(s_ctx->inventory[slot].defIndex,
                                  s_ctx->inventory[slot].itemLevel, 0,
                                  s_ctx->inventory[slot].optionFlags);

          // Quantity overlay (deferred — drawn after 3D item models)
          if (s_ctx->inventory[slot].quantity > 1) {
            DeferredOverlay ov;
            snprintf(ov.text, sizeof(ov.text), "%d",
                     s_ctx->inventory[slot].quantity);
            ImVec2 qSize = ImGui::CalcTextSize(ov.text);
            ov.x = iMax.x - qSize.x - 2;
            ov.y = iMin.y + 1;
            ov.color = IM_COL32(255, 210, 80, 255);
            g_deferredOverlays.push_back(ov);
          }
        }
      }
    }
  }

  // Drop-target preview: highlight the item's footprint on the grid
  if (g_isDragging) {
    auto dit = g_itemDefs.find(g_dragDefIndex);
    if (dit != g_itemDefs.end()) {
      int iw = dit->second.width;
      int ih = dit->second.height;
      float gridVX = px + gridRX * g_uiPanelScale;
      float gridVY = py + gridRY * g_uiPanelScale;
      float gridVW = 8 * cellW * g_uiPanelScale;
      float gridVH = 8 * cellH * g_uiPanelScale;

      // Check if mouse is over the bag grid
      if (mp.x >= c.ToScreenX(gridVX) && mp.x < c.ToScreenX(gridVX + gridVW) &&
          mp.y >= c.ToScreenY(gridVY) && mp.y < c.ToScreenY(gridVY + gridVH)) {
        // Compute which cell the mouse is over
        float localX = (mp.x - c.ToScreenX(gridVX)) /
                       (c.ToScreenX(gridVX + cellW * g_uiPanelScale) -
                        c.ToScreenX(gridVX));
        float localY = (mp.y - c.ToScreenY(gridVY)) /
                       (c.ToScreenY(gridVY + cellH * g_uiPanelScale) -
                        c.ToScreenY(gridVY));
        int hCol = (int)localX;
        int hRow = (int)localY;
        if (hCol >= 0 && hCol < 8 && hRow >= 0 && hRow < 8) {
          bool fits = (hCol + iw <= 8 && hRow + ih <= 8);
          if (fits) {
            // Check occupancy (ignoring the item being dragged)
            for (int rr = 0; rr < ih && fits; rr++) {
              for (int cc = 0; cc < iw && fits; cc++) {
                int s = (hRow + rr) * 8 + (hCol + cc);
                if (s_ctx->inventory[s].occupied) {
                  // If dragging from bag, ignore source cells
                  if (g_dragFromSlot >= 0) {
                    int pRow = g_dragFromSlot / 8;
                    int pCol = g_dragFromSlot % 8;
                    if (hRow + rr >= pRow && hRow + rr < pRow + ih &&
                        hCol + cc >= pCol && hCol + cc < pCol + iw)
                      continue; // Source cell, ignore
                  }
                  fits = false;
                }
              }
            }
          }
          // Draw the preview outline
          ImU32 previewCol =
              fits ? IM_COL32(50, 200, 50, 160) : IM_COL32(200, 50, 50, 160);
          float ox = px + (gridRX + hCol * cellW) * g_uiPanelScale;
          float oy = py + (gridRY + hRow * cellH) * g_uiPanelScale;
          float ow = iw * cellW * g_uiPanelScale;
          float oh = ih * cellH * g_uiPanelScale;
          ImVec2 pMin(c.ToScreenX(ox), c.ToScreenY(oy));
          ImVec2 pMax(c.ToScreenX(ox + ow), c.ToScreenY(oy + oh));
          dl->AddRectFilled(pMin, pMax, (previewCol & 0x00FFFFFF) | 0x30000000,
                            2.0f);
          dl->AddRect(pMin, pMax, previewCol, 2.0f, 0, 2.0f);
        }
      }
    }
  }

  if (g_isDragging && g_dragDefIndex >= 0) {
    auto it = g_itemDefs.find(g_dragDefIndex);
    if (it != g_itemDefs.end()) {
      const auto &def = it->second;
      float dw = def.width * 32.0f;
      float dh = def.height * 32.0f;
      ImVec2 iMin(mp.x - dw * 0.5f, mp.y - dh * 0.5f);
      ImVec2 iMax(iMin.x + dw, iMin.y + dh);

      dl->AddRectFilled(iMin, iMax, IM_COL32(30, 30, 50, 180), 3.0f);
      // Queue 3D render for dragged item
      int dragY = (int)iMin.y;
      g_renderQueue.push_back({def.modelFile, g_dragDefIndex, (int)iMin.x,
                               dragY, (int)dw, (int)dh, false,
                               g_dragItemLevel});

      if (g_dragItemLevel > 0)
        snprintf(buf, sizeof(buf), "%s +%d", def.name.c_str(), g_dragItemLevel);
      else
        snprintf(buf, sizeof(buf), "%s", def.name.c_str());
      dl->AddText(ImVec2(iMin.x, iMax.y + 2), colGold, buf);
    }
  }

  // Tooltip on hover (bag items)
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int slot = row * 8 + col;
      if (!s_ctx->inventory[slot].occupied || !s_ctx->inventory[slot].primary)
        continue;

      auto it = g_itemDefs.find(s_ctx->inventory[slot].defIndex);
      int dw = 1, dh = 1;
      if (it != g_itemDefs.end()) {
        dw = it->second.width;
        dh = it->second.height;
      }
      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 iMax(c.ToScreenX(vX + dw * cellW * g_uiPanelScale),
                  c.ToScreenY(vY + dh * cellH * g_uiPanelScale));

      if (mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y && mp.y < iMax.y &&
          !g_isDragging) {
        AddPendingItemTooltip(s_ctx->inventory[slot].defIndex,
                              s_ctx->inventory[slot].itemLevel, 0,
                              s_ctx->inventory[slot].optionFlags);
      }
    }
  }

  // Zen display at the bottom
  {
    dl->AddRectFilled(ImVec2(c.ToScreenX(px + 10 * g_uiPanelScale),
                             c.ToScreenY(py + 400 * g_uiPanelScale)),
                      ImVec2(c.ToScreenX(px + 180 * g_uiPanelScale),
                             c.ToScreenY(py + 424 * g_uiPanelScale)),
                      IM_COL32(20, 25, 40, 255), 3.0f);
    char zenBuf[64];
    std::string s = std::to_string(*s_ctx->zen);
    int n = s.length() - 3;
    while (n > 0) {
      s.insert(n, ",");
      n -= 3;
    }
    snprintf(zenBuf, sizeof(zenBuf), "%s Zen", s.c_str());
    DrawPanelTextRight(dl, c, px, py, 10, 405, 160, zenBuf, colGold);
  }
}

void RenderShopPanel(ImDrawList *dl, const UICoords &c) {
  if (!*s_ctx->shopOpen)
    return;
  auto &g_itemDefs = ItemDatabase::GetItemDefs();
  float px = GetShopPanelX(), py = PANEL_Y;
  float pw = PanelW(), ph = PanelH();
  ImVec2 mp = ImGui::GetIO().MousePos;

  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);

  DrawStyledPanel(dl, c.ToScreenX(px), c.ToScreenY(py), c.ToScreenX(px + pw),
                  c.ToScreenY(py + ph));

  DrawPanelTextCentered(dl, c, px, py, 0, 11, BASE_PANEL_W, "Shop", colTitle,
                        s_ctx->fontDefault);

  DrawCloseButton(dl, c, px, py);

  // Detect new shop list from server (items changed)
  // Compare both count AND first item defIndex to catch same-size shop swaps
  size_t curCount = s_ctx->shopItems->size();
  int16_t firstDef = (curCount > 0) ? (*s_ctx->shopItems)[0].defIndex : -1;
  if (curCount != s_lastShopItemCount || firstDef != s_lastShopFirstDef) {
    s_shopGridDirty = true;
    s_lastShopItemCount = curCount;
    s_lastShopFirstDef = firstDef;
  }
  if (s_shopGridDirty)
    RebuildShopGrid();

  // Grid constants (same cell size as inventory bag grid)
  float gridRX = 15.0f, gridRY = 35.0f;
  float cellW = 20.0f, cellH = 20.0f;

  const ImU32 colSlotBg = IM_COL32(0, 0, 0, 60);
  const ImU32 colSlotBr = IM_COL32(80, 75, 60, 255);

  // Draw empty grid cells
  for (int row = 0; row < s_shopGridRows; row++) {
    for (int col = 0; col < SHOP_GRID_COLS; col++) {
      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      float sW = cellW * g_uiPanelScale;
      float sH = cellH * g_uiPanelScale;

      ImVec2 sMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 sMax(c.ToScreenX(vX + sW), c.ToScreenY(vY + sH));
      dl->AddRectFilled(sMin, sMax, colSlotBg, 1.0f);
      dl->AddRect(sMin, sMax, colSlotBr, 1.0f);
    }
  }

  // Draw shop items in grid
  bool processed[SHOP_GRID_MAX_SLOTS] = {false};
  for (int row = 0; row < s_shopGridRows; row++) {
    for (int col = 0; col < SHOP_GRID_COLS; col++) {
      int slot = row * SHOP_GRID_COLS + col;
      if (processed[slot])
        continue;
      if (!s_shopGrid[slot].occupied || !s_shopGrid[slot].primary)
        continue;

      // Skip rendering if this item is being dragged from the shop
      bool isBeingDragged = (g_isDragging && g_dragFromShopSlot == slot);

      auto it = g_itemDefs.find(s_shopGrid[slot].defIndex);
      if (it == g_itemDefs.end())
        continue;
      const auto &def = it->second;

      // Mark entire footprint as processed
      for (int dy = 0; dy < def.height; dy++)
        for (int dx = 0; dx < def.width; dx++) {
          int s = (row + dy) * SHOP_GRID_COLS + (col + dx);
          if (s < SHOP_GRID_MAX_SLOTS)
            processed[s] = true;
        }

      float rX = gridRX + col * cellW;
      float rY = gridRY + row * cellH;
      float vX = px + rX * g_uiPanelScale;
      float vY = py + rY * g_uiPanelScale;
      ImVec2 iMin(c.ToScreenX(vX), c.ToScreenY(vY));
      ImVec2 iMax(c.ToScreenX(vX + def.width * cellW * g_uiPanelScale),
                  c.ToScreenY(vY + def.height * cellH * g_uiPanelScale));

      bool hoverItem =
          mp.x >= iMin.x && mp.x < iMax.x && mp.y >= iMin.y && mp.y < iMax.y;

      if (!isBeingDragged) {
        if (hoverItem)
          dl->AddRectFilled(iMin, iMax, IM_COL32(255, 255, 255, 30), 2.0f);

        const char *model =
            def.modelFile.empty()
                ? ItemDatabase::GetDropModelName(s_shopGrid[slot].defIndex)
                : def.modelFile.c_str();
        if (model && model[0]) {
          int slotY = (int)iMin.y;
          g_renderQueue.push_back(
              {model, s_shopGrid[slot].defIndex, (int)iMin.x,
               slotY, (int)(iMax.x - iMin.x),
               (int)(iMax.y - iMin.y), hoverItem, s_shopGrid[slot].itemLevel});
        }

        // Stack quantity overlay for potions/ammo
        if (s_shopGrid[slot].stackQty > 0) {
          DeferredOverlay ov;
          snprintf(ov.text, sizeof(ov.text), "x%d",
                   s_shopGrid[slot].stackQty);
          ImVec2 qSize = ImGui::CalcTextSize(ov.text);
          ov.x = iMax.x - qSize.x - 2;
          ov.y = iMin.y + 1;
          ov.color = IM_COL32(255, 210, 80, 255);
          g_deferredOverlays.push_back(ov);
        }
      }

      if (hoverItem && !g_isDragging)
        AddPendingItemTooltip(s_shopGrid[slot].defIndex,
                              s_shopGrid[slot].itemLevel,
                              s_shopGrid[slot].buyPrice);
    }
  }
}

bool HandlePanelClick(float vx, float vy) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();

  // Shop Panel
  if (*s_ctx->shopOpen && IsPointInPanel(vx, vy, GetShopPanelX())) {
    float px = GetShopPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      SoundManager::Play(SOUND_CLICK01);
      *s_ctx->shopOpen = false;
      s_shopGridDirty = true;
      return true;
    }

    // Grid hit test
    float gridRX = 15.0f, gridRY = 35.0f;
    float cellW = 20.0f, cellH = 20.0f;

    if (relX >= gridRX && relX < gridRX + SHOP_GRID_COLS * cellW &&
        relY >= gridRY && relY < gridRY + s_shopGridRows * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
      int slot = row * SHOP_GRID_COLS + col;

      if (slot >= 0 && slot < SHOP_GRID_MAX_SLOTS &&
          s_shopGrid[slot].occupied) {
        // Find primary slot
        int primarySlot = slot;
        if (!s_shopGrid[slot].primary) {
          int16_t di = s_shopGrid[slot].defIndex;
          for (int r = 0; r <= row; r++) {
            for (int c2 = 0; c2 <= col; c2++) {
              int s = r * SHOP_GRID_COLS + c2;
              if (s_shopGrid[s].occupied && s_shopGrid[s].primary &&
                  s_shopGrid[s].defIndex == di) {
                auto it2 = g_itemDefs.find(di);
                if (it2 != g_itemDefs.end()) {
                  if (r + it2->second.height > row &&
                      c2 + it2->second.width > col) {
                    primarySlot = s;
                  }
                }
              }
            }
          }
        }

        // Start drag from shop (buy via drag-and-drop)
        g_dragFromShopSlot = primarySlot;
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        g_dragDefIndex = s_shopGrid[primarySlot].defIndex;
        g_dragQuantity = 1;
        g_dragItemLevel = s_shopGrid[primarySlot].itemLevel;
        g_isDragging = true;
        return true;
      }
    }

    return true;
  }

  // Character Info panel
  if (*s_ctx->showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX())) {
    float px = GetCharInfoPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button (relative: 190 - 24, 6, size 16, 12)
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      SoundManager::Play(SOUND_CLICK01);
      *s_ctx->showCharInfo = false;
      return true;
    }

    // Stat "+" buttons — must match RenderCharInfoPanel layout:
    // curY=62 (Attributes header) + 16 = 78, rowH=18, rowGap=1
    constexpr float statStartY = 78.0f;
    constexpr float statRowStep = 19.0f; // rowH(18) + rowGap(1)
    if (*s_ctx->serverLevelUpPoints > 0) {
      for (int i = 0; i < 4; i++) {
        float btnX = BASE_PANEL_W - 10 - 16, btnY = statStartY + i * statRowStep + 2;
        float btnS = 14.0f; // rowH - 4
        if (relX >= btnX && relX < btnX + btnS && relY >= btnY &&
            relY < btnY + btnS) {
          s_ctx->server->SendStatAlloc(static_cast<uint8_t>(i));
          SoundManager::Play(SOUND_CLICK01);
          return true;
        }
      }
    }
    return true; // Click consumed by panel
  }

  // Inventory panel (also visible when shop is open)
  if ((*s_ctx->showInventory || *s_ctx->shopOpen) &&
      IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // Close button
    if (relX >= 190 - 24 && relX < 190 - 8 && relY >= 6 && relY < 18) {
      SoundManager::Play(SOUND_CLICK01);
      *s_ctx->showInventory = false;
      return true;
    }

    // Equipment slots: start drag
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.x && relX < ep.x + ep.w && relY >= ep.y &&
          relY < ep.y + ep.h) {
        if (s_ctx->equipSlots[ep.slot].equipped) {
          g_dragFromSlot = -1;
          g_dragFromEquipSlot = ep.slot;

          g_dragDefIndex = ItemDatabase::GetDefIndexFromCategory(
              s_ctx->equipSlots[ep.slot].category,
              s_ctx->equipSlots[ep.slot].itemIndex);
          if (g_dragDefIndex == -1)
            g_dragDefIndex = 0; // Fallback

          g_dragQuantity = 1;
          g_dragItemLevel = s_ctx->equipSlots[ep.slot].itemLevel;
          g_isDragging = true;
        }
        return true;
      }
    }

    // Bag grid: start drag
    float gridRX = 15.0f, gridRY = 208.0f;
    float cellW = 20.0f, cellH = 20.0f, gap = 0.0f;

    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        int slot = row * 8 + col;
        float cx = gridRX + col * (cellW + gap);
        float cy = gridRY + row * (cellH + gap);
        if (relX >= cx && relX < cx + cellW && relY >= cy &&
            relY < cy + cellH) {
          if (s_ctx->inventory[slot].occupied) {
            // Find primary slot if this is a secondary one
            int primarySlot = slot;
            if (!s_ctx->inventory[slot].primary) {
              int16_t di = s_ctx->inventory[slot].defIndex;
              bool found = false;
              for (int r = 0; r <= row && !found; r++) {
                for (int c = 0; c <= col && !found; c++) {
                  int s = r * 8 + c;
                  if (s_ctx->inventory[s].occupied &&
                      s_ctx->inventory[s].primary &&
                      s_ctx->inventory[s].defIndex == di) {
                    auto it = g_itemDefs.find(di);
                    if (it != g_itemDefs.end()) {
                      if (r + it->second.height > row &&
                          c + it->second.width > col) {
                        primarySlot = s;
                        found = true;
                      }
                    }
                  }
                }
              }
            }
            g_dragFromSlot = primarySlot;
            g_dragFromEquipSlot = -1;
            g_dragDefIndex = s_ctx->inventory[primarySlot].defIndex;
            g_dragQuantity = s_ctx->inventory[primarySlot].quantity;
            g_dragItemLevel = s_ctx->inventory[primarySlot].itemLevel;
            g_isDragging = true;
            SoundManager::Play(SOUND_GET_ITEM01);
          }
          return true;
        }
      }
    }

    return true; // Consumed by panel area
  }

  // Skill Window — consume clicks so they don't fall through to click-to-move
  if (*s_ctx->showSkillWindow) {
    // Must match RenderSkillPanel layout exactly (dynamic rows)
    constexpr float SK_CELL_W = 110.0f, SK_CELL_H = 105.0f, SK_CELL_PAD = 10.0f;
    constexpr float SK_TITLE_H = 32.0f, SK_FOOTER_H = 24.0f, SK_MARGIN = 16.0f;
    constexpr int SK_COLS = 4;
    uint8_t classCode = s_ctx->hero ? s_ctx->hero->GetClass() : 16;
    int skillCount = 0;
    GetClassSkills(classCode, skillCount);
    int SK_ROWS = (skillCount + SK_COLS - 1) / SK_COLS;
    float spw =
        SK_MARGIN * 2 + SK_COLS * SK_CELL_W + (SK_COLS - 1) * SK_CELL_PAD;
    float sph = SK_TITLE_H + SK_MARGIN + SK_ROWS * SK_CELL_H +
                (SK_ROWS - 1) * SK_CELL_PAD + SK_FOOTER_H + SK_MARGIN;
    float spx = (UICoords::VIRTUAL_W - spw) * 0.5f;
    float spy = (UICoords::VIRTUAL_H - sph) * 0.5f;
    if (vx >= spx && vx < spx + spw && vy >= spy && vy < spy + sph)
      return true;
  }

  // Quickbar (HUD area) - bottom center
  int winW = (int)ImGui::GetIO().DisplaySize.x;
  int winH = (int)ImGui::GetIO().DisplaySize.y;
  // Use shared Diablo-style HUD layout constants
  using namespace HudLayout;

  if (vy >= ROW_VY) {
    // Potion slots (Q, W, E, R)
    for (int i = 0; i < 4; i++) {
      float x0 = PotionSlotX(i);
      if (vx >= x0 && vx <= x0 + SLOT && s_ctx->potionBar[i] != -1) {
        g_isDragging = true;
        g_dragFromPotionSlot = i;
        g_dragDefIndex = s_ctx->potionBar[i];
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        SoundManager::Play(SOUND_INTERFACE01);
        return true;
      }
    }

    // Skill slots (1-4)
    for (int i = 0; i < 4; i++) {
      float x0 = SkillSlotX(i);
      if (vx >= x0 && vx <= x0 + SLOT && s_ctx->skillBar[i] != -1) {
        // Block rearrangement while a skill slot is activated (1-4 toggle)
        if (InputHandler::GetActiveQuickSlot() >= 0) {
          ShowNotification("Deactivate skill first! (press 1-4)");
          SoundManager::Play(SOUND_ERROR01);
          return true;
        }
        g_isDragging = true;
        g_dragFromSkillSlot = i;
        g_dragDefIndex = -(int)s_ctx->skillBar[i]; // Negative = skill ID
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        SoundManager::Play(SOUND_INTERFACE01);
        return true;
      }
    }

    // RMC Slot
    float rmcX = RmcSlotX();
    if (vx >= rmcX && vx <= rmcX + SLOT) {
      if (s_ctx->rmcSkillId && *s_ctx->rmcSkillId >= 0) {
        g_isDragging = true;
        g_dragFromRmcSlot = true;
        g_dragDefIndex = -(*s_ctx->rmcSkillId); // Negative = skill ID
        g_dragFromSlot = -1;
        g_dragFromEquipSlot = -1;
        SoundManager::Play(SOUND_INTERFACE01);
        return true;
      }
    }
  }

  // Menu buttons: C, I, S, T, M, L (screen-pixel, bottom-right corner)
  // Convert virtual coords to screen pixels for hit testing
  {
    float scrW = (float)winW, scrH = (float)winH;
    float bs = MBTN_SCREEN_BTN;
    float bStartX = scrW - MBTN_SCREEN_RIGHT_PAD - MBTN_SCREEN_TOTAL_W;
    float bStartY =
        scrH - XP_SCREEN_BOTTOM - XP_SCREEN_H - MBTN_SCREEN_BOTTOM_PAD - bs;
    // Convert virtual click coords to screen pixels
    ImVec2 mp = ImGui::GetIO().MousePos;
    float clickX = mp.x, clickY = mp.y;
    for (int i = 0; i < MBTN_COUNT; i++) {
      float bx = bStartX + i * (bs + MBTN_SCREEN_GAP);
      float by = bStartY;
      if (clickX >= bx && clickX <= bx + bs && clickY >= by &&
          clickY <= by + bs) {
        if (i == 0) {
          *s_ctx->showCharInfo = !*s_ctx->showCharInfo;
          SoundManager::Play(SOUND_INTERFACE01);
        } else if (i == 1) {
          *s_ctx->showInventory = !*s_ctx->showInventory;
          SoundManager::Play(SOUND_INTERFACE01);
        } else if (i == 2) {
          *s_ctx->showSkillWindow = !*s_ctx->showSkillWindow;
          SoundManager::Play(SOUND_INTERFACE01);
        } else if (i == 3 && s_ctx->teleportingToTown &&
                   !*s_ctx->teleportingToTown) {
          if (s_ctx->hero && s_ctx->hero->IsDead()) {
            // Can't teleport when dead
          } else if (s_ctx->hero && s_ctx->hero->IsInSafeZone()) {
            ShowNotification("Already in town!");
            SoundManager::Play(SOUND_ERROR01);
          } else {
            *s_ctx->teleportingToTown = true;
            *s_ctx->teleportTimer = s_ctx->teleportCastTime;
            SoundManager::Play(SOUND_SUMMON);
          }
        } else if (i == 4) {
          *s_ctx->showQuestLog = !*s_ctx->showQuestLog;
          SoundManager::Play(SOUND_INTERFACE01);
        }
        return true;
      }
    }
  }

  return false;
}

// Map skill orb itemIndex → skillId (matches server orbSkillMap)
// Returns {skillId, requiredClass} where requiredClass: 16=DK, 32=Elf, 0=any
static std::pair<uint8_t, uint8_t> OrbIndexToSkillInfo(uint8_t orbIndex) {
  static const struct {
    uint8_t orbIdx;
    uint8_t skillId;
    uint8_t classReq; // 16=DK, 32=Elf
  } map[] = {
      // DK orbs
      {20, 19, 16}, {21, 20, 16}, {22, 21, 16}, {23, 22, 16},
      {24, 23, 16}, {7, 41, 16},  {12, 42, 16}, {19, 43, 16},
      // Elf orbs
      {8, 26, 32},  {9, 27, 32},  {10, 28, 32}, {11, 30, 32},
      {25, 31, 32}, {26, 32, 32}, {27, 33, 32}, {28, 34, 32}, {29, 35, 32},
  };
  for (auto &m : map)
    if (m.orbIdx == orbIndex)
      return {m.skillId, m.classReq};
  return {0, 0};
}

// Legacy wrapper for scrolls (still needed below)
static uint8_t OrbIndexToSkillId(uint8_t orbIndex) {
  return OrbIndexToSkillInfo(orbIndex).first;
}

// Map scroll itemIndex → skillId (matches server scrollSkillMap)
static uint8_t ScrollIndexToSkillId(uint8_t scrollIndex) {
  static const struct {
    uint8_t scrollIdx;
    uint8_t skillId;
  } map[] = {
      {0, 1},   // Scroll of Poison
      {1, 2},   // Scroll of Meteorite
      {2, 3},   // Scroll of Lightning
      {3, 4},   // Scroll of Fire Ball
      {4, 5},   // Scroll of Flame
      {5, 6},   // Scroll of Teleport
      {6, 7},   // Scroll of Ice
      {7, 8},   // Scroll of Twister
      {8, 9},   // Scroll of Evil Spirit
      {9, 10},  // Scroll of Hellfire
      {11, 12}, // Scroll of Aqua Beam
      {12, 13}, // Scroll of Cometfall
      {13, 14}, // Scroll of Inferno
  };
  for (auto &m : map)
    if (m.scrollIdx == scrollIndex)
      return m.skillId;
  return 0;
}

bool HandlePanelRightClick(float vx, float vy) {
  // Shop Panel - inform user drag-drop is required for buy
  if (*s_ctx->shopOpen && IsPointInPanel(vx, vy, GetShopPanelX())) {
    ShowNotification("Use drag-and-drop to purchase!");
    return true;
  }

  // Inventory Panel
  if ((*s_ctx->showInventory || *s_ctx->shopOpen) &&
      IsPointInPanel(vx, vy, GetInventoryPanelX())) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;
    float gridRX = 15.0f, gridRY = 208.0f, cellW = 20.0f, cellH = 20.0f;

    if (relX >= gridRX && relX < gridRX + 8 * cellW && relY >= gridRY &&
        relY < gridRY + 8 * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
      int targetSlot = row * 8 + col;

      if (targetSlot >= 0 && targetSlot < 64 &&
          s_ctx->inventory[targetSlot].occupied) {
        int primarySlot = targetSlot;
        if (!s_ctx->inventory[targetSlot].primary) {
          int16_t di = s_ctx->inventory[targetSlot].defIndex;
          for (int r = 0; r <= row; ++r) {
            for (int c = 0; c <= col; ++c) {
              int s = r * 8 + c;
              if (s_ctx->inventory[s].occupied && s_ctx->inventory[s].primary &&
                  s_ctx->inventory[s].defIndex == di) {
                primarySlot = s;
              }
            }
          }
        }

        auto &item = s_ctx->inventory[primarySlot];
        uint8_t cat = (uint8_t)(item.defIndex / 32);
        uint8_t itemIdx = (uint8_t)(item.defIndex % 32);

        // Skill orb (category 12): right-click to USE (learn skill)
        if (!*s_ctx->shopOpen && cat == 12 && !*s_ctx->isLearningSkill) {
          auto [skillId, classReq] = OrbIndexToSkillInfo(itemIdx);
          if (skillId > 0) {
            uint8_t playerClass = s_ctx->hero->GetClass();
            if (playerClass != classReq) {
              ShowNotification((classReq == 16) ? "Only Dark Knight can use this orb!"
                                               : "Only Elf can use this orb!");
              SoundManager::Play(SOUND_ERROR01);
              return true;
            }
            if (s_ctx->learnedSkills) {
              for (auto s : *s_ctx->learnedSkills)
                if (s == skillId) {
                  ShowNotification("Skill already learned!");
                  SoundManager::Play(SOUND_ERROR01);
                  return true;
                }
            }
            // Check level and energy requirements before sending to server
            auto &orbDefs = ItemDatabase::GetItemDefs();
            auto orbIt = orbDefs.find(item.defIndex);
            if (orbIt != orbDefs.end()) {
              if (s_ctx->serverLevel && *s_ctx->serverLevel < (int)orbIt->second.levelReq) {
                ShowNotification("Level too low!");
                SoundManager::Play(SOUND_ERROR01);
                return true;
              }
              if (s_ctx->serverEne && *s_ctx->serverEne < (int)orbIt->second.reqEne) {
                ShowNotification("Not enough Energy!");
                SoundManager::Play(SOUND_ERROR01);
                return true;
              }
            }
            s_ctx->server->SendItemUse((uint8_t)primarySlot);
            *s_ctx->isLearningSkill = true;
            *s_ctx->learnSkillTimer = 0.0f;
            *s_ctx->learningSkillId = skillId;
            std::cout << "[Skill] Learning skill " << (int)skillId
                      << " from orb idx=" << (int)itemIdx << std::endl;
            return true;
          }
        }

        // DW Scroll (category 15): right-click to USE (learn spell) — DW only
        if (!*s_ctx->shopOpen && cat == 15 && !*s_ctx->isLearningSkill) {
          uint8_t skillId = ScrollIndexToSkillId(itemIdx);
          if (skillId > 0) {
            if (s_ctx->hero->GetClass() != 0) {
              ShowNotification("Only Dark Wizard can use spell scrolls!");
              SoundManager::Play(SOUND_ERROR01);
              return true;
            }
            if (s_ctx->learnedSkills) {
              for (auto s : *s_ctx->learnedSkills)
                if (s == skillId) {
                  ShowNotification("Spell already learned!");
                  SoundManager::Play(SOUND_ERROR01);
                  return true;
                }
            }
            // Check level and energy requirements before sending to server
            auto &scrollDefs = ItemDatabase::GetItemDefs();
            auto scrollIt = scrollDefs.find(item.defIndex);
            if (scrollIt != scrollDefs.end()) {
              if (s_ctx->serverLevel && *s_ctx->serverLevel < (int)scrollIt->second.levelReq) {
                ShowNotification("Level too low!");
                SoundManager::Play(SOUND_ERROR01);
                return true;
              }
              if (s_ctx->serverEne && *s_ctx->serverEne < (int)scrollIt->second.reqEne) {
                ShowNotification("Not enough Energy!");
                SoundManager::Play(SOUND_ERROR01);
                return true;
              }
            }
            s_ctx->server->SendItemUse((uint8_t)primarySlot);
            *s_ctx->isLearningSkill = true;
            *s_ctx->learnSkillTimer = 0.0f;
            *s_ctx->learningSkillId = skillId;
            std::cout << "[Spell] Learning spell " << (int)skillId
                      << " from scroll idx=" << (int)itemIdx << std::endl;
            return true;
          }
        }

        // Shop open: right-click to SELL
        if (*s_ctx->shopOpen) {
          s_ctx->server->SendShopSell(primarySlot);
        }
      }
      return true;
    }
  }

  return false;
}

void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy) {
  auto &g_itemDefs = ItemDatabase::GetItemDefs();

  if (!g_isDragging)
    return;
  bool wasDragging = g_isDragging;
  g_isDragging = false;

  int winW, winH;
  glfwGetWindowSize(window, &winW, &winH);
  bool droppedOnHUD =
      (vy >= HudLayout::ROW_VY); // HUD row VY threshold (uses shared constant)

  if (g_dragFromPotionSlot != -1) {
    if (!droppedOnHUD) {
      s_ctx->potionBar[g_dragFromPotionSlot] = -1;
      std::cout << "[QuickSlot] Cleared potion slot " << g_dragFromPotionSlot
                << std::endl;
    }
    g_dragFromPotionSlot = -1;
    return;
  }

  if (g_dragFromSkillSlot != -1) {
    if (!droppedOnHUD) {
      s_ctx->skillBar[g_dragFromSkillSlot] = -1;
      std::cout << "[QuickSlot] Cleared skill slot " << g_dragFromSkillSlot
                << std::endl;
    }
    g_dragFromSkillSlot = -1;
    return;
  }

  // Skill drag (negative dragDefIndex = skill ID)
  if (g_dragDefIndex < 0) {
    uint8_t skillId = (uint8_t)(-g_dragDefIndex);
    if (g_dragFromRmcSlot) {
      // Dragged FROM RMC slot — clear if dropped outside HUD
      if (!droppedOnHUD) {
        *s_ctx->rmcSkillId = -1;
        std::cout << "[RMC] Cleared RMC slot (dragged out)" << std::endl;
      }
      g_dragFromRmcSlot = false;
    } else if (droppedOnHUD) {
      // Check if dropped on a specific skill slot (1-4) first
      static constexpr float SK_SLOT = 44.0f, SK_GAP = 5.0f, SK_BAR_W = 140.0f;
      static constexpr float SK_CW =
          SK_BAR_W + SK_GAP * 2 + (SK_SLOT + SK_GAP) * 4 + SK_GAP +
          (SK_SLOT + SK_GAP) * 4 + SK_GAP + SK_SLOT + SK_GAP * 2 + SK_BAR_W;
      static constexpr float SK_START = (1280.0f - SK_CW) * 0.5f;
      float potStart = SK_START + SK_BAR_W + SK_GAP * 2;
      float skillStart = potStart + 4 * (SK_SLOT + SK_GAP) + SK_GAP;
      bool assignedToSlot = false;
      for (int i = 0; i < 4; i++) {
        float x0 = skillStart + i * (SK_SLOT + SK_GAP);
        if (vx >= x0 && vx <= x0 + SK_SLOT) {
          s_ctx->skillBar[i] = (int8_t)skillId;
          SoundManager::Play(SOUND_GET_ITEM01);
          std::cout << "[Skill] Assigned skill " << (int)skillId << " to slot "
                    << i << std::endl;
          assignedToSlot = true;
          break;
        }
      }
      if (!assignedToSlot) {
        // Dropped on HUD but not on a skill slot → assign to RMC
        *s_ctx->rmcSkillId = (int8_t)skillId;
        SoundManager::Play(SOUND_INTERFACE01);
        std::cout << "[RMC] Assigned skill " << (int)skillId << " to RMC slot"
                  << std::endl;
      }
    }
    // Dropped elsewhere = cancelled (no action)
    g_dragFromSlot = -1;
    g_dragFromEquipSlot = -1;
    g_dragFromShopSlot = -1;
    return;
  }

  if (*s_ctx->showInventory || *s_ctx->shopOpen) {
    float px = GetInventoryPanelX(), py = PANEL_Y;
    float relX = (vx - px) / g_uiPanelScale;
    float relY = (vy - py) / g_uiPanelScale;

    // 1. Check drop on Equipment slots
    for (const auto &ep : g_equipLayoutRects) {
      if (relX >= ep.x && relX < ep.x + ep.w && relY >= ep.y &&
          relY < ep.y + ep.h) {
        // Dragging FROM Inventory TO Equipment
        if (g_dragFromSlot >= 0) {
          if (!CanEquipItem(g_dragDefIndex)) {
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }
        }

        // Dragging FROM Shop TO Equipment — just buy to bag
        else if (g_dragFromShopSlot >= 0 && *s_ctx->shopOpen) {
          if (*s_ctx->syncDone) {
            if (s_shopGrid[g_dragFromShopSlot].buyPrice > *s_ctx->zen) {
              ShowNotification("Not enough Zen!");
              SoundManager::Play(SOUND_ERROR01);
            } else {
              {
              int16_t buyDef2 = s_shopGrid[g_dragFromShopSlot].defIndex;
              uint8_t buyCat2 = buyDef2 / 32, buyIdx2 = buyDef2 % 32;
              bool isAmmo2 = (buyCat2 == 4 && (buyIdx2 == 7 || buyIdx2 == 15));
              bool isPotion2 = (buyCat2 == 14);
              uint8_t buyQty2 = (isAmmo2 || isPotion2) ? 20 : 1;
              s_ctx->server->SendShopBuy(buyDef2,
                  s_shopGrid[g_dragFromShopSlot].itemLevel, buyQty2);
              }
              std::cout << "[Shop] Bought item to bag via drag to Equip"
                        << (int)ep.slot << std::endl;
              ShowNotification("Purchased to bag. Equip manually.");
            }
          }
          return;
        }

        if (g_dragFromSlot >= 0) {
          uint8_t cat, idx;
          ItemDatabase::GetItemCategoryAndIndex(g_dragDefIndex, cat, idx);

          // Enforce Strict Slot Category Compatibility (Main 5.2 logic)
          bool validSlot = false;
          switch (ep.slot) {
          case 0: // R.Hand
            validSlot = (cat <= 5);
            break;
          case 1: // L.Hand
            validSlot = (cat <= 6);
            break;
          case 2:
            validSlot = (cat == 7);
            break; // Helm
          case 3:
            validSlot = (cat == 8);
            break; // Armor
          case 4:
            validSlot = (cat == 9);
            break; // Pants
          case 5:
            validSlot = (cat == 10);
            break; // Gloves
          case 6:
            validSlot = (cat == 11);
            break; // Boots
          case 7:
            validSlot = (cat == 12 && idx <= 6);
            break; // Wings
          case 8:
            validSlot = (cat == 13 && (idx == 0 || idx == 1)); // Pets only
            if (cat == 13 && (idx == 2 || idx == 3)) {
              ShowNotification("Use mount in Q/W/E/R quickslots!");
              SoundManager::Play(SOUND_ERROR01);
            }
            break;
          case 9:
            validSlot = (cat == 13 && idx >= 8 && idx <= 13);
            break; // Pendant
          case 10:
          case 11:
            validSlot = (cat == 13 && idx >= 20 && idx <= 25);
            break; // Rings
          }

          if (!validSlot) {
            std::cout << "[UI] Cannot equip category " << (int)cat
                      << " in slot " << ep.slot << std::endl;
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          // Enforce Hand Compatibility (Main 5.2 rules)
          if (ep.slot == 0 && cat == 6) {
            std::cout << "[UI] Cannot equip Shield in Right Hand!" << std::endl;
            g_isDragging = false;
            g_dragFromSlot = -1;
            return;
          }

          // L.Hand specific checks
          if (ep.slot == 1) {
            // Ammo (arrows/bolts) is always allowed alongside bows
            bool isAmmo = (cat == 4 && (idx == 7 || idx == 15));

            // Check if R.Hand has a 2H weapon (exempt ammo — bows need arrows)
            bool has2H = false;
            if (!isAmmo && s_ctx->equipSlots[0].equipped) {
              int16_t s0Idx = ItemDatabase::GetDefIndexFromCategory(
                  s_ctx->equipSlots[0].category,
                  s_ctx->equipSlots[0].itemIndex);
              auto s0It = ItemDatabase::GetItemDefs().find(s0Idx);
              if (s0It != ItemDatabase::GetItemDefs().end() &&
                  s0It->second.twoHanded) {
                has2H = true;
              }
            }
            if (has2H) {
              ShowNotification("Blocked: 2-handed weapon equipped!");
              SoundManager::Play(SOUND_ERROR01);
              g_isDragging = false;
              g_dragFromSlot = -1;
              return;
            }

            // Dual-wield/Shield logic (exempt ammo)
            if (cat <= 5 && !isAmmo) { // Weapon (not ammo)
              uint8_t baseClass = s_ctx->hero->GetClass() >> 4;
              bool canDualWield =
                  (baseClass == 1 || baseClass == 3); // 1=DK, 3=MG
              auto defIt = g_itemDefs.find(g_dragDefIndex);
              bool isTwoHanded =
                  (defIt != g_itemDefs.end() && defIt->second.twoHanded);
              if (!canDualWield) {
                ShowNotification("Only Dark Knights can dual-wield!");
                SoundManager::Play(SOUND_ERROR01);
                g_isDragging = false;
                g_dragFromSlot = -1;
                return;
              }
              if (isTwoHanded) {
                ShowNotification("Two-handed weapons can't go in left hand!");
                SoundManager::Play(SOUND_ERROR01);
                g_isDragging = false;
                g_dragFromSlot = -1;
                return;
              }
            }
          }

          // Prepare logic for swap if equipped
          ClientInventoryItem swapItem = {};
          if (s_ctx->equipSlots[ep.slot].equipped) {
            swapItem.defIndex = ItemDatabase::GetDefIndexFromCategory(
                s_ctx->equipSlots[ep.slot].category,
                s_ctx->equipSlots[ep.slot].itemIndex);
            swapItem.quantity = 1;
            swapItem.itemLevel = s_ctx->equipSlots[ep.slot].itemLevel;
            swapItem.occupied = true;
          }

          // Equip the new item
          s_ctx->equipSlots[ep.slot].category = cat;
          s_ctx->equipSlots[ep.slot].itemIndex = idx;
          s_ctx->equipSlots[ep.slot].itemLevel = g_dragItemLevel;
          s_ctx->equipSlots[ep.slot].equipped = true;
          s_ctx->equipSlots[ep.slot].modelFile =
              ItemDatabase::GetDropModelName(g_dragDefIndex);

          auto defIt = g_itemDefs.find(g_dragDefIndex);

          // Handle conflict: 2H weapon in R.Hand forces L.Hand uneq
          if (ep.slot == 0 && defIt != g_itemDefs.end() &&
              defIt->second.twoHanded) {
            if (s_ctx->equipSlots[1].equipped) {
              std::cout << "[UI] 2H weapon equipped: auto-unequipping left hand"
                        << std::endl;
              s_ctx->server->SendUnequip(*s_ctx->heroCharacterId, 1);
              s_ctx->equipSlots[1].equipped = false;
              s_ctx->equipSlots[1].category = 0xFF;
              WeaponEquipInfo emptyInfo;
              s_ctx->hero->EquipShield(emptyInfo);
            }
          }

          // Update Hero Visuals Immediately
          WeaponEquipInfo info;
          info.category = cat;
          info.itemIndex = idx;
          info.itemLevel = g_dragItemLevel;
          info.modelFile = s_ctx->equipSlots[ep.slot].modelFile;
          if (defIt != g_itemDefs.end())
            info.twoHanded = defIt->second.twoHanded;

          if (ep.slot == 0)
            s_ctx->hero->EquipWeapon(info);
          if (ep.slot == 1)
            s_ctx->hero->EquipShield(info);
          if (ep.slot == 8 && cat == 13 && (idx == 0 || idx == 1))
            s_ctx->hero->EquipPet(idx);
          if (ep.slot == 8 && cat == 13 && (idx == 2 || idx == 3))
            s_ctx->hero->EquipMount(idx);

          // Body part equipment (Helm/Armor/Pants/Gloves/Boots)
          int bodyPart = ItemDatabase::GetBodyPartIndex(cat);
          if (bodyPart >= 0) {
            std::string partModel =
                ItemDatabase::GetBodyPartModelFile(cat, idx);
            if (!partModel.empty())
              s_ctx->hero->EquipBodyPart(bodyPart, partModel, g_dragItemLevel,
                                         idx);
          }

          // Send Equip packet
          if (*s_ctx->syncDone) {
            s_ctx->server->SendEquip(*s_ctx->heroCharacterId,
                                     static_cast<uint8_t>(ep.slot), cat, idx,
                                     g_dragItemLevel);
            SoundManager::Play(SOUND_GET_ITEM01);
          }

          // Clear source bag slot, place swapped item if any
          ClearBagItem(g_dragFromSlot);
          if (swapItem.occupied && swapItem.defIndex >= 0) {
            SetBagItem(g_dragFromSlot, swapItem.defIndex, swapItem.quantity,
                       swapItem.itemLevel);
          }
          std::cout << "[UI] Equipped item from Inv " << g_dragFromSlot
                    << " to Equip " << ep.slot << std::endl;
          RecalcEquipmentStats();
        }
        // Dragging FROM Equipment TO Equipment (e.g. swap rings) - TODO if
        // needed
        return;
      }
    }

    // 2. Check drop on Quickbar Slots (bottom bar)
    // Use shared Diablo-style HUD layout constants
    using namespace HudLayout;

    if (vy >= ROW_VY) {
      // Potion slots (Q, W, E, R)
      for (int i = 0; i < 4; i++) {
        float x0 = PotionSlotX(i);
        if (vx >= x0 && vx <= x0 + SLOT) {
          if (g_dragDefIndex >= 0) {
            auto it = g_itemDefs.find(g_dragDefIndex);
            if (it != g_itemDefs.end() &&
                (it->second.category == 14 ||
                 (it->second.category == 13 &&
                  (it->second.itemIndex == 2 || it->second.itemIndex == 3)))) {
              s_ctx->potionBar[i] = g_dragDefIndex;
              SoundManager::Play(SOUND_GET_ITEM01);
              return;
            }
          }
        }
      }

      // Skill slots (1-4)
      for (int i = 0; i < 4; i++) {
        float x0 = SkillSlotX(i);
        if (vx >= x0 && vx <= x0 + SLOT) {
          if (g_dragDefIndex < 0) {
            uint8_t skillId = (uint8_t)(-g_dragDefIndex);
            s_ctx->skillBar[i] = (int8_t)skillId;
            SoundManager::Play(SOUND_GET_ITEM01);
            return;
          }
        }
      }

      // RMC Slot
      float rmcX = RmcSlotX();
      if (vx >= rmcX && vx <= rmcX + SLOT) {
        if (g_dragDefIndex < 0) {
          uint8_t skillId = (uint8_t)(-g_dragDefIndex);
          *s_ctx->rmcSkillId = (int8_t)skillId;
          SoundManager::Play(SOUND_GET_ITEM01);
          return;
        }
      }
    }

    // 3. Check drop on Bag grid
    float gridRX = 15.0f, gridRY = 208.0f;
    float cellW = 20.0f, cellH = 20.0f;

    if (relX >= gridRX && relX < gridRX + 8 * cellW && relY >= gridRY &&
        relY < gridRY + 8 * cellH) {
      int col = (int)((relX - gridRX) / cellW);
      int row = (int)((relY - gridRY) / cellH);
      if (col >= 0 && col < 8 && row >= 0 && row < 8) {
        int targetSlot = row * 8 + col;

        // Dragging FROM Equipment TO Inventory (Unequip)
        if (g_dragFromEquipSlot >= 0) {
          if (CheckBagFit(g_dragDefIndex, targetSlot)) {
            // Move item to inventory
            SetBagItem(targetSlot, g_dragDefIndex, g_dragQuantity,
                       g_dragItemLevel);

            // Clear equip slot
            s_ctx->equipSlots[g_dragFromEquipSlot].equipped = false;
            s_ctx->equipSlots[g_dragFromEquipSlot].category = 0xFF;

            // Update Hero Visuals Immediately
            WeaponEquipInfo info;
            if (g_dragFromEquipSlot == 0)
              s_ctx->hero->EquipWeapon(info);
            if (g_dragFromEquipSlot == 1)
              s_ctx->hero->EquipShield(info);
            if (g_dragFromEquipSlot == 8)
              s_ctx->hero->UnequipPet();

            if (g_dragFromEquipSlot >= 2 && g_dragFromEquipSlot <= 6) {
              int partIdx = g_dragFromEquipSlot - 2;
              s_ctx->hero->EquipBodyPart(partIdx, "");
            }

            // Send Unequip packet
            if (*s_ctx->syncDone)
              s_ctx->server->SendUnequip(
                  *s_ctx->heroCharacterId,
                  static_cast<uint8_t>(g_dragFromEquipSlot));

            SoundManager::Play(SOUND_GET_ITEM01);
            std::cout << "[UI] Unequipped item to Inv " << targetSlot
                      << std::endl;
            RecalcEquipmentStats();
          } else {
            std::cout << "[UI] Not enough space for unequipped item"
                      << std::endl;
          }
        }
        // Dragging FROM Shop TO Inventory (Buy via drag-and-drop)
        else if (g_dragFromShopSlot >= 0 && *s_ctx->shopOpen) {
          if (s_shopGrid[g_dragFromShopSlot].buyPrice > *s_ctx->zen) {
            ShowNotification("Not enough Zen!");
          } else if (*s_ctx->syncDone) {
            // CHECK BAG FIT FIRST
            if (CheckBagFit(s_shopGrid[g_dragFromShopSlot].defIndex,
                            targetSlot)) {
              // Arrows/bolts buy in stacks of 20
              int16_t buyDef = s_shopGrid[g_dragFromShopSlot].defIndex;
              uint8_t buyCat = buyDef / 32, buyIdx = buyDef % 32;
              bool isAmmo = (buyCat == 4 && (buyIdx == 7 || buyIdx == 15));
              bool isPotion = (buyCat == 14);
              uint8_t buyQty = (isAmmo || isPotion) ? 20 : 1;
              s_ctx->server->SendShopBuy(
                  buyDef,
                  s_shopGrid[g_dragFromShopSlot].itemLevel, buyQty, targetSlot);
              std::cout << "[Shop] Bought item via drag (def="
                        << s_shopGrid[g_dragFromShopSlot].defIndex << ")"
                        << std::endl;
            } else {
              ShowNotification("Not enough space at this location!");
              std::cout << "[UI] Shop drag-buy failed: no space at slot "
                        << targetSlot << std::endl;
            }
          }
        }
        // Dragging FROM Inventory TO Inventory (Move)
        else if (g_dragFromSlot >= 0 && g_dragFromSlot != targetSlot) {
          // Temporarily clear old area to check fit
          int16_t di = g_dragDefIndex;
          uint8_t dq = g_dragQuantity;
          uint8_t dl = g_dragItemLevel;

          ClearBagItem(g_dragFromSlot);
          if (CheckBagFit(di, targetSlot)) {
            SetBagItem(targetSlot, di, dq, dl);
            // Send move packet to server
            if (*s_ctx->syncDone)
              s_ctx->server->SendInventoryMove(
                  static_cast<uint8_t>(g_dragFromSlot),
                  static_cast<uint8_t>(targetSlot));

            SoundManager::Play(SOUND_GET_ITEM01);
            std::cout << "[UI] Moved item from " << g_dragFromSlot << " to "
                      << targetSlot << std::endl;
          } else {
            // Restore old area
            SetBagItem(g_dragFromSlot, di, dq, dl);
            std::cout << "[UI] Cannot move: target area occupied" << std::endl;
          }
        }
      }
    }

    // 4. Dragging FROM Inventory TO Shop (Sell via drag-and-drop)
    if (g_dragFromSlot >= 0 && *s_ctx->shopOpen &&
        IsPointInPanel(vx, vy, GetShopPanelX()) && *s_ctx->syncDone) {
      s_ctx->server->SendShopSell(g_dragFromSlot);
      std::cout << "[Shop] Sold item via drag from slot " << g_dragFromSlot
                << std::endl;
      g_dragFromSlot = -1;
      g_dragFromEquipSlot = -1;
      g_dragFromShopSlot = -1;
      return;
    }

    // 5. Drop item to ground — dragged from bag slot and released outside
    // panels
    if (g_dragFromSlot >= 0 && *s_ctx->syncDone) {
      bool insideInvPanel = IsPointInPanel(vx, vy, GetInventoryPanelX());
      bool insideCharPanel =
          *s_ctx->showCharInfo && IsPointInPanel(vx, vy, GetCharInfoPanelX());
      bool insideShopPanel =
          *s_ctx->shopOpen && IsPointInPanel(vx, vy, GetShopPanelX());

      if (!insideInvPanel && !insideCharPanel && !insideShopPanel &&
          !droppedOnHUD) {
        s_ctx->server->SendDropItem(static_cast<uint8_t>(g_dragFromSlot));
        SoundManager::Play(SOUND_DROP_ITEM01);
        std::cout << "[UI] Dropped item from slot " << g_dragFromSlot
                  << " to ground" << std::endl;
      }
    }
  }

  g_dragFromSlot = -1;
  g_dragFromEquipSlot = -1;
  g_dragFromShopSlot = -1;
}

bool IsDragging() { return g_isDragging; }

int16_t GetDragDefIndex() { return g_dragDefIndex; }

const std::vector<ItemRenderJob> &GetRenderQueue() { return g_renderQueue; }

void ClearRenderQueue() {
  g_renderQueue.clear();
  g_deferredOverlays.clear();
  g_deferredCooldowns.clear();
}

void AddRenderJob(const ItemRenderJob &job) { g_renderQueue.push_back(job); }

bool HasDeferredOverlays() { return !g_deferredOverlays.empty(); }

void FlushDeferredOverlays() {
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  for (const auto &ov : g_deferredOverlays) {
    // Shadow
    dl->AddText(ImVec2(ov.x + 1, ov.y + 1), IM_COL32(0, 0, 0, 220), ov.text);
    dl->AddText(ImVec2(ov.x, ov.y), ov.color, ov.text);
  }
}

bool HasDeferredCooldowns() { return !g_deferredCooldowns.empty(); }

void FlushDeferredCooldowns() {
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  for (const auto &cd : g_deferredCooldowns) {
    ImVec2 p0(cd.x, cd.y), p1(cd.x + cd.w, cd.y + cd.h);
    dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 180), 3.0f);
    ImVec2 tsz = ImGui::CalcTextSize(cd.text);
    float tx = cd.x + (cd.w - tsz.x) * 0.5f;
    float ty = cd.y + (cd.h - tsz.y) * 0.5f;
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), cd.text);
    dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 255), cd.text);
  }
}

bool HasPendingTooltip() { return g_pendingTooltip.active; }

void ResetPendingTooltip() { g_pendingTooltip.active = false; }

void LoadSlotBackgrounds(const std::string &dataPath) {
  g_texInventoryBg =
      UITexture::Load(dataPath + "/Interface/mu_inventory_bg.png");

  g_slotBackgrounds[0] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_weapon(R).OZT");
  g_slotBackgrounds[1] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_weapon(L).OZT");
  g_slotBackgrounds[2] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_cap.OZT");
  g_slotBackgrounds[3] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_upper.OZT");
  g_slotBackgrounds[4] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_lower.OZT");
  g_slotBackgrounds[5] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_gloves.OZT");
  g_slotBackgrounds[6] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_boots.OZT");
  g_slotBackgrounds[7] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_wing.OZT");
  g_slotBackgrounds[8] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_fairy.OZT");
  g_slotBackgrounds[9] = TextureLoader::Resolve(dataPath + "/Interface",
                                                "newui_item_necklace.OZT");
  g_slotBackgrounds[10] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_ring.OZT");
  g_slotBackgrounds[11] =
      TextureLoader::Resolve(dataPath + "/Interface", "newui_item_ring.OZT");

  // Skill icon sprite sheet (25 icons per row, 20x28px each on 512x512)
  g_texSkillIcons = TextureLoader::LoadOZJ(dataPath + "/Interface/Skill.OZJ");
  if (TexValid(g_texSkillIcons)) {
    printf("[UI] Loaded Skill.OZJ icon sheet (tex=%zu)\n", (size_t)TexImID(g_texSkillIcons));
  }

  // Load aura icons for pets/mounts
  const char *auraIconFiles[] = {
    "aura_guardian_angel.png", "aura_imp.png",
    "aura_uniria.png", "aura_dinorant.png",
  };
  for (int i = 0; i < 4; i++) {
    auto tex = UITexture::Load(dataPath + "/Interface/" + auraIconFiles[i]);
    g_texAuraIcons[i] = tex.id;
  }
  printf("[UI] Loaded 4 aura icons\n");
}

void UpdatePanelScale(int /*windowHeight*/) {
  g_uiPanelScale = 1.5f; // constant — UICoords handles resolution scaling
}

float GetCharInfoPanelX() { return PanelXRight(); }

float GetInventoryPanelX() {
  return *s_ctx->showCharInfo ? PanelXRight() - PanelW() - 5.0f : PanelXRight();
}

float GetShopPanelX() { return GetInventoryPanelX() - PanelW() - 5.0f; }

bool IsPointInPanel(float vx, float vy, float panelX) {
  return vx >= panelX && vx < panelX + PanelW() && vy >= PANEL_Y &&
         vy < PANEL_Y + PanelH();
}

} // namespace InventoryUI
