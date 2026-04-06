#include "GroundItemRenderer.hpp"
#include "InventoryUI.hpp"
#include "ItemDatabase.hpp"
#include "ItemModelManager.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace FloatingDamageRenderer {

void Spawn(const glm::vec3 &pos, int damage, uint8_t type, FloatingDamage *pool,
           int poolSize) {
  for (int i = 0; i < poolSize; ++i) {
    auto &d = pool[i];
    if (!d.active) {
      // Main 5.2: CreatePoint — spawn 140 units above target with random XZ
      // offset
      d.worldPos =
          pos + glm::vec3(((rand() % 40) - 20), 140.0f, ((rand() % 40) - 20));
      d.damage = damage;
      d.type = type;
      d.gravity = 10.0f; // Main 5.2: initial upward velocity
      d.yOffset = 0.0f;
      // Main 5.2: critical/excellent start at scale 50, normal at 15
      d.scale = (type == 2 || type == 3) ? 50.0f : 15.0f;
      d.active = true;
      return;
    }
  }
}

void UpdateAndRender(FloatingDamage *pool, int poolSize, float deltaTime,
                     ImDrawList *dl, ImFont *font, const glm::mat4 &view,
                     const glm::mat4 &proj, int winW, int winH) {
  glm::mat4 vp = proj * view;
  float ticks = deltaTime * 25.0f; // Convert to 25fps tick-based

  for (int i = 0; i < poolSize; ++i) {
    auto &d = pool[i];
    if (!d.active)
      continue;

    // Main 5.2: MovePoints (ZzzEffectPoint.cpp)
    // Gravity-based vertical motion: position += gravity, gravity -= 0.3/tick
    d.yOffset += d.gravity * ticks;
    d.gravity -= 0.3f * ticks;

    if (d.gravity <= 0.0f) {
      d.active = false;
      continue;
    }

    // Scale decay — crits/excellent stay larger (min 22 vs normal 15)
    d.scale -= 5.0f * ticks;
    float minScale = (d.type == 2 || d.type == 3) ? 22.0f : 15.0f;
    if (d.scale < minScale)
      d.scale = minScale;

    // Current position
    glm::vec3 pos = d.worldPos + glm::vec3(0, d.yOffset, 0);

    // Project to screen
    glm::vec4 clip = vp * glm::vec4(pos, 1.0f);
    if (clip.w <= 0.0f)
      continue;
    float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
    float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

    // Main 5.2: alpha = gravity * 0.4 (starts at 4.0, clamped to 1.0, fades to
    // 0)
    float alpha = std::min(d.gravity * 0.4f, 1.0f);

    // WoW-style scrolling combat text colors
    ImU32 col;
    const char *text;
    char buf[16];
    int a = (int)(alpha * 255);
    if (d.type == 7) {
      // Miss: gray text (WoW: white "Miss")
      col = IM_COL32(180, 180, 180, a);
      text = "MISS";
    } else if (d.type == 9) {
      // XP gain: purple-gold (WoW-style)
      snprintf(buf, sizeof(buf), "+%d XP", d.damage);
      text = buf;
      col = IM_COL32(220, 180, 255, a);
    } else if (d.type == 10) {
      // Healing: green (WoW: bright green)
      snprintf(buf, sizeof(buf), "+%d", d.damage);
      text = buf;
      col = IM_COL32(60, 255, 60, a);
    } else {
      snprintf(buf, sizeof(buf), "%d", d.damage);
      text = buf;
      if (d.type == 8)
        col = IM_COL32(255, 0, 0, a);       // Incoming damage: red
      else if (d.type == 2)
        col = IM_COL32(255, 255, 0, a);      // Critical: yellow (WoW)
      else if (d.type == 3)
        col = IM_COL32(255, 200, 0, a);      // Excellent: gold (WoW)
      else if (d.type == 4)
        col = IM_COL32(30, 255, 30, a);      // Poison DoT: green (WoW nature)
      else
        col = IM_COL32(255, 255, 255, a);    // Normal hit: white (WoW)
    }

    // Draw with shadow
    // Main 5.2: scale 15 → 20px base, scale 50 → ~67px for crits
    float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;
    float fontSize = d.scale * (20.0f / 15.0f) * uiScale;
    ImVec2 tpos(sx, sy);
    dl->AddText(font, fontSize, ImVec2(tpos.x + 1, tpos.y + 1),
                IM_COL32(0, 0, 0, (int)(alpha * 200)), text);
    dl->AddText(font, fontSize, tpos, col, text);
  }
}

} // namespace FloatingDamageRenderer

namespace GroundItemRenderer {

void GetItemRestingAngle(int defIndex, glm::vec3 &angle, float &scale,
                         float &heightBoost) {
  // Main 5.2 ItemAngle() — ZzzObject.cpp:5437-5686
  // + ItemObjectAttribute() — ZzzObject.cpp:5286
  // Default: Angle = (0, 0, -45), Scale = 0.8
  angle = glm::vec3(0.0f, 0.0f, -45.0f);
  scale = 0.8f; // Main 5.2 ItemObjectAttribute default
  heightBoost = 0.0f;

  if (defIndex == -1) { // Zen
    angle = glm::vec3(0, 0, 0);
    scale = 1.6f;
    return;
  }

  int category = 0;
  int index = 0;

  auto &itemDefs = ItemDatabase::GetItemDefs();
  auto it = itemDefs.find(defIndex);
  if (it != itemDefs.end()) {
    category = it->second.category;
    index = it->second.itemIndex;
  } else {
    category = defIndex / 32;
    index = defIndex % 32;
  }

  // Weapon angles: lay flat on ground with blade visible (not buried)
  // heightBoost lifts the model above terrain so it doesn't clip
  if (category >= 0 && category <= 1) { // Swords, Axes
    angle = glm::vec3(0.0f, 0.0f, -45.0f);
    heightBoost = 30.0f;
    if (category == 0 && index == 19) scale = 0.7f;
  } else if (category == 2) { // Maces
    angle = glm::vec3(0.0f, 270.0f, -45.0f);
    heightBoost = 30.0f;
  } else if (category == 3) { // Spears
    angle = glm::vec3(0.0f, 0.0f, -45.0f);
    scale = 0.7f;
    heightBoost = 30.0f;
  } else if (category == 4) { // Bows/Crossbows
    angle = glm::vec3(0.0f, 0.0f, -45.0f);
    heightBoost = 25.0f;
  } else if (category == 5) { // Staffs
    angle = glm::vec3(0.0f, 270.0f, -45.0f);
    scale = 0.7f;
    heightBoost = 30.0f;
  } else if (category == 6) { // Shields — flat on ground, tilted
    angle = glm::vec3(0.0f, 270.0f, 225.0f);
    heightBoost = 15.0f;
  } else if (category == 7) { // Helms — standing upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    heightBoost = 10.0f;
  } else if (category >= 8 && category <= 9) { // Armor/Pants — lying flat
    angle = glm::vec3(270.0f, 0.0f, -45.0f);
    heightBoost = 15.0f;
  } else if (category == 10) { // Gloves — lying flat
    angle = glm::vec3(270.0f, 0.0f, -45.0f);
    heightBoost = 25.0f;
  } else if (category == 11) { // Boots — standing upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    heightBoost = 10.0f;
  } else if (category == 12) { // Wings/Misc — Orbs standing upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    scale = 0.6f;
    heightBoost = 10.0f;
  } else if (category == 13) { // Accessories — Rings, Pendants
    if (index < 10) { // Rings — flat
      angle = glm::vec3(90.0f, 0.0f, -45.0f);
      scale = 0.5f;
    } else { // Pendants — standing
      angle = glm::vec3(0.0f, 0.0f, 0.0f);
      scale = 0.5f;
      heightBoost = 8.0f;
    }
  } else if (category == 14) { // Potions, Jewels, Ale
    if (index <= 8) {
      // Potions/Ale: standing upright
      angle = glm::vec3(0.0f, 0.0f, 0.0f);
      scale = 0.7f;
      heightBoost = 10.0f;
    } else if (index == 13 || index == 14 || index == 16 || index == 22) {
      // Jewels: standing upright
      angle = glm::vec3(0.0f, 0.0f, 0.0f);
      scale = 0.8f;
      heightBoost = 15.0f;
    } else {
      // Other consumables: standing
      angle = glm::vec3(0.0f, 0.0f, 0.0f);
      scale = 0.8f;
      heightBoost = 10.0f;
    }
  } else if (category == 15) { // Scrolls/Skill items — standing upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    scale = 0.9f;
    heightBoost = 15.0f;
  }
}

void UpdatePhysics(GroundItem &gi, float terrainHeight) {
  if (gi.isResting) {
    float heightBoost = 0.0f;
    glm::vec3 tmpAngle;
    float tmpScale;
    GetItemRestingAngle(gi.defIndex, tmpAngle, tmpScale, heightBoost);
    gi.position.y = terrainHeight + 0.5f + heightBoost;
    return;
  }

  // Apply gravity
  gi.position.y += gi.gravity * 0.5f;
  gi.gravity -= 1.0f;

  // Items keep their resting angle at all times (no tumble during fall)

  // Floor check (bounce)
  if (gi.position.y <= terrainHeight + 0.5f) {
    gi.position.y = terrainHeight + 0.5f;

    // Bounce
    if (abs(gi.gravity) > 2.0f) {
      gi.gravity = -gi.gravity * 0.4f; // Bounce with damping
    } else {
      gi.gravity = 0;
      gi.isResting = true;
      // Snap to resting angle on landing
      float restScale, heightBoost;
      GetItemRestingAngle(gi.defIndex, gi.angle, restScale, heightBoost);
      gi.position.y += heightBoost;
    }
  }
}

// Main 5.2 RandomTable — deterministic pile layout (same values every frame)
static const int s_randomTable[100] = {
    73, 17, 94, 22, 56, 81, 39, 67, 10, 48,
    85, 31,  5, 62, 90, 14, 77, 43, 28, 59,
    96,  8, 53, 36, 71, 19, 88, 45,  2, 64,
    50, 83, 27, 12, 70, 41, 97, 33, 58, 79,
    15, 92, 24, 66,  7, 54, 38, 87, 46, 75,
    20, 61, 99,  3, 49, 82, 34, 69, 11, 57,
    80, 26, 44, 93, 16, 72, 37, 55, 86,  1,
    63, 47, 29, 91, 68, 13, 78, 42, 98, 23,
    52, 35, 84,  9, 74, 60, 30, 40, 18, 95,
    65,  6, 51, 76, 21, 89, 32, 58, 25, 46
};

void RenderZenPile(int quantity, glm::vec3 pos, glm::vec3 angle, float scale,
                   const glm::mat4 &view, const glm::mat4 &proj) {
  // Main 5.2 coin count formula: sqrt(zen) / 2, clamped [3, 80]
  int coinCount = (int)sqrtf((float)quantity) / 2;
  if (coinCount < 3)
    coinCount = 3;
  if (coinCount > 80)
    coinCount = 80;

  // Derive a stable item index from position (consistent across frames)
  int itemIdx = (int)(pos.x * 7.0f + pos.z * 13.0f) & 0x7FFFFFFF;

  // Render first coin at pile center
  ItemModelManager::RenderItemWorld("Gold01.bmd", pos, view, proj, scale,
                                    angle, -1);

  // Main 5.2 radial distribution: each coin placed at random angle + distance
  for (int j = 1; j < coinCount; ++j) {
    // Deterministic random angle (0-359 degrees around Y axis)
    float randAngle = (float)(s_randomTable[(itemIdx * 20 + j) % 100] % 360);
    float angleRad = glm::radians(randAngle);

    // Deterministic random distance from center (0 to coinCount+19 units)
    float dist = (float)(s_randomTable[(itemIdx + j) % 100] % (coinCount + 20));

    // Convert polar to XZ offset
    glm::vec3 offset;
    offset.x = cosf(angleRad) * dist;
    offset.z = sinf(angleRad) * dist;
    offset.y = 0.0f;

    // Per-coin rotation for visual variety
    glm::vec3 coinAngle = angle;
    coinAngle.y = randAngle;

    ItemModelManager::RenderItemWorld("Gold01.bmd", pos + offset, view, proj,
                                      scale, coinAngle, -1);
  }
}

void RenderModels(GroundItem *items, int maxItems, float deltaTime,
                  const glm::mat4 &view, const glm::mat4 &proj,
                  float (*getTerrainHeight)(float, float)) {
  for (int i = 0; i < maxItems; ++i) {
    auto &gi = items[i];
    if (!gi.active)
      continue;

    float terrainH = getTerrainHeight(gi.position.x, gi.position.z);
    UpdatePhysics(gi, terrainH);

    const char *modelFile = ItemDatabase::GetDropModelName(gi.defIndex);
    if (modelFile) {
      ItemModelManager::RenderItemWorld(modelFile, gi.position, view, proj,
                                        gi.scale, gi.angle, gi.defIndex);
    } else if (gi.defIndex == -1) {
      RenderZenPile(gi.quantity, gi.position, gi.angle, gi.scale, view, proj);
    }
  }
}

void UpdateSparkleTimers(GroundItem *items, int maxItems, float deltaTime,
                         std::vector<glm::vec3> &outPositions) {
  for (int i = 0; i < maxItems; ++i) {
    auto &gi = items[i];
    if (!gi.active || !gi.isResting)
      continue;

    gi.sparkleTimer += deltaTime;
    if (gi.sparkleTimer >= 1.92f) {
      gi.sparkleTimer -= 1.92f;
      // Main 5.2: BITMAP_FLARE sparkle at item position +20 Y
      outPositions.push_back(gi.position + glm::vec3(0, 20.0f, 0));
    }
  }
}

void RenderLabels(GroundItem *items, int maxItems, ImDrawList *dl, ImFont *font,
                  const glm::mat4 &view, const glm::mat4 &proj, int winW,
                  int winH, const glm::vec3 &camPos, int hoveredGroundItem,
                  const std::map<int16_t, ClientItemDefinition> &itemDefs) {
  glm::mat4 vp = proj * view;
  float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;

  for (int i = 0; i < maxItems; ++i) {
    auto &gi = items[i];
    if (!gi.active)
      continue;

    // Main 5.2: Position[2] += 30.f — raise label above item
    // Use 50 to clear grass geometry
    glm::vec3 labelPos = gi.position + glm::vec3(0, 50.0f, 0);
    glm::vec4 clip = vp * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0.0f)
      continue;
    float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
    float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

    float dist = glm::length(gi.position - camPos);
    if (dist > 1500.0f)
      continue;

    const char *name = ItemDatabase::GetDropName(gi.defIndex);
    char label[128];
    if (gi.defIndex == -1) {
      snprintf(label, sizeof(label), "%d Zen", gi.quantity);
    } else {
      // Build label: "Name +Level" with option indicators
      // Additional Option shown as (Opt+N) to avoid confusion with enhancement level
      char optSuffix[48] = {};
      if (gi.optionFlags != 0) {
        char *p = optSuffix;
        if ((gi.optionFlags & 0x80) && gi.defIndex / 32 == 4)
          p += snprintf(p, sizeof(optSuffix) - (p - optSuffix), " +Skill");
        if (gi.optionFlags & 0x40)
          p += snprintf(p, sizeof(optSuffix) - (p - optSuffix), " +Luck");
        int addLvl = gi.optionFlags & 0x07;
        if (addLvl > 0) {
          uint8_t cat = (uint8_t)(gi.defIndex / 32);
          const char *optType = (cat <= 5) ? "Dmg" : "Def";
          snprintf(p, sizeof(optSuffix) - (p - optSuffix), " (+%d %s)", addLvl * 4, optType);
        }
      }
      if (gi.itemLevel > 0)
        snprintf(label, sizeof(label), "%s +%d%s", name, gi.itemLevel, optSuffix);
      else
        snprintf(label, sizeof(label), "%s%s", name, optSuffix);
    }

    float labelFs = 14.0f * uiScale;
    ImVec2 ts = font->CalcTextSizeA(labelFs, FLT_MAX, 0, label);
    float tx = sx - ts.x * 0.5f, ty = sy - ts.y * 0.5f;

    bool isHovered = (i == hoveredGroundItem);

    // Main 5.2 color scheme: gold/blue/white based on item quality
    ImU32 col;
    if (gi.defIndex == -1) {
      col = IM_COL32(255, 215, 0, 255); // Zen gold
    } else if (gi.optionFlags != 0) {
      col = IM_COL32(100, 180, 255, 255); // Blue for optioned items
    } else {
      col = IM_COL32(230, 230, 230, 255); // White/light gray (Main 5.2 default)
    }

    if (isHovered)
      col = IM_COL32(255, 255, 100, 255); // Bright yellow on hover

    // Main 5.2: SetBgColor(0,0,0,255) — dark background rectangle behind label
    float padX = 4.0f * uiScale;
    float padY = 2.0f * uiScale;
    ImU32 bgCol = isHovered ? IM_COL32(0, 0, 0, 200) : IM_COL32(0, 0, 0, 160);
    dl->AddRectFilled(ImVec2(tx - padX, ty - padY),
                      ImVec2(tx + ts.x + padX, ty + ts.y + padY), bgCol);

    // 1px black outline for readability on any background
    ImU32 outlineCol = IM_COL32(0, 0, 0, 220);
    dl->AddText(font, labelFs, ImVec2(tx + 1, ty), outlineCol, label);
    dl->AddText(font, labelFs, ImVec2(tx - 1, ty), outlineCol, label);
    dl->AddText(font, labelFs, ImVec2(tx, ty + 1), outlineCol, label);
    dl->AddText(font, labelFs, ImVec2(tx, ty - 1), outlineCol, label);

    // Foreground text
    dl->AddText(font, labelFs, ImVec2(tx, ty), col, label);

    // Hover tooltip — reuse full inventory tooltip system
    if (isHovered && gi.defIndex != -1) {
      InventoryUI::AddPendingItemTooltip(gi.defIndex, gi.itemLevel, 0,
                                         gi.optionFlags);
    }
  }
}

int PickLabel(GroundItem *items, int maxItems, float mouseX, float mouseY,
              ImFont *font, const glm::mat4 &view, const glm::mat4 &proj,
              int winW, int winH, const glm::vec3 &camPos) {
  glm::mat4 vp = proj * view;
  float uiScale = ImGui::GetIO().DisplaySize.y / 768.0f;

  for (int i = 0; i < maxItems; ++i) {
    auto &gi = items[i];
    if (!gi.active)
      continue;

    glm::vec3 labelPos = gi.position + glm::vec3(0, 50.0f, 0);
    glm::vec4 clip = vp * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0.0f)
      continue;
    float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
    float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

    float dist = glm::length(gi.position - camPos);
    if (dist > 1500.0f)
      continue;

    const char *name = ItemDatabase::GetDropName(gi.defIndex);
    char label[128];
    if (gi.defIndex == -1)
      snprintf(label, sizeof(label), "%d Zen", gi.quantity);
    else if (gi.itemLevel > 0)
      snprintf(label, sizeof(label), "%s +%d", name, gi.itemLevel);
    else
      snprintf(label, sizeof(label), "%s", name);

    float labelFs = 14.0f * uiScale;
    ImVec2 ts = font->CalcTextSizeA(labelFs, FLT_MAX, 0, label);
    float padX = 6.0f * uiScale, padY = 4.0f * uiScale;
    float tx = sx - ts.x * 0.5f, ty = sy - ts.y * 0.5f;

    if (mouseX >= tx - padX && mouseX <= tx + ts.x + padX &&
        mouseY >= ty - padY && mouseY <= ty + ts.y + padY) {
      return i;
    }
  }
  return -1;
}

void RenderShadows(GroundItem *items, int maxItems, const glm::mat4 &view,
                   const glm::mat4 &proj) {
}

} // namespace GroundItemRenderer
