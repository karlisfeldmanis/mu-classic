#include "ClientPacketHandler.hpp"
#include "HeroCharacter.hpp"
#include "InputHandler.hpp"
#include "InventoryUI_Internal.hpp"
#include "ItemDatabase.hpp"
#include "SoundManager.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

// ─── File-local helpers ─────────────────────────────────────────────────────

// Look up the level requirement for a skill from its teaching orb/scroll item
static uint16_t GetSkillLevelReq(uint8_t skillId) {
  // DK orbs + Elf orbs (category 12)
  static const uint8_t orbSkillToItem[][2] = {
      {19, 20}, {20, 21}, {21, 22}, {22, 23}, {23, 24},
      {41, 7},  {42, 12}, {43, 19},
      {26, 8},  {27, 9},  {28, 10}, {30, 11},
      {31, 25}, {32, 26}, {33, 27}, {34, 28}, {35, 29},
  };
  for (auto &m : orbSkillToItem) {
    if (m[0] == skillId) {
      int defIndex = 12 * 32 + m[1];
      auto &defs = ItemDatabase::GetItemDefs();
      auto it = defs.find(defIndex);
      return (it != defs.end()) ? it->second.levelReq : 0;
    }
  }
  // DW scrolls (category 15)
  static const uint8_t scrollSkillToItem[][2] = {
      {1, 0},  {2, 1},  {3, 2},  {4, 3},   {5, 4},  {6, 5}, {7, 6},
      {8, 7},  {9, 8},  {10, 9}, {11, 10}, {12, 11}, {13, 12}, {14, 13},
  };
  for (auto &m : scrollSkillToItem) {
    if (m[0] == skillId) {
      int defIndex = 15 * 32 + m[1];
      auto &defs = ItemDatabase::GetItemDefs();
      auto it = defs.find(defIndex);
      return (it != defs.end()) ? it->second.levelReq : 0;
    }
  }
  return 0;
}

static void RenderBar(ImDrawList *dl, float x, float y, float w, float h,
                      float frac, ImU32 fillColor, ImU32 bgColor,
                      const char *label) {
  frac = std::clamp(frac, 0.0f, 1.0f);
  ImVec2 p0(x, y), p1(x + w, y + h);
  dl->AddRectFilled(p0, p1, bgColor, 3.0f);
  if (frac > 0.01f)
    dl->AddRectFilled(p0, ImVec2(x + w * frac, y + h), fillColor, 3.0f);
  dl->AddRect(p0, p1, IM_COL32(60, 60, 80, 200), 3.0f);
  // Centered text with shadow
  ImVec2 tsz = ImGui::CalcTextSize(label);
  float tx = x + (w - tsz.x) * 0.5f;
  float ty = y + (h - tsz.y) * 0.5f;
  dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), label);
  dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 230), label);
}

// Helper: render a skill icon into a rect
static void RenderSkillIcon(ImDrawList *dl, int8_t skillId, float sx, float sy,
                            float sz,
                            ImU32 tint = IM_COL32(255, 255, 255, 255)) {
  if (skillId >= 0 && TexValid(g_texSkillIcons)) {
    int ic = skillId % SKILL_ICON_COLS;
    int ir = skillId / SKILL_ICON_COLS;
    static constexpr float UV_INSET = 0.1f / SKILL_TEX_SIZE;
    float u0 = (SKILL_ICON_W * ic) / SKILL_TEX_SIZE + UV_INSET;
    float v0 = (SKILL_ICON_H * ir) / SKILL_TEX_SIZE + UV_INSET;
    float u1 = (SKILL_ICON_W * (ic + 1)) / SKILL_TEX_SIZE - UV_INSET;
    float v1 = (SKILL_ICON_H * (ir + 1)) / SKILL_TEX_SIZE - UV_INSET;
    float pad = 4.0f;
    dl->AddImage((ImTextureID)TexImID(g_texSkillIcons),
                 ImVec2(sx + pad, sy + pad),
                 ImVec2(sx + sz - pad, sy + sz - pad), ImVec2(u0, v0),
                 ImVec2(u1, v1), tint);
  }
}

static const char *GetSkillName(uint8_t skillId) {
  for (int i = 0; i < NUM_DK_SKILLS; i++)
    if (g_dkSkills[i].skillId == skillId)
      return g_dkSkills[i].name;
  for (int i = 0; i < NUM_DW_SPELLS; i++)
    if (g_dwSpells[i].skillId == skillId)
      return g_dwSpells[i].name;
  for (int i = 0; i < NUM_ELF_SKILLS; i++)
    if (g_elfSkills[i].skillId == skillId)
      return g_elfSkills[i].name;
  return nullptr;
}

namespace InventoryUI {

void RenderSkillDragCursor(ImDrawList *dl) {
  if (!g_isDragging || g_dragDefIndex >= 0)
    return;

  uint8_t skillId = (uint8_t)(-g_dragDefIndex);
  ImVec2 mp = ImGui::GetIO().MousePos;
  float dw = 40.0f, dh = 56.0f; // 20:28 ratio scaled up
  ImVec2 iMin(mp.x - dw * 0.5f, mp.y - dh * 0.5f);
  ImVec2 iMax(iMin.x + dw, iMin.y + dh);

  dl->AddRectFilled(iMin, iMax, IM_COL32(30, 30, 50, 180), 3.0f);
  if (TexValid(g_texSkillIcons)) {
    int ic = skillId % SKILL_ICON_COLS;
    int ir = skillId / SKILL_ICON_COLS;
    float uvIn = 0.1f / SKILL_TEX_SIZE;
    float u0 = (SKILL_ICON_W * ic) / SKILL_TEX_SIZE + uvIn;
    float v0 = (SKILL_ICON_H * ir) / SKILL_TEX_SIZE + uvIn;
    float u1 = (SKILL_ICON_W * (ic + 1)) / SKILL_TEX_SIZE - uvIn;
    float v1 = (SKILL_ICON_H * (ir + 1)) / SKILL_TEX_SIZE - uvIn;
    dl->AddImage((ImTextureID)TexImID(g_texSkillIcons), iMin, iMax,
                 ImVec2(u0, v0), ImVec2(u1, v1));
  }
  // Look up skill name from both DK and DW tables
  const char *skillName = nullptr;
  for (int i = 0; i < NUM_DK_SKILLS; i++) {
    if (g_dkSkills[i].skillId == skillId) {
      skillName = g_dkSkills[i].name;
      break;
    }
  }
  if (!skillName) {
    for (int i = 0; i < NUM_DW_SPELLS; i++) {
      if (g_dwSpells[i].skillId == skillId) {
        skillName = g_dwSpells[i].name;
        break;
      }
    }
  }
  if (!skillName) {
    for (int i = 0; i < NUM_ELF_SKILLS; i++) {
      if (g_elfSkills[i].skillId == skillId) {
        skillName = g_elfSkills[i].name;
        break;
      }
    }
  }
  if (skillName) {
    ImVec2 nsz = ImGui::CalcTextSize(skillName);
    dl->AddText(ImVec2(iMin.x + dw * 0.5f - nsz.x * 0.5f, iMax.y + 2),
                IM_COL32(255, 210, 80, 255), skillName);
  }
}

// ═══════════════════════════════════════════════════════════════════
// RMC (Right Mouse Click) Skill Slot -- HUD element
// ═══════════════════════════════════════════════════════════════════

void RenderRmcSlot(ImDrawList *dl, float screenX, float screenY, float size) {
  if (!s_ctx->rmcSkillId)
    return;
  int8_t skillId = *s_ctx->rmcSkillId;

  // Check if player can afford the resource cost (AG for DK, Mana for DW)
  int cost = (skillId > 0) ? GetSkillResourceCost(skillId) : 0;
  bool isDK = s_ctx->hero && s_ctx->hero->GetClass() == 16;
  int currentResource = isDK ? (s_ctx->serverAG ? *s_ctx->serverAG : 0)
                             : (s_ctx->serverMP ? *s_ctx->serverMP : 0);
  bool canAfford = currentResource >= cost;
  ImU32 tint =
      canAfford ? IM_COL32(255, 255, 255, 255) : IM_COL32(100, 100, 100, 180);

  ImVec2 p0(screenX, screenY);
  ImVec2 p1(screenX + size, screenY + size);
  ImVec2 mpos = ImGui::GetIO().MousePos;
  bool hov = mpos.x >= p0.x && mpos.x < p1.x && mpos.y >= p0.y && mpos.y < p1.y;
  DrawStyledSlot(dl, p0, p1, hov);

  if (skillId >= 0 && TexValid(g_texSkillIcons)) {
    int ic = skillId % SKILL_ICON_COLS;
    int ir = skillId / SKILL_ICON_COLS;
    static constexpr float UV_INSET = 0.1f / SKILL_TEX_SIZE;
    float u0 = (SKILL_ICON_W * ic) / SKILL_TEX_SIZE + UV_INSET;
    float v0 = (SKILL_ICON_H * ir) / SKILL_TEX_SIZE + UV_INSET;
    float u1 = (SKILL_ICON_W * (ic + 1)) / SKILL_TEX_SIZE - UV_INSET;
    float v1 = (SKILL_ICON_H * (ir + 1)) / SKILL_TEX_SIZE - UV_INSET;

    float pad = 4.0f;
    ImVec2 iMin(screenX + pad, screenY + pad);
    ImVec2 iMax(screenX + size - pad, screenY + size - pad);
    dl->AddImage((ImTextureID)TexImID(g_texSkillIcons), iMin, iMax,
                 ImVec2(u0, v0), ImVec2(u1, v1), tint);
  }

  if (skillId > 0 && !canAfford)
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 120), 3.0f);

  // "RMC" label
  dl->AddText(ImVec2(screenX + 2, screenY + 1), IM_COL32(255, 255, 255, 180),
              "RMC");

  // RMC slot tooltip
  if (hov && skillId > 0) {
    uint8_t classCode = s_ctx->hero ? s_ctx->hero->GetClass() : 16;
    bool isDKClass3 = (classCode == 16);
    const char *resLabel = isDKClass3 ? "AG" : "Mana";
    int skillCount3 = 0;
    const SkillDef *skills = GetClassSkills(classCode, skillCount3);
    const SkillDef *found = nullptr;
    for (int j = 0; j < skillCount3; j++) {
      if (skills[j].skillId == (uint8_t)skillId) {
        found = &skills[j];
        break;
      }
    }
    if (found) {
      char buf[64];
      BeginPendingTooltip(200, 18 * 4 + 10);
      AddPendingTooltipLine(IM_COL32(255, 210, 80, 255), found->name);
      snprintf(buf, sizeof(buf), "%s Cost: %d", resLabel, found->resourceCost);
      AddPendingTooltipLine(IM_COL32(100, 180, 255, 255), buf);
      snprintf(buf, sizeof(buf), "Damage: +%d", found->damageBonus);
      AddPendingTooltipLine(IM_COL32(255, 200, 100, 255), buf);
      AddPendingTooltipLine(IM_COL32(170, 170, 190, 255), found->desc);
    }
  }
}

void RenderQuickbar(ImDrawList *dl, const UICoords &c) {
  if (!s_ctx->hero)
    return;
  HeroCharacter &hero = *s_ctx->hero;

  int winW = (int)ImGui::GetIO().DisplaySize.x;
  int winH = (int)ImGui::GetIO().DisplaySize.y;
  float screenBottom = (float)winH;

  using namespace HudLayout;

  ImVec2 mp = ImGui::GetIO().MousePos;

  // ══════════════ Background panel (dark gothic stone, multi-layer frame) ══════════════
  {
    float bgX = c.ToScreenX(PANEL_LEFT - 4.0f);
    float bgY = c.ToScreenY(ROW_VY - 8.0f);
    float bgX1 = c.ToScreenX(PANEL_RIGHT + 4.0f);
    float bgY1 = c.ToScreenY(ROW_VY + SLOT + 8.0f);
    // Dark stone gradient background
    dl->AddRectFilledMultiColor(
        ImVec2(bgX, bgY), ImVec2(bgX1, bgY1), IM_COL32(14, 12, 10, 245),
        IM_COL32(14, 12, 10, 245), IM_COL32(20, 17, 14, 240),
        IM_COL32(20, 17, 14, 240));
    // Outermost shadow border (3px, very dark)
    dl->AddRect(ImVec2(bgX - 1, bgY - 1), ImVec2(bgX1 + 1, bgY1 + 1),
                IM_COL32(5, 4, 3, 220), 4.0f, 0, 3.0f);
    // Main gold trim band
    dl->AddRect(ImVec2(bgX, bgY), ImVec2(bgX1, bgY1),
                IM_COL32(130, 108, 52, 200), 4.0f, 0, 2.0f);
    // Inner dark border
    dl->AddRect(ImVec2(bgX + 3, bgY + 3), ImVec2(bgX1 - 3, bgY1 - 3),
                IM_COL32(20, 16, 12, 200), 2.0f);
    // Top highlight line (light catch on metallic frame)
    dl->AddLine(ImVec2(bgX + 6, bgY + 1), ImVec2(bgX1 - 6, bgY + 1),
                IM_COL32(200, 175, 90, 70));
    // Bottom shadow line
    dl->AddLine(ImVec2(bgX + 6, bgY1 - 1), ImVec2(bgX1 - 6, bgY1 - 1),
                IM_COL32(0, 0, 0, 80));
    // Corner rivet dots (4 corners)
    float rivetR = 2.5f;
    ImVec2 corners[] = {
      {bgX + 7, bgY + 7}, {bgX1 - 7, bgY + 7},
      {bgX + 7, bgY1 - 7}, {bgX1 - 7, bgY1 - 7}
    };
    for (auto &cp : corners) {
      dl->AddCircleFilled(ImVec2(cp.x + 1, cp.y + 1), rivetR, IM_COL32(0, 0, 0, 100), 10);
      dl->AddCircleFilled(cp, rivetR, IM_COL32(180, 155, 75, 200), 10);
      dl->AddCircleFilled(ImVec2(cp.x - 0.5f, cp.y - 0.5f), rivetR * 0.4f,
                          IM_COL32(230, 210, 140, 80), 8);
    }
  }

  // ══════════════ HP Orb (left) ══════════════
  {
    int curHP = hero.GetHP();
    int maxHP = hero.GetMaxHP();
    float hpFrac = maxHP > 0 ? (float)curHP / (float)maxHP : 0.0f;

    float orbSX = c.ToScreenX(HP_ORB_CX);
    float orbSY = c.ToScreenY(ORB_CY);
    float orbSR = (c.ToScreenX(HP_ORB_CX + ORB_RADIUS) - orbSX);

    // Show value on the orb itself
    char hpLabel[32];
    snprintf(hpLabel, sizeof(hpLabel), "%d/%d", std::max(curHP, 0), maxHP);

    DrawOrb(dl, orbSX, orbSY, orbSR, hpFrac,
            IM_COL32(255, 80, 50, 245),  // fill top: bright vivid red
            IM_COL32(80, 5, 5, 240),     // fill bottom: very dark red
            IM_COL32(15, 5, 5, 220),     // empty: near black
            IM_COL32(140, 115, 60, 200), // frame: gold
            hpLabel);

    // "HP" label inside orb (upper portion)
    ImVec2 hpsz = ImGui::CalcTextSize("HP");
    float hlx = orbSX - hpsz.x * 0.5f;
    float hly = orbSY - hpsz.y * 0.5f - orbSR * 0.3f;
    dl->AddText(ImVec2(hlx + 1, hly + 1), IM_COL32(0, 0, 0, 200), "HP");
    dl->AddText(ImVec2(hlx, hly), IM_COL32(200, 80, 80, 200), "HP");
  }

  // ══════════════ MP/AG Orb (right) ══════════════
  {
    bool isDK = (hero.GetClass() == 16);
    int curVal = isDK ? hero.GetAG() : hero.GetMana();
    int maxVal = isDK ? hero.GetMaxAG() : hero.GetMaxMana();
    float frac = maxVal > 0 ? (float)curVal / (float)maxVal : 0.0f;

    float orbSX = c.ToScreenX(MP_ORB_CX);
    float orbSY = c.ToScreenY(ORB_CY);
    float orbSR = (c.ToScreenX(MP_ORB_CX + ORB_RADIUS) - orbSX);

    ImU32 fillTop, fillBot, emptyCol;
    const char *resName;
    if (isDK) {
      fillTop = IM_COL32(230, 150, 30, 240); // orange top
      fillBot = IM_COL32(170, 90, 10, 240);  // dark orange
      emptyCol = IM_COL32(20, 12, 5, 220);   // dark orange-brown
      resName = "AG";
    } else {
      fillTop = IM_COL32(80, 140, 255, 245); // bright vivid blue
      fillBot = IM_COL32(5, 20, 90, 240);    // very dark blue
      emptyCol = IM_COL32(5, 5, 15, 220);    // near black
      resName = "MP";
    }

    // Show value on the orb itself
    char resLabel[32];
    snprintf(resLabel, sizeof(resLabel), "%d/%d", std::max(curVal, 0), maxVal);

    DrawOrb(dl, orbSX, orbSY, orbSR, frac, fillTop, fillBot, emptyCol,
            IM_COL32(140, 115, 60, 200), // frame: gold
            resLabel);

    // Resource name inside orb (upper portion)
    ImVec2 nsz = ImGui::CalcTextSize(resName);
    float nlx = orbSX - nsz.x * 0.5f;
    float nly = orbSY - nsz.y * 0.5f - orbSR * 0.3f;
    dl->AddText(ImVec2(nlx + 1, nly + 1), IM_COL32(0, 0, 0, 200), resName);
    ImU32 nameCol =
        isDK ? IM_COL32(230, 170, 50, 200) : IM_COL32(80, 120, 220, 200);
    dl->AddText(ImVec2(nlx, nly), nameCol, resName);
  }

  // ══════════════ CISLM buttons (screen-pixel, bottom-right corner)
  // ══════════════
  {
    const char *btnLabels[] = {"C", "I", "S", "L", "M"};
    // Icon indices: 0=Character, 1=Inventory, 2=Skills, 4=Quests, 5=Map
    // (skip 3=Teleport which was removed)
    int iconIndices[] = {0, 1, 2, 4, 5};

    // Position in screen pixels: bottom-right corner
    // Scale all pixel constants by resolution (designed for 768p)
    float scrW = (float)winW, scrH = (float)winH;
    float uiS = scrH / 768.0f;
    float bs = MBTN_SCREEN_BTN * uiS;
    float btnGap = MBTN_SCREEN_GAP * uiS;
    float rightPad = MBTN_SCREEN_RIGHT_PAD * uiS;
    float bottomPad = MBTN_SCREEN_BOTTOM_PAD * uiS;
    float xpH = XP_SCREEN_H * uiS;
    float xpBot = XP_SCREEN_BOTTOM * uiS;
    float totalBtnW = bs * MBTN_COUNT + btnGap * (MBTN_COUNT - 1);
    float bStartX = scrW - rightPad - totalBtnW;
    float bStartY = scrH - xpBot - xpH - bottomPad - bs;

    for (int i = 0; i < MBTN_COUNT; i++) {
      float bx = bStartX + i * (bs + btnGap);
      float by = bStartY;
      float bcx = bx + bs * 0.5f;
      float bcy = by + bs * 0.5f;
      float br = bs * 0.5f;
      // Circular hit test
      float dx = mp.x - bcx, dy = mp.y - bcy;
      bool hov = (dx * dx + dy * dy) <= (br * br);
      // Filled circle background
      ImU32 bgCol = hov ? IM_COL32(35, 28, 20, 240)
                        : IM_COL32(16, 13, 10, 230);
      dl->AddCircleFilled(ImVec2(bcx, bcy), br, bgCol, 32);
      // Top-half highlight for 3D metallic look
      dl->PushClipRect(ImVec2(bcx - br, bcy - br), ImVec2(bcx + br, bcy), true);
      ImU32 hlCol = hov ? IM_COL32(60, 48, 30, 120)
                        : IM_COL32(30, 24, 18, 80);
      dl->AddCircleFilled(ImVec2(bcx, bcy), br - 1, hlCol, 32);
      dl->PopClipRect();
      // Outer dark shadow ring
      dl->AddCircle(ImVec2(bcx, bcy), br + 1, IM_COL32(5, 4, 3, 180), 32, 2.0f);
      // Main border ring
      ImU32 borderCol = hov ? IM_COL32(210, 180, 85, 240)
                            : IM_COL32(110, 92, 48, 180);
      dl->AddCircle(ImVec2(bcx, bcy), br, borderCol, 32, 1.5f);
      // Inner highlight ring (top half only for sheen)
      dl->PushClipRect(ImVec2(bcx - br, bcy - br), ImVec2(bcx + br, bcy - 2), true);
      dl->AddCircle(ImVec2(bcx, bcy), br - 2, IM_COL32(180, 155, 80, 50),
                    32, 1.0f);
      dl->PopClipRect();
      // Inner shadow ring
      dl->AddCircle(ImVec2(bcx, bcy), br - 3, IM_COL32(0, 0, 0, 50), 32, 1.0f);
      // Render icon as main button content (prominent)
      int iconIdx = iconIndices[i];
      if (TexValid(g_texMenuBtnIcons[iconIdx])) {
        float iconPad = br * 0.22f;
        ImU32 iconTint = IM_COL32(190, 175, 140, 180);
        if (hov)
          iconTint = IM_COL32(240, 220, 160, 220);
        dl->AddImage((ImTextureID)TexImID(g_texMenuBtnIcons[iconIdx]),
                     ImVec2(bcx - br + iconPad, bcy - br + iconPad),
                     ImVec2(bcx + br - iconPad, bcy + br - iconPad),
                     ImVec2(0, 0), ImVec2(1, 1), iconTint);
      }
      // Letter badge (small circle at upper-right with keybind letter)
      {
        float badgeR = br * 0.32f;
        float badgeX = bcx + br * 0.55f;
        float badgeY = bcy - br * 0.55f;
        // Badge background
        dl->AddCircleFilled(ImVec2(badgeX, badgeY), badgeR + 1,
                            IM_COL32(0, 0, 0, 180), 16);
        dl->AddCircleFilled(ImVec2(badgeX, badgeY), badgeR,
                            IM_COL32(30, 25, 18, 230), 16);
        dl->AddCircle(ImVec2(badgeX, badgeY), badgeR,
                      IM_COL32(130, 110, 55, 180), 16, 1.0f);
        // Badge letter
        ImVec2 tsz = ImGui::CalcTextSize(btnLabels[i]);
        float lScale = 0.85f;
        ImU32 textCol = IM_COL32(210, 195, 155, 240);
        dl->AddText(ImVec2(badgeX - tsz.x * lScale * 0.5f + 1,
                           badgeY - tsz.y * lScale * 0.5f + 1),
                    IM_COL32(0, 0, 0, 200), btnLabels[i]);
        dl->AddText(ImVec2(badgeX - tsz.x * lScale * 0.5f,
                           badgeY - tsz.y * lScale * 0.5f),
                    textCol, btnLabels[i]);
      }
    }
  }

  // ══════════════ Potion slots (Q, W, E, R) ══════════════
  const char *potLabels[] = {"Q", "W", "E", "R"};
  for (int i = 0; i < 4; i++) {
    float vx = PotionSlotX(i);
    float vy = ROW_VY;
    float sx = c.ToScreenX(vx);
    float sy = c.ToScreenY(vy);
    float sz = c.ToScreenX(vx + SLOT) - sx;

    ImVec2 p0(sx, sy), p1(sx + sz, sy + sz);
    bool slotHov = mp.x >= p0.x && mp.x < p1.x && mp.y >= p0.y && mp.y < p1.y;
    DrawStyledSlot(dl, p0, p1, slotHov);
    DrawShadowText(dl, ImVec2(sx + 2, sy + 1), IM_COL32(200, 185, 150, 180),
                   potLabels[i]);

    int16_t defIdx = s_ctx->potionBar[i];
    if (defIdx != -1) {
      int count = 0;
      for (int slot = 0; slot < INVENTORY_SLOTS; slot++) {
        if (s_ctx->inventory[slot].occupied && s_ctx->inventory[slot].primary &&
            s_ctx->inventory[slot].defIndex == defIdx)
          count += s_ctx->inventory[slot].quantity;
      }
      if (count > 0) {
        auto it = ItemDatabase::GetItemDefs().find(defIdx);
        if (it != ItemDatabase::GetItemDefs().end()) {
          int potY = (int)(sy + 4); // BGFX: top-left origin
          AddRenderJob({it->second.modelFile, defIdx, (int)sx + 4,
                        potY, (int)sz - 8,
                        (int)sz - 8, false});
          // Mounts are unique — don't show quantity count
          bool isMount = (it->second.category == 13 &&
                         (it->second.itemIndex == 2 || it->second.itemIndex == 3));
          if (!isMount) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "%d", count);
            ImVec2 tsz = ImGui::CalcTextSize(cbuf);
            dl->AddText(ImVec2(sx + sz - tsz.x - 2, sy + sz - 14),
                        IM_COL32(255, 210, 80, 255), cbuf);
          }
        }
      }
    }

    if (*s_ctx->potionCooldown > 0.0f) {
      DeferredCooldown cd;
      cd.x = sx;
      cd.y = sy;
      cd.w = sz;
      cd.h = sz;
      snprintf(cd.text, sizeof(cd.text), "%d",
               (int)ceil(*s_ctx->potionCooldown));
      g_deferredCooldowns.push_back(cd);
    }

    // Gold border for active mount in quickslot
    if (defIdx != -1 && s_ctx->hero && s_ctx->hero->IsMounted()) {
      auto mit = ItemDatabase::GetItemDefs().find(defIdx);
      if (mit != ItemDatabase::GetItemDefs().end() &&
          mit->second.category == 13 &&
          (mit->second.itemIndex == 2 || mit->second.itemIndex == 3))
        dl->AddRect(p0, p1, IM_COL32(255, 210, 80, 255), 3.0f, 0, 2.0f);
    }

    // Potion slot tooltip
    if (slotHov && defIdx != -1) {
      auto it = ItemDatabase::GetItemDefs().find(defIdx);
      if (it != ItemDatabase::GetItemDefs().end()) {
        int count = 0;
        for (int slot = 0; slot < INVENTORY_SLOTS; slot++) {
          if (s_ctx->inventory[slot].occupied &&
              s_ctx->inventory[slot].primary &&
              s_ctx->inventory[slot].defIndex == defIdx)
            count += s_ctx->inventory[slot].quantity;
        }
        char buf[64];
        BeginPendingTooltip(180, 18 * 2 + 10);
        AddPendingTooltipLine(IM_COL32(255, 210, 80, 255), it->second.name);
        snprintf(buf, sizeof(buf), "Quantity: %d", count);
        AddPendingTooltipLine(IM_COL32(170, 170, 190, 255), buf);
      }
    }
  }

  // ══════════════ Skill slots (1, 2, 3, 4) ══════════════
  const char *skillLabels[] = {"1", "2", "3", "4"};
  for (int i = 0; i < 4; i++) {
    float vx = SkillSlotX(i);
    float vy = ROW_VY;
    float sx = c.ToScreenX(vx);
    float sy = c.ToScreenY(vy);
    float sz = c.ToScreenX(vx + SLOT) - sx;

    ImVec2 p0(sx, sy), p1(sx + sz, sy + sz);
    bool slotHov = mp.x >= p0.x && mp.x < p1.x && mp.y >= p0.y && mp.y < p1.y;
    DrawStyledSlot(dl, p0, p1, slotHov);
    // Gold border for active quickslot toggle
    if (i == InputHandler::GetActiveQuickSlot())
      dl->AddRect(p0, p1, IM_COL32(255, 210, 80, 255), 3.0f, 0, 2.0f);
    DrawShadowText(dl, ImVec2(sx + 2, sy + 1), IM_COL32(200, 185, 150, 180),
                   skillLabels[i]);

    int8_t sid = s_ctx->skillBar[i];
    int skillCost = (sid > 0) ? GetSkillResourceCost(sid) : 0;
    bool isDKClass = s_ctx->hero && s_ctx->hero->GetClass() == 16;
    int curRes = isDKClass ? (s_ctx->serverAG ? *s_ctx->serverAG : 0)
                           : (s_ctx->serverMP ? *s_ctx->serverMP : 0);
    bool canAfford = curRes >= skillCost;
    ImU32 tint =
        canAfford ? IM_COL32(255, 255, 255, 255) : IM_COL32(100, 100, 100, 180);
    RenderSkillIcon(dl, sid, sx, sy, sz, tint);
    if (sid > 0 && !canAfford)
      dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 120), 3.0f);

    // GCD overlay
    if (sid > 0 && s_ctx->hero) {
      float gcd = s_ctx->hero->GetGlobalCooldown();
      float gcdMax = s_ctx->hero->GetGlobalCooldownMax();
      if (gcd > 0.0f && gcdMax > 0.0f) {
        float gcdFrac = gcd / gcdMax;
        float fillH = sz * gcdFrac;
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + fillH),
                          IM_COL32(10, 10, 10, 180), 3.0f);
      }
    }

    // Skill slot tooltip
    if (slotHov && sid > 0) {
      uint8_t classCode = s_ctx->hero ? s_ctx->hero->GetClass() : 16;
      bool isDKClass2 = (classCode == 16);
      const char *resourceLabel = isDKClass2 ? "AG" : "Mana";
      int skillCount2 = 0;
      const SkillDef *skills = GetClassSkills(classCode, skillCount2);
      const SkillDef *found = nullptr;
      for (int j = 0; j < skillCount2; j++) {
        if (skills[j].skillId == (uint8_t)sid) {
          found = &skills[j];
          break;
        }
      }
      if (found) {
        char buf[64];
        int numLines = 4;
        BeginPendingTooltip(200, 18 * numLines + 10);
        AddPendingTooltipLine(IM_COL32(255, 210, 80, 255), found->name);
        snprintf(buf, sizeof(buf), "%s Cost: %d", resourceLabel,
                 found->resourceCost);
        AddPendingTooltipLine(IM_COL32(100, 180, 255, 255), buf);
        snprintf(buf, sizeof(buf), "Damage: +%d", found->damageBonus);
        AddPendingTooltipLine(IM_COL32(255, 200, 100, 255), buf);
        AddPendingTooltipLine(IM_COL32(170, 170, 190, 255), found->desc);
      }
    }
  }

  // ══════════════ Unified Buff/Aura Row (above main buttons) ══════════════
  {
    // Collect all active buffs, debuffs, and auras into a flat list
    struct BuffIcon {
      enum Type { SKILL_ICON, TEXTURE_ICON, TEXT_ICON } type;
      int8_t skillId;           // for SKILL_ICON
      int texIdx;               // for TEXTURE_ICON (g_texAuraIcons index)
      const char *textLabel;    // for TEXT_ICON
      ImU32 tint, borderCol, bgCol;
      // Tooltip
      const char *name;
      const char *desc;
      char bonusLine[48];
      char durLine[32];
      bool hasDuration;
      float durationFrac;       // 0..1 remaining fraction
      int durationSecs;
    };
    BuffIcon icons[8];
    int iconCount = 0;

    // Pet/mount auras (leftmost)
    if (s_ctx->hero) {
      if (s_ctx->hero->IsPetActive()) {
        uint8_t petIdx = s_ctx->hero->GetPetItemIndex();
        auto &ic = icons[iconCount++];
        ic.type = BuffIcon::TEXTURE_ICON;
        ic.hasDuration = false;
        ic.durationFrac = 0;
        ic.durationSecs = 0;
        if (petIdx == 0) { // Guardian Angel
          ic.texIdx = 0;
          ic.name = "Guardian Angel";
          ic.desc = "Reduces incoming damage";
          snprintf(ic.bonusLine, sizeof(ic.bonusLine), "-20%% Damage Taken");
          ic.borderCol = IM_COL32(80, 160, 255, 180);
          ic.bgCol = IM_COL32(10, 12, 20, 200);
        } else { // Imp
          ic.texIdx = 1;
          ic.name = "Imp";
          ic.desc = "Increases attack damage";
          snprintf(ic.bonusLine, sizeof(ic.bonusLine), "+30%% Attack Damage");
          ic.borderCol = IM_COL32(255, 100, 40, 180);
          ic.bgCol = IM_COL32(20, 10, 5, 200);
        }
      }
      if (s_ctx->hero->IsMounted()) {
        uint8_t mountIdx = s_ctx->hero->GetMountItemIndex();
        auto &ic = icons[iconCount++];
        ic.type = BuffIcon::TEXTURE_ICON;
        ic.hasDuration = false;
        ic.durationFrac = 0;
        ic.durationSecs = 0;
        if (mountIdx == 2) { // Uniria
          ic.texIdx = 2;
          ic.name = "Uniria";
          ic.desc = "Increases movement speed";
          snprintf(ic.bonusLine, sizeof(ic.bonusLine), "+20%% Move Speed");
          ic.borderCol = IM_COL32(100, 200, 255, 180);
          ic.bgCol = IM_COL32(10, 15, 20, 200);
        } else { // Dinorant
          ic.texIdx = 3;
          ic.name = "Dinorant";
          ic.desc = "Enables flight, boosts speed";
          snprintf(ic.bonusLine, sizeof(ic.bonusLine), "+25%% Move Speed");
          ic.borderCol = IM_COL32(40, 180, 140, 180);
          ic.bgCol = IM_COL32(8, 18, 14, 200);
        }
      }
    }

    // Elf buffs (Greater Defense / Greater Damage)
    const auto *buffs = ClientPacketHandler::GetActiveBuffs();
    for (int b = 0; buffs && b < 2; b++) {
      auto &buff = buffs[b];
      if (!buff.active) continue;
      auto &ic = icons[iconCount++];
      ic.type = BuffIcon::SKILL_ICON;
      ic.skillId = (buff.type == 1) ? 27 : 28;
      ic.tint = (buff.type == 1) ? IM_COL32(100, 200, 255, 255)
                                 : IM_COL32(255, 180, 80, 255);
      ic.borderCol = (buff.type == 1) ? IM_COL32(60, 140, 255, 180)
                                      : IM_COL32(255, 140, 40, 180);
      ic.bgCol = IM_COL32(15, 12, 8, 200);
      ic.name = (buff.type == 1) ? "Greater Defense" : "Greater Damage";
      ic.desc = (buff.type == 1) ? "Increases Defense" : "Increases Attack Damage";
      snprintf(ic.bonusLine, sizeof(ic.bonusLine), "+%d %s", buff.value,
               (buff.type == 1) ? "Defense" : "Damage");
      ic.hasDuration = (buff.maxDuration > 0);
      ic.durationFrac = ic.hasDuration ? buff.remaining / buff.maxDuration : 0;
      ic.durationSecs = (int)ceilf(buff.remaining);
      int mins = ic.durationSecs / 60, rsecs = ic.durationSecs % 60;
      snprintf(ic.durLine, sizeof(ic.durLine), "Remaining: %d:%02d", mins, rsecs);
    }

    // Poison debuff (rightmost)
    auto poison = ClientPacketHandler::GetPoisonState();
    if (poison.active) {
      auto &ic = icons[iconCount++];
      ic.type = BuffIcon::TEXT_ICON;
      ic.textLabel = "P";
      ic.tint = IM_COL32(80, 255, 80, 255);
      ic.borderCol = IM_COL32(40, 180, 40, 180);
      ic.bgCol = IM_COL32(10, 15, 8, 200);
      ic.name = "Poison";
      ic.desc = "Poisoned by monster";
      snprintf(ic.bonusLine, sizeof(ic.bonusLine), "3%% HP damage / 3 sec");
      ic.hasDuration = (poison.maxDuration > 0);
      ic.durationFrac = ic.hasDuration ? poison.remaining / poison.maxDuration : 0;
      ic.durationSecs = (int)ceilf(poison.remaining);
      int mins = ic.durationSecs / 60, rsecs = ic.durationSecs % 60;
      snprintf(ic.durLine, sizeof(ic.durLine), "Remaining: %d:%02d", mins, rsecs);
    }

    // Render all icons in a centered row above the main buttons
    if (iconCount > 0) {
      float iconSz = SLOT * 0.8f;
      float iconGap = 6.0f;
      float totalW = iconCount * iconSz + (iconCount - 1) * iconGap;
      float panelCenterVX = (PANEL_LEFT + PANEL_RIGHT) * 0.5f;
      float startVX = panelCenterVX - totalW * 0.5f;
      float vy = ROW_VY - SLOT * 0.5f - iconSz - 4.0f;

      for (int i = 0; i < iconCount; i++) {
        auto &ic = icons[i];
        float vx = startVX + i * (iconSz + iconGap);
        float sx = c.ToScreenX(vx);
        float sy = c.ToScreenY(vy);
        float sz = c.ToScreenX(vx + iconSz) - sx;

        ImVec2 p0(sx, sy), p1(sx + sz, sy + sz);
        dl->AddRectFilled(p0, p1, ic.bgCol, 3.0f);

        // Render icon content
        if (ic.type == BuffIcon::SKILL_ICON) {
          RenderSkillIcon(dl, ic.skillId, sx, sy, sz, ic.tint);
        } else if (ic.type == BuffIcon::TEXTURE_ICON) {
          if (ic.texIdx >= 0 && ic.texIdx < 4 && TexValid(g_texAuraIcons[ic.texIdx])) {
            float pad = 3.0f;
            dl->AddImage((ImTextureID)TexImID(g_texAuraIcons[ic.texIdx]),
                         ImVec2(sx + pad, sy + pad),
                         ImVec2(sx + sz - pad, sy + sz - pad),
                         ImVec2(0, 0), ImVec2(1, 1),
                         IM_COL32(255, 255, 255, 230));
          }
        } else { // TEXT_ICON
          ImVec2 ksz = ImGui::CalcTextSize(ic.textLabel);
          DrawShadowText(dl, ImVec2(sx + (sz - ksz.x) * 0.5f, sy + 2),
                         ic.tint, ic.textLabel);
        }

        // Cooldown sweep
        if (ic.hasDuration) {
          float fillH = sz * (1.0f - ic.durationFrac);
          dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + fillH),
                            IM_COL32(0, 0, 0, 150), 3.0f);
        }

        // Duration text
        if (ic.hasDuration) {
          char timeBuf[8];
          if (ic.durationSecs >= 60)
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", ic.durationSecs / 60,
                     ic.durationSecs % 60);
          else
            snprintf(timeBuf, sizeof(timeBuf), "%ds", ic.durationSecs);
          ImVec2 tsz = ImGui::CalcTextSize(timeBuf);
          DrawShadowText(dl, ImVec2(sx + (sz - tsz.x) * 0.5f, sy + sz - tsz.y - 1),
                         IM_COL32(255, 255, 255, 230), timeBuf);
        }

        // Border
        dl->AddRect(p0, p1, ic.borderCol, 3.0f, 0, 1.5f);

        // Hover tooltip
        ImVec2 mpos = ImGui::GetIO().MousePos;
        if (mpos.x >= p0.x && mpos.x <= p1.x && mpos.y >= p0.y && mpos.y <= p1.y) {
          float tw = 200.0f, th = 12.0f + 18.0f * 4 + 8.0f;
          BeginPendingTooltip(tw, th);
          g_pendingTooltip.borderColor = ic.borderCol;
          AddPendingTooltipLine(IM_COL32(255, 255, 255, 255), ic.name, 1);
          AddTooltipSeparator();
          AddPendingTooltipLine(IM_COL32(180, 180, 180, 255), ic.desc);
          AddPendingTooltipLine(IM_COL32(100, 255, 100, 255), ic.bonusLine);
          if (ic.hasDuration)
            AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), ic.durLine);
        }
      }
    }
  }

  // ══════════════ RMC Slot ══════════════
  {
    float sx = c.ToScreenX(RmcSlotX());
    float sy = c.ToScreenY(ROW_VY);
    float sz = c.ToScreenX(RmcSlotX() + SLOT) - sx;
    RenderRmcSlot(dl, sx, sy, sz);
  }

  // ══════════════ XP Bar (screen-pixel, full-width, fragmented, screen bottom)
  // ══════════════
  {
    uint64_t curXp = hero.GetExperience();
    int curLv = hero.GetLevel();
    uint64_t nextXp = hero.GetNextExperience();
    uint64_t prevXp = HeroCharacter::CalcXPForLevel(curLv);
    float xpFrac = 0.0f;
    if (nextXp > prevXp)
      xpFrac = (float)(curXp - prevXp) / (float)(nextXp - prevXp);
    xpFrac = std::clamp(xpFrac, 0.0f, 1.0f);

    // Render in screen pixels directly
    float scrW = (float)winW, scrH = (float)winH;
    float barLeft = XP_SCREEN_PAD;
    float barRight = scrW - XP_SCREEN_PAD;
    float barY = scrH - XP_SCREEN_BOTTOM - XP_SCREEN_H;
    float barH = XP_SCREEN_H;
    float totalW = barRight - barLeft;
    float gapScreen = XP_SEG_GAP;
    float segW = (totalW - gapScreen * (XP_SEGMENTS - 1)) / (float)XP_SEGMENTS;

    for (int i = 0; i < XP_SEGMENTS; i++) {
      float sx = barLeft + i * (segW + gapScreen);
      ImVec2 sp0(sx, barY);
      ImVec2 sp1(sx + segW, barY + barH);

      // Segment background — deeper gradient with inner bevel
      dl->AddRectFilledMultiColor(sp0, sp1,
          IM_COL32(3, 3, 8, 230), IM_COL32(3, 3, 8, 230),
          IM_COL32(10, 10, 18, 230), IM_COL32(10, 10, 18, 230));
      // Top inner shadow (inset bevel)
      dl->AddLine(ImVec2(sx + 1, barY + 1), ImVec2(sx + segW - 1, barY + 1),
                  IM_COL32(0, 0, 0, 120));
      // Bottom inner highlight
      dl->AddLine(ImVec2(sx + 1, barY + barH - 1), ImVec2(sx + segW - 1, barY + barH - 1),
                  IM_COL32(30, 30, 50, 40));

      // Fill based on XP fraction — 3-color gradient
      float segStart = (float)i / (float)XP_SEGMENTS;
      float segEnd = (float)(i + 1) / (float)XP_SEGMENTS;
      if (xpFrac > segStart) {
        float segFrac =
            std::clamp((xpFrac - segStart) / (segEnd - segStart), 0.0f, 1.0f);
        float fillW = segW * segFrac;

        // Main fill: 3-row gradient (bright cyan → teal → deep blue)
        float midY = barY + barH * 0.4f;
        // Top portion (bright cyan)
        dl->AddRectFilledMultiColor(
            sp0, ImVec2(sx + fillW, midY),
            IM_COL32(100, 230, 255, 240), IM_COL32(100, 230, 255, 240),
            IM_COL32(50, 190, 230, 240), IM_COL32(50, 190, 230, 240));
        // Bottom portion (teal → deep blue)
        dl->AddRectFilledMultiColor(
            ImVec2(sx, midY), ImVec2(sx + fillW, barY + barH),
            IM_COL32(50, 190, 230, 240), IM_COL32(50, 190, 230, 240),
            IM_COL32(20, 100, 160, 240), IM_COL32(20, 100, 160, 240));

        // Bright top highlight (3px, more visible)
        dl->AddLine(ImVec2(sx + 1, barY + 1), ImVec2(sx + fillW - 1, barY + 1),
                    IM_COL32(220, 255, 255, 80));
        dl->AddLine(ImVec2(sx + 1, barY + 2), ImVec2(sx + fillW - 1, barY + 2),
                    IM_COL32(180, 240, 255, 40));
        dl->AddLine(ImVec2(sx + 1, barY + 3), ImVec2(sx + fillW - 1, barY + 3),
                    IM_COL32(140, 220, 255, 20));

        // Leading edge glow (bright vertical line at fill edge)
        if (segFrac < 0.99f && fillW > 2.0f) {
          dl->AddRectFilled(
              ImVec2(sx + fillW - 2, barY + 1),
              ImVec2(sx + fillW, barY + barH - 1),
              IM_COL32(200, 255, 255, 100));
          // Glow halo around leading edge
          dl->AddRectFilled(
              ImVec2(sx + fillW - 4, barY),
              ImVec2(sx + fillW + 1, barY + barH),
              IM_COL32(100, 220, 255, 25));
        }
      }

      // 10% tick marks within each segment (subtle vertical lines)
      for (int t = 1; t < 10; t++) {
        float tickX = sx + segW * ((float)t / 10.0f);
        dl->AddLine(ImVec2(tickX, barY + 1), ImVec2(tickX, barY + barH - 1),
                    IM_COL32(40, 40, 60, 40));
      }

      // 3-layer border — outer shadow, gold frame, inner dark
      dl->AddRect(ImVec2(sp0.x - 1, sp0.y - 1), ImVec2(sp1.x + 1, sp1.y + 1),
                  IM_COL32(0, 0, 0, 100), 1.0f);
      dl->AddRect(sp0, sp1, IM_COL32(90, 75, 40, 140), 1.0f);
      dl->AddRect(ImVec2(sp0.x + 1, sp0.y + 1), ImVec2(sp1.x - 1, sp1.y - 1),
                  IM_COL32(50, 45, 30, 50), 1.0f);
    }

    // Level + XP text centered above the bar (Cinzel font, larger)
    float uiS = (float)winH / 768.0f;
    char xpLabel[64];
    snprintf(xpLabel, sizeof(xpLabel), "Level %d  -  %.1f%%", curLv,
             xpFrac * 100.0f);
    ImFont *xpFont = s_ctx->fontBold ? s_ctx->fontBold : ImGui::GetFont();
    float xpFs = 16.0f * uiS;
    ImVec2 tsz = xpFont->CalcTextSizeA(xpFs, FLT_MAX, 0, xpLabel);
    float tx = barLeft + (totalW - tsz.x) * 0.5f;
    float ty = barY - tsz.y - 3.0f;
    dl->AddText(xpFont, xpFs, ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), xpLabel);
    dl->AddText(xpFont, xpFs, ImVec2(tx, ty), IM_COL32(220, 200, 150, 240), xpLabel);
  }
}

// ═══════════════════════════════════════════════════════════════════
// Skill Window (S key) -- Class-aware skill/spell panel
// ═══════════════════════════════════════════════════════════════════

void RenderSkillPanel(ImDrawList *dl, const UICoords &c) {
  // Get class-appropriate skill list
  uint8_t classCode = s_ctx->hero ? s_ctx->hero->GetClass() : 16;
  bool isDK = (classCode == 16);
  int skillCount = 0;
  const SkillDef *skills = GetClassSkills(classCode, skillCount);
  const char *resourceLabel = isDK ? "AG" : "Mana";
  const char *panelTitle = isDK ? "Skills" : "Spells";

  // Grid layout: cleaner spacing, slightly smaller cells
  static constexpr int GRID_COLS = 5;
  int GRID_ROWS = (skillCount + GRID_COLS - 1) / GRID_COLS;
  static constexpr float CELL_W = 105.0f;
  static constexpr float CELL_H = 95.0f;
  static constexpr float CELL_PAD = 12.0f;
  static constexpr float TITLE_H = 36.0f;
  static constexpr float FOOTER_H = 28.0f;
  static constexpr float MARGIN = 18.0f;

  float pw = MARGIN * 2 + GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_PAD;
  float ph = TITLE_H + MARGIN + GRID_ROWS * CELL_H +
             (GRID_ROWS - 1) * CELL_PAD + FOOTER_H + MARGIN;

  // Center on screen
  float px = (UICoords::VIRTUAL_W - pw) * 0.5f;
  float py = (UICoords::VIRTUAL_H - ph) * 0.5f;

  // Colors
  const ImU32 colTitle = IM_COL32(255, 210, 80, 255);
  const ImU32 colLabel = IM_COL32(170, 170, 190, 255);
  const ImU32 colValue = IM_COL32(255, 255, 255, 255);
  const ImU32 colGreen = IM_COL32(100, 255, 100, 255);
  const ImU32 colRed = IM_COL32(255, 80, 80, 255);
  const ImU32 colDim = IM_COL32(255, 255, 255, 100);
  char buf[256];

  // Background
  DrawStyledPanel(dl, c.ToScreenX(px), c.ToScreenY(py), c.ToScreenX(px + pw),
                  c.ToScreenY(py + ph), 6.0f);

  // Title -- bold font, centered with gold separator
  {
    ImFont *titleFont = s_ctx->fontBold ? s_ctx->fontBold : ImGui::GetFont();
    float titleFs = titleFont->LegacySize * ImGui::GetIO().FontGlobalScale;
    ImVec2 tsz = titleFont->CalcTextSizeA(titleFs, FLT_MAX, 0, panelTitle);
    float tx = c.ToScreenX(px + pw * 0.5f) - tsz.x * 0.5f;
    float ty = c.ToScreenY(py + 8.0f);
    dl->AddText(titleFont, titleFs, ImVec2(tx + 2, ty + 2), IM_COL32(0, 0, 0, 230), panelTitle);
    dl->AddText(titleFont, titleFs, ImVec2(tx, ty), colTitle, panelTitle);
    // Gold separator line
    float sepY = c.ToScreenY(py + TITLE_H - 2.0f);
    float sepL = c.ToScreenX(px + MARGIN);
    float sepR = c.ToScreenX(px + pw - MARGIN);
    dl->AddLine(ImVec2(sepL, sepY), ImVec2(sepR, sepY),
                IM_COL32(130, 108, 52, 140), 1.0f);
    dl->AddLine(ImVec2(sepL, sepY + 1), ImVec2(sepR, sepY + 1),
                IM_COL32(0, 0, 0, 100), 1.0f);
  }

  // Close button (top-right)
  {
    float bx = px + pw - 24.0f;
    float by = py + 8.0f;
    float bw = 16.0f, bh = 16.0f;
    ImVec2 bMin(c.ToScreenX(bx), c.ToScreenY(by));
    ImVec2 bMax(c.ToScreenX(bx + bw), c.ToScreenY(by + bh));
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
    if (hovered && ImGui::IsMouseClicked(0)) {
      SoundManager::Play(SOUND_CLICK01);
      *s_ctx->showSkillWindow = false;
    }
  }

  // Check which skills are learned
  auto isLearned = [](uint8_t skillId) -> bool {
    if (!s_ctx->learnedSkills)
      return false;
    for (auto s : *s_ctx->learnedSkills) {
      if (s == skillId)
        return true;
    }
    return false;
  };

  ImVec2 mousePos = ImGui::GetIO().MousePos;

  // Icon display size (smaller, preserve 20:28 aspect ratio)
  static constexpr float ICON_DISP_W = 38.0f;
  static constexpr float ICON_DISP_H = 53.0f; // 38 * 28/20 ≈ 53

  float gridStartY = py + TITLE_H + 4.0f;

  for (int i = 0; i < skillCount; i++) {
    const auto &skill = skills[i];
    bool learned = isLearned(skill.skillId);

    int col = i % GRID_COLS;
    int row = i / GRID_COLS;
    float cx = px + MARGIN + col * (CELL_W + CELL_PAD);
    float cy = gridStartY + row * (CELL_H + CELL_PAD);

    // Cell background
    ImVec2 cMin(c.ToScreenX(cx), c.ToScreenY(cy));
    ImVec2 cMax(c.ToScreenX(cx + CELL_W), c.ToScreenY(cy + CELL_H));

    bool cellHovered = mousePos.x >= cMin.x && mousePos.x < cMax.x &&
                       mousePos.y >= cMin.y && mousePos.y < cMax.y;

    // Use DrawStyledSlot for consistent Diablo-style gothic cells
    DrawStyledSlot(dl, cMin, cMax, cellHovered, 4.0f);
    // Learned skill: subtle gold inner glow
    if (learned) {
      dl->AddRect(ImVec2(cMin.x + 2, cMin.y + 2), ImVec2(cMax.x - 2, cMax.y - 2),
                  IM_COL32(180, 150, 60, 50), 3.0f, 0, 1.0f);
    }

    // Icon centered at top of cell
    if (TexValid(g_texSkillIcons)) {
      int iconIdx = skill.skillId;
      int ic = iconIdx % SKILL_ICON_COLS;
      int ir = iconIdx / SKILL_ICON_COLS;
      // Tiny inset (0.1 texels) prevents bilinear bleed from neighbors
      static constexpr float UV_INSET = 0.1f / SKILL_TEX_SIZE;
      float u0 = (SKILL_ICON_W * ic) / SKILL_TEX_SIZE + UV_INSET;
      float v0 = (SKILL_ICON_H * ir) / SKILL_TEX_SIZE + UV_INSET;
      float u1 = (SKILL_ICON_W * (ic + 1)) / SKILL_TEX_SIZE - UV_INSET;
      float v1 = (SKILL_ICON_H * (ir + 1)) / SKILL_TEX_SIZE - UV_INSET;

      float iconX = cx + (CELL_W - ICON_DISP_W) * 0.5f;
      float iconY = cy + 6.0f;

      ImVec2 iMin(c.ToScreenX(iconX), c.ToScreenY(iconY));
      ImVec2 iMax(c.ToScreenX(iconX + ICON_DISP_W),
                  c.ToScreenY(iconY + ICON_DISP_H));

      // Learning animation: interpolate opacity 100->255 over learn duration
      bool isBeingLearned =
          *s_ctx->isLearningSkill && *s_ctx->learningSkillId == skill.skillId;
      int iconAlpha;
      if (learned)
        iconAlpha = 255;
      else if (isBeingLearned)
        iconAlpha = 100 + (int)(155.0f *
                                std::min(1.0f, *s_ctx->learnSkillTimer / 3.0f));
      else
        iconAlpha = 80;
      ImU32 iconTint = IM_COL32(255, 255, 255, iconAlpha);
      dl->AddImage((ImTextureID)TexImID(g_texSkillIcons), iMin, iMax,
                   ImVec2(u0, v0), ImVec2(u1, v1), iconTint);

      // "Learning..." / "Learned" label overlay
      if (isBeingLearned) {
        const char *lbl = "Learning...";
        ImVec2 lsz = ImGui::CalcTextSize(lbl);
        float lx = (iMin.x + iMax.x) * 0.5f - lsz.x * 0.5f;
        float ly = iMax.y - lsz.y - 2.0f;
        dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), lbl);
        dl->AddText(ImVec2(lx, ly), IM_COL32(255, 210, 80, 255), lbl);
      }
    }

    // Drag from skill panel (learned skills only)
    if (learned && cellHovered && ImGui::IsMouseClicked(0) && !g_isDragging) {
      g_isDragging = true;
      g_dragFromSlot = -1;
      g_dragFromEquipSlot = -1;
      g_dragDefIndex =
          -skill.skillId; // Negative = skill ID (not item defIndex)
      std::cout << "[Skill] Started dragging skill " << skill.name << std::endl;
    }

    // Skill name centered below icon (clipped to cell width)
    {
      ImU32 nameCol = learned ? colValue : colDim;
      float nameY = cy + 5.0f + ICON_DISP_H + 3.0f;
      float cellScrL = c.ToScreenX(cx + 3.0f);
      float cellScrR = c.ToScreenX(cx + CELL_W - 3.0f);
      float ny = c.ToScreenY(nameY);
      ImVec2 nsz = ImGui::CalcTextSize(skill.name);
      float nx = c.ToScreenX(cx + CELL_W * 0.5f) - nsz.x * 0.5f;
      if (nx < cellScrL) nx = cellScrL;
      dl->PushClipRect(ImVec2(cellScrL, ny), ImVec2(cellScrR, ny + nsz.y + 2), true);
      dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 180), skill.name);
      dl->AddText(ImVec2(nx, ny), nameCol, skill.name);
      dl->PopClipRect();
    }

    // Level req badge (bottom-right corner of cell, only if > 1)
    uint16_t skillLvReq = GetSkillLevelReq(skill.skillId);
    if (skillLvReq > 1) {
      snprintf(buf, sizeof(buf), "Lv%d", skillLvReq);
      bool meetsReq = *s_ctx->serverLevel >= skillLvReq;
      ImU32 reqCol = meetsReq ? colGreen : colRed;
      if (!learned)
        reqCol = (reqCol & 0x00FFFFFF) | 0x64000000;
      ImVec2 rsz = ImGui::CalcTextSize(buf);
      float rx = c.ToScreenX(cx + CELL_W - 4.0f) - rsz.x;
      float ry = c.ToScreenY(cy + CELL_H - 14.0f);
      dl->AddText(ImVec2(rx + 1, ry + 1), IM_COL32(0, 0, 0, 180), buf);
      dl->AddText(ImVec2(rx, ry), reqCol, buf);
    }

    // Tooltip on hover (with header separator)
    if (cellHovered) {
      float tw = 210;
      float lineH = 18;
      int numLines = 5;
      float th = lineH * numLines + 12;

      BeginPendingTooltip(tw, th);
      AddPendingTooltipLine(colTitle, skill.name, 1);
      AddTooltipSeparator();

      snprintf(buf, sizeof(buf), "%s Cost: %d", resourceLabel,
               skill.resourceCost);
      AddPendingTooltipLine(IM_COL32(100, 180, 255, 255), buf);

      snprintf(buf, sizeof(buf), "Damage: +%d", skill.damageBonus);
      AddPendingTooltipLine(IM_COL32(255, 200, 100, 255), buf);

      uint16_t ttLvReq = GetSkillLevelReq(skill.skillId);
      snprintf(buf, sizeof(buf), "Level Req: %d", ttLvReq);
      bool meetsReq = *s_ctx->serverLevel >= ttLvReq;
      AddPendingTooltipLine(meetsReq ? colGreen : colRed, buf);

      AddPendingTooltipLine(colLabel, skill.desc);

      if (!learned) {
        AddPendingTooltipLine(IM_COL32(255, 150, 50, 255), "(Not learned)");
      }
    }
  }

  // Footer separator + text
  {
    float sepY = c.ToScreenY(py + ph - FOOTER_H - 2.0f);
    float sepL = c.ToScreenX(px + MARGIN);
    float sepR = c.ToScreenX(px + pw - MARGIN);
    dl->AddLine(ImVec2(sepL, sepY), ImVec2(sepR, sepY),
                IM_COL32(130, 108, 52, 100), 1.0f);
  }
  int learnedCount =
      s_ctx->learnedSkills ? (int)s_ctx->learnedSkills->size() : 0;
  snprintf(buf, sizeof(buf), "Learned: %d / %d", learnedCount, skillCount);
  {
    ImVec2 fsz = ImGui::CalcTextSize(buf);
    float fx = c.ToScreenX(px + pw * 0.5f) - fsz.x * 0.5f;
    float fy = c.ToScreenY(py + ph - FOOTER_H + 4.0f);
    dl->AddText(ImVec2(fx + 1, fy + 1), IM_COL32(0, 0, 0, 180), buf);
    dl->AddText(ImVec2(fx, fy), IM_COL32(200, 180, 120, 220), buf);
  }
}

void RenderCastBar(ImDrawList *dl) {
  if (!s_ctx)
    return;

  bool isTeleport = s_ctx->teleportingToTown && *s_ctx->teleportingToTown;
  bool isLearning = s_ctx->isLearningSkill && *s_ctx->isLearningSkill;
  bool isMounting = s_ctx->mountToggling && *s_ctx->mountToggling;
  if (!isTeleport && !isLearning && !isMounting)
    return;

  // Build label and progress
  char label[64];
  float progress = 0.0f;
  float remaining = 0.0f;

  if (isLearning) {
    const char *name = GetSkillName(*s_ctx->learningSkillId);
    snprintf(label, sizeof(label), "Learning %s", name ? name : "Skill");
    float elapsed = *s_ctx->learnSkillTimer;
    float duration = s_ctx->learnSkillDuration;
    progress = std::clamp(elapsed / duration, 0.0f, 1.0f);
    remaining = std::max(0.0f, duration - elapsed);
  } else if (isMounting) {
    bool mounted = s_ctx->hero && s_ctx->hero->IsMounted();
    snprintf(label, sizeof(label), mounted ? "Dismounting" : "Mounting");
    float timer = *s_ctx->mountToggleTimer;
    float duration = s_ctx->mountToggleTime;
    progress = std::clamp(1.0f - timer / duration, 0.0f, 1.0f);
    remaining = std::max(0.0f, timer);
  } else {
    snprintf(label, sizeof(label), "Teleporting to Town");
    float timer = *s_ctx->teleportTimer;
    float duration = s_ctx->teleportCastTime;
    progress = std::clamp(1.0f - timer / duration, 0.0f, 1.0f);
    remaining = std::max(0.0f, timer);
  }

  // Minimal layout: narrow bar centered on screen, scaled for fullscreen
  ImVec2 disp = ImGui::GetIO().DisplaySize;
  float uiScale = ImGui::GetIO().FontGlobalScale;
  float barW = 220.0f * uiScale;
  float barH = 14.0f * uiScale;
  float bx = (disp.x - barW) * 0.5f;
  float by = disp.y * 0.68f; // Above HUD bar

  // Label above bar
  ImVec2 labelSz = ImGui::CalcTextSize(label);
  float lx = bx + (barW - labelSz.x) * 0.5f;
  float ly = by - labelSz.y - 3.0f;
  dl->AddText(ImVec2(lx + 1, ly + 1), IM_COL32(0, 0, 0, 200), label);
  dl->AddText(ImVec2(lx, ly), IM_COL32(220, 210, 180, 240), label);

  // Bar background
  dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
                    IM_COL32(0, 0, 0, 160), 2.0f);

  // Fill
  if (progress > 0.01f) {
    float fillW = barW * progress;
    dl->AddRectFilledMultiColor(
        ImVec2(bx, by), ImVec2(bx + fillW, by + barH),
        IM_COL32(190, 165, 55, 220), IM_COL32(190, 165, 55, 220),
        IM_COL32(130, 105, 30, 220), IM_COL32(130, 105, 30, 220));
  }

  // Thin border
  dl->AddRect(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
              IM_COL32(80, 70, 45, 180), 2.0f);

  // Time right-aligned inside bar
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%.1fs", remaining);
  ImVec2 tsz = ImGui::CalcTextSize(timeBuf);
  float tx = bx + barW - tsz.x - 4.0f;
  float ty = by + (barH - tsz.y) * 0.5f;
  dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), timeBuf);
  dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 220), timeBuf);
}

} // namespace InventoryUI
