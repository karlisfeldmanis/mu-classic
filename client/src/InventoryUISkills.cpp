#include "ClientPacketHandler.hpp"
#include "HeroCharacter.hpp"
#include "InputHandler.hpp"
#include "InventoryUI_Internal.hpp"
#include "SoundManager.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

// ─── File-local helpers ─────────────────────────────────────────────────────

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

  // ══════════════ Background panel (dark gothic stone) ══════════════
  {
    float bgX = c.ToScreenX(PANEL_LEFT - 4.0f);
    float bgY = c.ToScreenY(ROW_VY - 8.0f);
    float bgX1 = c.ToScreenX(PANEL_RIGHT + 4.0f);
    float bgY1 = c.ToScreenY(ROW_VY + SLOT + 8.0f);
    // Dark stone gradient
    dl->AddRectFilledMultiColor(
        ImVec2(bgX, bgY), ImVec2(bgX1, bgY1), IM_COL32(12, 10, 8, 240),
        IM_COL32(12, 10, 8, 240), IM_COL32(18, 15, 12, 235),
        IM_COL32(18, 15, 12, 235));
    // Outer dark border
    dl->AddRect(ImVec2(bgX, bgY), ImVec2(bgX1, bgY1), IM_COL32(8, 6, 4, 255),
                4.0f, 0, 2.5f);
    // Inner gold trim
    dl->AddRect(ImVec2(bgX + 2, bgY + 2), ImVec2(bgX1 - 2, bgY1 - 2),
                IM_COL32(120, 100, 55, 100), 3.0f);
    // Top decorative line (filigree accent)
    dl->AddLine(ImVec2(bgX + 10, bgY + 1), ImVec2(bgX1 - 10, bgY + 1),
                IM_COL32(160, 135, 70, 80));
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
            IM_COL32(200, 40, 30, 240),  // fill top: bright red
            IM_COL32(120, 15, 10, 240),  // fill bottom: dark red
            IM_COL32(20, 8, 8, 220),     // empty: very dark red
            IM_COL32(140, 115, 60, 200), // frame: gold
            hpLabel);

    // "HP" label above orb
    ImVec2 hpsz = ImGui::CalcTextSize("HP");
    float hlx = orbSX - hpsz.x * 0.5f;
    float hly = orbSY - orbSR - hpsz.y - 3.0f;
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
      fillTop = IM_COL32(40, 80, 220, 240); // blue top
      fillBot = IM_COL32(15, 35, 140, 240); // blue bottom
      emptyCol = IM_COL32(8, 8, 20, 220);   // dark blue
      resName = "MP";
    }

    // Show value on the orb itself
    char resLabel[32];
    snprintf(resLabel, sizeof(resLabel), "%d/%d", std::max(curVal, 0), maxVal);

    DrawOrb(dl, orbSX, orbSY, orbSR, frac, fillTop, fillBot, emptyCol,
            IM_COL32(140, 115, 60, 200), // frame: gold
            resLabel);

    // Resource name above orb
    ImVec2 nsz = ImGui::CalcTextSize(resName);
    float nlx = orbSX - nsz.x * 0.5f;
    float nly = orbSY - orbSR - nsz.y - 3.0f;
    dl->AddText(ImVec2(nlx + 1, nly + 1), IM_COL32(0, 0, 0, 200), resName);
    ImU32 nameCol =
        isDK ? IM_COL32(230, 170, 50, 200) : IM_COL32(80, 120, 220, 200);
    dl->AddText(ImVec2(nlx, nly), nameCol, resName);
  }

  // ══════════════ CISTM buttons (screen-pixel, bottom-right corner)
  // ══════════════
  {
    const char *btnLabels[] = {"C", "I", "S", "T", "M"};
    bool tDisabled = (s_ctx->hero && s_ctx->hero->IsInSafeZone());
    bool mDisabled = !(s_ctx->hero && s_ctx->hero->HasMountEquipped());

    // Position in screen pixels: bottom-right corner
    float scrW = (float)winW, scrH = (float)winH;
    float bs = MBTN_SCREEN_BTN;
    float bStartX = scrW - MBTN_SCREEN_RIGHT_PAD - MBTN_SCREEN_TOTAL_W;
    float bStartY =
        scrH - XP_SCREEN_BOTTOM - XP_SCREEN_H - MBTN_SCREEN_BOTTOM_PAD - bs;

    for (int i = 0; i < MBTN_COUNT; i++) {
      float bx = bStartX + i * (bs + MBTN_SCREEN_GAP);
      float by = bStartY;
      ImVec2 bp0(bx, by), bp1(bx + bs, by + bs);
      bool hov = mp.x >= bp0.x && mp.x < bp1.x && mp.y >= bp0.y && mp.y < bp1.y;
      bool disabled = (i == 3 && tDisabled) || (i == 4 && mDisabled);
      // Gothic dark stone button
      ImU32 topFill = disabled ? IM_COL32(12, 10, 8, 200)
                      : hov    ? IM_COL32(40, 32, 22, 240)
                               : IM_COL32(18, 14, 10, 230);
      ImU32 botFill = disabled ? IM_COL32(8, 6, 5, 200)
                      : hov    ? IM_COL32(30, 24, 16, 240)
                               : IM_COL32(12, 10, 8, 230);
      dl->AddRectFilledMultiColor(bp0, bp1, topFill, topFill, botFill, botFill);
      ImU32 borderCol = disabled ? IM_COL32(40, 35, 25, 100)
                        : hov    ? IM_COL32(200, 170, 80, 230)
                                 : IM_COL32(100, 85, 45, 160);
      dl->AddRect(bp0, bp1, borderCol, 3.0f);
      ImVec2 tsz = ImGui::CalcTextSize(btnLabels[i]);
      ImU32 textCol =
          disabled ? IM_COL32(80, 70, 55, 140) : IM_COL32(200, 185, 150, 240);
      DrawShadowText(dl,
                     ImVec2(bx + (bs - tsz.x) * 0.5f, by + (bs - tsz.y) * 0.5f),
                     textCol, btnLabels[i]);
      // Teleport cooldown overlay
      if (i == 3 && s_ctx->hero && s_ctx->hero->GetTeleportCooldown() > 0.0f) {
        float cd = s_ctx->hero->GetTeleportCooldown();
        float cdMax = s_ctx->hero->GetTeleportCooldownMax();
        float cdFrac = cd / cdMax;
        float fillH = bs * cdFrac;
        dl->AddRectFilled(bp0, ImVec2(bp1.x, bp0.y + fillH),
                          IM_COL32(10, 10, 10, 180), 3.0f);
        char cdBuf[8];
        snprintf(cdBuf, sizeof(cdBuf), "%d", (int)ceil(cd));
        ImVec2 cdsz = ImGui::CalcTextSize(cdBuf);
        DrawShadowText(dl,
                       ImVec2(bx + (bs - cdsz.x) * 0.5f, by + bs - cdsz.y - 2),
                       IM_COL32(255, 180, 80, 255), cdBuf);
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
          char cbuf[16];
          snprintf(cbuf, sizeof(cbuf), "%d", count);
          ImVec2 tsz = ImGui::CalcTextSize(cbuf);
          dl->AddText(ImVec2(sx + sz - tsz.x - 2, sy + sz - 14),
                      IM_COL32(255, 210, 80, 255), cbuf);
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

  // ══════════════ Active Buff Icons (above skill slots) ══════════════
  {
    const auto *buffs = ClientPacketHandler::GetActiveBuffs();
    for (int b = 0; buffs && b < 2; b++) {
      auto &buff = buffs[b];
      if (!buff.active) continue;

      int8_t iconSkillId = (buff.type == 1) ? 27 : 28;
      // Position: above skill slots 1-2 (buff 0) and 3-4 (buff 1)
      float vx = SkillSlotX(b * 2) + (SLOT + GAP) * 0.5f;
      float vy = ROW_VY - SLOT - 6.0f;
      float sx = c.ToScreenX(vx);
      float sy = c.ToScreenY(vy);
      float sz = c.ToScreenX(vx + SLOT * 0.8f) - sx;

      ImVec2 p0(sx, sy), p1(sx + sz, sy + sz);
      dl->AddRectFilled(p0, p1, IM_COL32(15, 12, 8, 200), 3.0f);

      ImU32 tint = (buff.type == 1) ? IM_COL32(100, 200, 255, 255)
                                    : IM_COL32(255, 180, 80, 255);
      RenderSkillIcon(dl, iconSkillId, sx, sy, sz, tint);

      // Cooldown sweep (fills from top as time passes)
      if (buff.maxDuration > 0) {
        float frac = buff.remaining / buff.maxDuration;
        float fillH = sz * (1.0f - frac);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + fillH),
                          IM_COL32(0, 0, 0, 150), 3.0f);
      }

      // Duration text (m:ss for >= 60s, Ns for < 60s)
      char timeBuf[8];
      int secs = (int)ceilf(buff.remaining);
      if (secs >= 60)
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", secs / 60, secs % 60);
      else
        snprintf(timeBuf, sizeof(timeBuf), "%ds", secs);
      ImVec2 tsz = ImGui::CalcTextSize(timeBuf);
      DrawShadowText(dl, ImVec2(sx + (sz - tsz.x) * 0.5f, sy + sz - tsz.y - 1),
                     IM_COL32(255, 255, 255, 230), timeBuf);

      // Border glow
      ImU32 borderCol = (buff.type == 1) ? IM_COL32(60, 140, 255, 180)
                                         : IM_COL32(255, 140, 40, 180);
      dl->AddRect(p0, p1, borderCol, 3.0f, 0, 1.5f);

      // Hover tooltip
      ImVec2 mpos = ImGui::GetIO().MousePos;
      if (mpos.x >= p0.x && mpos.x <= p1.x && mpos.y >= p0.y && mpos.y <= p1.y) {
        const char *name = (buff.type == 1) ? "Greater Defense" : "Greater Damage";
        const char *desc = (buff.type == 1) ? "Increases Defense" : "Increases Attack Damage";
        int mins = secs / 60, rsecs = secs % 60;
        char durLine[32];
        snprintf(durLine, sizeof(durLine), "Remaining: %d:%02d", mins, rsecs);
        char bonusLine[48];
        snprintf(bonusLine, sizeof(bonusLine), "+%d %s", buff.value,
                 (buff.type == 1) ? "Defense" : "Damage");

        float tw = 180.0f, th = 12.0f + 18.0f * 4 + 8.0f;
        BeginPendingTooltip(tw, th);
        g_pendingTooltip.borderColor = borderCol;
        AddPendingTooltipLine(IM_COL32(255, 255, 255, 255), name, 1);
        AddTooltipSeparator();
        AddPendingTooltipLine(IM_COL32(180, 180, 180, 255), desc);
        AddPendingTooltipLine(IM_COL32(100, 255, 100, 255), bonusLine);
        AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), durLine);
      }
    }
  }

  // ══════════════ Active Debuff Icons (poison — above skill slots, right side) ══
  {
    auto poison = ClientPacketHandler::GetPoisonState();
    if (poison.active) {
      // Position: above skill slot 4 (rightmost), same row as buff icons
      float vx = SkillSlotX(3) + (SLOT + GAP) * 0.5f;
      float vy = ROW_VY - SLOT - 6.0f;
      float sx = c.ToScreenX(vx);
      float sy = c.ToScreenY(vy);
      float sz = c.ToScreenX(vx + SLOT * 0.8f) - sx;

      ImVec2 p0(sx, sy), p1(sx + sz, sy + sz);
      dl->AddRectFilled(p0, p1, IM_COL32(10, 15, 8, 200), 3.0f);

      // Poison skull icon — use skill icon 30 (Poison) or just draw text
      // Use green-tinted skull symbol
      ImU32 poisonTint = IM_COL32(80, 255, 80, 255);
      const char *skull = "P"; // Poison indicator
      ImVec2 ksz = ImGui::CalcTextSize(skull);
      DrawShadowText(dl, ImVec2(sx + (sz - ksz.x) * 0.5f, sy + 2),
                     poisonTint, skull);

      // Cooldown sweep (fills from top as time passes)
      if (poison.maxDuration > 0) {
        float frac = poison.remaining / poison.maxDuration;
        float fillH = sz * (1.0f - frac);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + fillH),
                          IM_COL32(0, 0, 0, 150), 3.0f);
      }

      // Duration text
      char timeBuf[8];
      int secs = (int)ceilf(poison.remaining);
      snprintf(timeBuf, sizeof(timeBuf), "%ds", secs);
      ImVec2 tsz = ImGui::CalcTextSize(timeBuf);
      DrawShadowText(dl, ImVec2(sx + (sz - tsz.x) * 0.5f, sy + sz - tsz.y - 1),
                     IM_COL32(80, 255, 80, 230), timeBuf);

      // Green poison border
      dl->AddRect(p0, p1, IM_COL32(40, 180, 40, 180), 3.0f, 0, 1.5f);

      // Hover tooltip
      ImVec2 mpos = ImGui::GetIO().MousePos;
      if (mpos.x >= p0.x && mpos.x <= p1.x && mpos.y >= p0.y && mpos.y <= p1.y) {
        int mins = secs / 60, rsecs = secs % 60;
        char durLine[32];
        snprintf(durLine, sizeof(durLine), "Remaining: %d:%02d", mins, rsecs);

        float tw = 200.0f, th = 12.0f + 18.0f * 4 + 8.0f;
        BeginPendingTooltip(tw, th);
        g_pendingTooltip.borderColor = IM_COL32(40, 180, 40, 180);
        AddPendingTooltipLine(IM_COL32(80, 255, 80, 255), "Poison", 1);
        AddTooltipSeparator();
        AddPendingTooltipLine(IM_COL32(180, 180, 180, 255), "Poisoned by monster");
        AddPendingTooltipLine(IM_COL32(255, 100, 100, 255), "3% HP damage / 3 sec");
        AddPendingTooltipLine(IM_COL32(200, 200, 200, 255), durLine);
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

      // Segment background
      dl->AddRectFilled(sp0, sp1, IM_COL32(8, 8, 12, 200), 1.0f);

      // Fill based on XP fraction
      float segStart = (float)i / (float)XP_SEGMENTS;
      float segEnd = (float)(i + 1) / (float)XP_SEGMENTS;
      if (xpFrac > segStart) {
        float segFrac =
            std::clamp((xpFrac - segStart) / (segEnd - segStart), 0.0f, 1.0f);
        float fillW = segW * segFrac;
        dl->AddRectFilledMultiColor(
            sp0, ImVec2(sx + fillW, barY + barH), IM_COL32(50, 180, 220, 220),
            IM_COL32(50, 180, 220, 220), IM_COL32(30, 130, 170, 220),
            IM_COL32(30, 130, 170, 220));
        // Subtle highlight at top
        dl->AddLine(ImVec2(sx + 1, barY + 1), ImVec2(sx + fillW - 1, barY + 1),
                    IM_COL32(255, 255, 255, 30));
      }

      // Thin border
      dl->AddRect(sp0, sp1, IM_COL32(40, 40, 50, 120), 1.0f);
    }

    // Level + XP text centered above the bar
    char xpLabel[64];
    snprintf(xpLabel, sizeof(xpLabel), "Lv.%d  -  %.1f%%", curLv,
             xpFrac * 100.0f);
    ImVec2 tsz = ImGui::CalcTextSize(xpLabel);
    float tx = barLeft + (totalW - tsz.x) * 0.5f;
    float ty = barY - tsz.y - 2.0f;
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 200), xpLabel);
    dl->AddText(ImVec2(tx, ty), IM_COL32(200, 190, 140, 220), xpLabel);
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

  // Grid layout: adapt rows to skill count
  static constexpr int GRID_COLS = 4;
  int GRID_ROWS = (skillCount + GRID_COLS - 1) / GRID_COLS;
  static constexpr float CELL_W = 110.0f;
  static constexpr float CELL_H = 105.0f;
  static constexpr float CELL_PAD = 10.0f;
  static constexpr float TITLE_H = 32.0f;
  static constexpr float FOOTER_H = 24.0f;
  static constexpr float MARGIN = 16.0f;

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

  // Title -- centered with strong shadow
  {
    const char *title = panelTitle;
    ImVec2 tsz = ImGui::CalcTextSize(title);
    float tx = c.ToScreenX(px + pw * 0.5f) - tsz.x * 0.5f;
    float ty = c.ToScreenY(py + 10.0f);
    DrawShadowText(dl, ImVec2(tx, ty), colTitle, title, 2);
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

  // Icon display size (preserve 20:28 aspect ratio = 5:7)
  static constexpr float ICON_DISP_W = 46.0f;
  static constexpr float ICON_DISP_H = 64.0f; // 46 * 28/20 = 64.4

  float gridStartY = py + TITLE_H;

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

    ImU32 cellBg =
        cellHovered ? IM_COL32(40, 45, 65, 220) : IM_COL32(25, 28, 40, 200);
    dl->AddRectFilled(cMin, cMax, cellBg, 4.0f);
    dl->AddRect(cMin, cMax, IM_COL32(50, 55, 75, 150), 4.0f);

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
        iconAlpha = 100;
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

    // Skill name centered below icon
    {
      ImU32 nameCol = learned ? colValue : colDim;
      ImVec2 nsz = ImGui::CalcTextSize(skill.name);
      float nameY = cy + 6.0f + ICON_DISP_H + 4.0f;
      // Clamp text to cell width
      float nx = c.ToScreenX(cx + CELL_W * 0.5f) - nsz.x * 0.5f;
      float cellLeft = c.ToScreenX(cx + 2.0f);
      if (nx < cellLeft)
        nx = cellLeft;
      float ny = c.ToScreenY(nameY);
      dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 180), skill.name);
      dl->AddText(ImVec2(nx, ny), nameCol, skill.name);
    }

    // Level req badge (bottom-right corner of cell, only if > 1)
    if (skill.levelReq > 1) {
      snprintf(buf, sizeof(buf), "Lv%d", skill.levelReq);
      bool meetsReq = *s_ctx->serverLevel >= skill.levelReq;
      ImU32 reqCol = meetsReq ? colGreen : colRed;
      if (!learned)
        reqCol = (reqCol & 0x00FFFFFF) | 0x64000000;
      ImVec2 rsz = ImGui::CalcTextSize(buf);
      float rx = c.ToScreenX(cx + CELL_W - 3.0f) - rsz.x;
      float ry = c.ToScreenY(cy + CELL_H - 16.0f);
      dl->AddText(ImVec2(rx + 1, ry + 1), IM_COL32(0, 0, 0, 180), buf);
      dl->AddText(ImVec2(rx, ry), reqCol, buf);
    }

    // Tooltip on hover
    if (cellHovered) {
      float tw = 200;
      float lineH = 18;
      int numLines = 5;
      float th = lineH * numLines + 10;

      BeginPendingTooltip(tw, th);
      AddPendingTooltipLine(colTitle, skill.name);

      snprintf(buf, sizeof(buf), "%s Cost: %d", resourceLabel,
               skill.resourceCost);
      AddPendingTooltipLine(IM_COL32(100, 180, 255, 255), buf);

      snprintf(buf, sizeof(buf), "Damage: +%d", skill.damageBonus);
      AddPendingTooltipLine(IM_COL32(255, 200, 100, 255), buf);

      snprintf(buf, sizeof(buf), "Level Req: %d", skill.levelReq);
      bool meetsReq = *s_ctx->serverLevel >= skill.levelReq;
      AddPendingTooltipLine(meetsReq ? colGreen : colRed, buf);

      AddPendingTooltipLine(colLabel, skill.desc);

      if (!learned) {
        AddPendingTooltipLine(IM_COL32(255, 150, 50, 255), "(Not learned)");
      }
    }
  }

  // Footer -- centered
  int learnedCount =
      s_ctx->learnedSkills ? (int)s_ctx->learnedSkills->size() : 0;
  snprintf(buf, sizeof(buf), "Learned: %d / %d", learnedCount, skillCount);
  {
    ImVec2 fsz = ImGui::CalcTextSize(buf);
    float fx = c.ToScreenX(px + pw * 0.5f) - fsz.x * 0.5f;
    float fy = c.ToScreenY(py + ph - FOOTER_H);
    dl->AddText(ImVec2(fx + 1, fy + 1), IM_COL32(0, 0, 0, 180), buf);
    dl->AddText(ImVec2(fx, fy), colLabel, buf);
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

  // Minimal layout: narrow bar centered on screen
  ImVec2 disp = ImGui::GetIO().DisplaySize;
  float barW = 220.0f;
  float barH = 14.0f;
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
