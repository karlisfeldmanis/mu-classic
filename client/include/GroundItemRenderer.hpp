#ifndef MU_GROUND_ITEM_RENDERER_HPP
#define MU_GROUND_ITEM_RENDERER_HPP

#include "ClientTypes.hpp"
#include <glm/glm.hpp>
#include <map>
#include <vector>

struct ImDrawList;
struct ImFont;
struct ClientItemDefinition;

// Floating damage number display state (Main 5.2: ZzzEffectPoint.cpp)
struct FloatingDamage {
  glm::vec3 worldPos; // World position where damage occurred
  int damage;
  uint8_t type;  // 0=normal(white), 2=critical(yellow), 3=excellent(gold),
                 // 4=poison(green), 7=miss(gray), 8=incoming(red), 9=XP, 10=heal
  float gravity; // Main 5.2: starts at 10, decreases 0.3/tick. Alpha = gravity*0.4
  float yOffset; // Accumulated vertical displacement
  float scale;   // Main 5.2: 50 for crits (decays to 15), 15 for normal
  bool active;
};

static constexpr int MAX_FLOATING_DAMAGE = 32;

namespace FloatingDamageRenderer {
void Spawn(const glm::vec3 &pos, int damage, uint8_t type,
           FloatingDamage *pool, int poolSize);
void UpdateAndRender(FloatingDamage *pool, int poolSize, float deltaTime,
                     ImDrawList *dl, ImFont *font,
                     const glm::mat4 &view, const glm::mat4 &proj,
                     int winW, int winH);
} // namespace FloatingDamageRenderer

namespace GroundItemRenderer {
void GetItemRestingAngle(int defIndex, glm::vec3 &angle, float &scale,
                         float &heightBoost);
void UpdatePhysics(GroundItem &gi, float terrainHeight);
void RenderZenPile(int quantity, glm::vec3 pos, glm::vec3 angle, float scale,
                   const glm::mat4 &view, const glm::mat4 &proj);
// Render 3D models + physics update for all active ground items
void RenderModels(GroundItem *items, int maxItems, float deltaTime,
                  const glm::mat4 &view, const glm::mat4 &proj,
                  float (*getTerrainHeight)(float, float));
// Render floating labels + tooltips for ground items
void RenderLabels(GroundItem *items, int maxItems, ImDrawList *dl, ImFont *font,
                  const glm::mat4 &view, const glm::mat4 &proj,
                  int winW, int winH, const glm::vec3 &camPos,
                  int hoveredGroundItem,
                  const std::map<int16_t, ClientItemDefinition> &itemDefs);
// Update sparkle timers; returns positions needing FLARE particle spawn
void UpdateSparkleTimers(GroundItem *items, int maxItems, float deltaTime,
                         std::vector<glm::vec3> &outPositions);
// Render blob shadows for all active ground items (call before RenderModels)
void RenderShadows(GroundItem *items, int maxItems, const glm::mat4 &view,
                   const glm::mat4 &proj);
// Pick ground item by label click (screen-space hit test)
int PickLabel(GroundItem *items, int maxItems, float mouseX, float mouseY,
              ImFont *font, const glm::mat4 &view, const glm::mat4 &proj,
              int winW, int winH, const glm::vec3 &camPos);
} // namespace GroundItemRenderer

#endif // MU_GROUND_ITEM_RENDERER_HPP
