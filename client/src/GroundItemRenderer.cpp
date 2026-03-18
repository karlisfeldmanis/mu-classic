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

    // Main 5.2: scale decay — 5.0 per tick, minimum 15.0
    d.scale -= 5.0f * ticks;
    if (d.scale < 15.0f)
      d.scale = 15.0f;

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
    float uiScale = ImGui::GetIO().FontGlobalScale;
    float fontSize = d.scale * (20.0f / 15.0f) * uiScale;
    ImVec2 tpos(sx, sy);
    dl->AddText(font, fontSize, ImVec2(tpos.x + 1, tpos.y + 1),
                IM_COL32(0, 0, 0, (int)(alpha * 200)), text);
    dl->AddText(font, fontSize, tpos, col, text);
  }
}

} // namespace FloatingDamageRenderer

namespace GroundItemRenderer {

void GetItemRestingAngle(int defIndex, glm::vec3 &angle, float &scale) {
  angle = glm::vec3(90.0f, 0.0f, 0.0f); // Default: lay flat on ground
  scale = 1.0f;

  if (defIndex == -1) { // Zen
    angle = glm::vec3(0, 0, 0);
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

  // All weapons lay flat (90 X tilt) -- vary Y for visual interest
  if (category == 0) { // Swords -- diagonal
    angle = glm::vec3(90.0f, 45.0f, 0.0f);
    scale = 1.0f;
    if (index == 19)
      scale = 0.7f;           // Divine Sword
  } else if (category == 1) { // Axes
    angle = glm::vec3(90.0f, 30.0f, 0.0f);
  } else if (category == 2) { // Maces
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 3) { // Spears -- longer, lay along Y
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 4) { // Bows/Crossbows
    angle = glm::vec3(90.0f, 90.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 5) { // Staffs
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 6) { // Shields -- lay face-up
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
    scale = 0.9f;
  } else if (category == 7 || category == 8) { // Helms / Armor
    angle = glm::vec3(90.0f, 0.0f, 0.0f);
  } else if (category == 14) { // Potions -- stand upright
    angle = glm::vec3(0.0f, 0.0f, 0.0f);
    scale = 0.6f;
  }
}

void UpdatePhysics(GroundItem &gi, float terrainHeight) {
  if (gi.isResting) {
    gi.position.y = terrainHeight + 0.5f; // Snap to ground
    return;
  }

  // Apply gravity
  gi.position.y += gi.gravity * 0.5f; // Integrate velocity (using Y as UP)
  gi.gravity -= 1.0f;                 // Gravity accel

  // Floor check (bounce)
  if (gi.position.y <= terrainHeight + 0.5f) {
    gi.position.y = terrainHeight + 0.5f;

    // Bounce
    if (abs(gi.gravity) > 2.0f) {
      gi.gravity = -gi.gravity * 0.4f; // Bounce with damping
    } else {
      gi.gravity = 0;
      gi.isResting = true;
    }
  }
}

void RenderZenPile(int quantity, glm::vec3 pos, glm::vec3 angle, float scale,
                   const glm::mat4 &view, const glm::mat4 &proj) {
  // Procedural pile based on quantity
  int coinCount = (int)sqrtf((float)quantity) / 2;
  if (coinCount < 3)
    coinCount = 3;
  if (coinCount > 20)
    coinCount = 20;

  // Seed rand with quantity to keep pile consistent per frame
  srand(quantity + (int)pos.x);

  for (int i = 0; i < coinCount; ++i) {
    glm::vec3 offset;
    offset.x = (rand() % 40) - 20.0f;
    offset.z = (rand() % 40) - 20.0f;
    offset.y = 0;

    float rotY = (float)(rand() % 360);

    // Simple stacking effect check
    if (i > 5)
      offset.y += 2.0f;
    if (i > 10)
      offset.y += 4.0f;

    ItemModelManager::RenderItemWorld("Gold01.bmd", pos + offset, view, proj,
                                      scale);
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

void RenderLabels(GroundItem *items, int maxItems, ImDrawList *dl, ImFont *font,
                  const glm::mat4 &view, const glm::mat4 &proj, int winW,
                  int winH, const glm::vec3 &camPos, int hoveredGroundItem,
                  const std::map<int16_t, ClientItemDefinition> &itemDefs) {
  glm::mat4 vp = proj * view;
  for (int i = 0; i < maxItems; ++i) {
    auto &gi = items[i];
    if (!gi.active)
      continue;

    glm::vec3 labelPos = gi.position + glm::vec3(0, 15.0f, 0);
    glm::vec4 clip = vp * glm::vec4(labelPos, 1.0f);
    if (clip.w <= 0.0f)
      continue;
    float sx = ((clip.x / clip.w) * 0.5f + 0.5f) * winW;
    float sy = ((1.0f - (clip.y / clip.w)) * 0.5f) * winH;

    float dist = glm::length(gi.position - camPos);
    if (dist > 1500.0f)
      continue;

    const char *name = ItemDatabase::GetDropName(gi.defIndex);
    char label[64];
    if (gi.defIndex == -1)
      snprintf(label, sizeof(label), "%d Zen", gi.quantity);
    else if (gi.itemLevel > 0)
      snprintf(label, sizeof(label), "%s +%d", name, gi.itemLevel);
    else
      snprintf(label, sizeof(label), "%s", name);

    float uiScale = ImGui::GetIO().FontGlobalScale;
    float labelFs = 13.0f * uiScale;
    ImVec2 ts = font->CalcTextSizeA(labelFs, FLT_MAX, 0, label);
    float tx = sx - ts.x * 0.5f, ty = sy - ts.y * 0.5f;

    bool isHovered = (i == hoveredGroundItem);

    ImU32 col = gi.defIndex == -1 ? IM_COL32(255, 215, 0, 220)
                                  : IM_COL32(180, 255, 180, 220);

    if (isHovered) {
      col = IM_COL32(255, 255, 255, 255);
      dl->AddText(font, labelFs, ImVec2(tx + 2, ty + 1), IM_COL32(0, 0, 0, 200),
                  label);
      dl->AddText(font, labelFs, ImVec2(tx - 1, ty - 1), IM_COL32(0, 0, 0, 200),
                  label);
    }

    dl->AddText(font, labelFs, ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 160),
                label);
    dl->AddText(font, labelFs, ImVec2(tx, ty), col, label);

    // Hover tooltip — reuse full inventory tooltip system
    if (isHovered && gi.defIndex != -1) {
      InventoryUI::AddPendingItemTooltip(gi.defIndex, gi.itemLevel);
    }
  }
}

void RenderShadows(GroundItem *items, int maxItems, const glm::mat4 &view,
                   const glm::mat4 &proj) {
}

} // namespace GroundItemRenderer
