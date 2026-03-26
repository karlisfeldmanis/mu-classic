#ifndef MU_INVENTORY_UI_HPP
#define MU_INVENTORY_UI_HPP

#include "ClientTypes.hpp"
#include "UICoords.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;
struct GLFWwindow;
class HeroCharacter;
class ServerConnection;

// Render job for deferred 3D item rendering on top of UI
struct ItemRenderJob {
  std::string modelFile;
  int16_t defIndex;
  int x, y, w, h;
  bool hovered;
  uint8_t itemLevel = 0;
};

// Equipment slot layout rect (virtual coords)
struct EquipSlotRect {
  int slot;
  float x, y, w, h;
};

// Context struct: pointers to shared state owned by main.cpp
struct InventoryUIContext {
  ClientInventoryItem *inventory; // [INVENTORY_SLOTS]
  ClientEquipSlot *equipSlots;    // [12]
  uint32_t *zen;
  bool *syncDone;
  bool *showCharInfo;
  bool *showInventory;
  bool *showSkillWindow;
  bool *showQuestLog;
  bool *showMapWindow;
  int *currentMapId;
  std::vector<uint8_t> *learnedSkills;
  int16_t *potionBar; // [4]
  int8_t *skillBar;   // [10]
  float *potionCooldown;
  bool *shopOpen;
  std::vector<ShopItem> *shopItems;
  bool *isLearningSkill;
  float *learnSkillTimer;
  uint8_t *learningSkillId;
  int8_t *rmcSkillId;

  // Server stats (written by ClientPacketHandler)
  int *serverLevel, *serverStr, *serverDex, *serverVit, *serverEne;
  int *serverLevelUpPoints, *serverDefense, *serverAttackSpeed,
      *serverMagicSpeed;
  int *serverHP, *serverMaxHP, *serverMP, *serverMaxMP;
  int *serverAG; // AG for DK (separate from mana)
  int64_t *serverXP;

  // Teleport / warp cast
  bool *teleportingToTown;
  float *teleportTimer;
  float teleportCastTime;
  int *warpTargetMapId;   // Target for map warp (-1 = town recall)
  int *warpTargetGX;
  int *warpTargetGZ;
  float learnSkillDuration;

  // Mount toggle (M key)
  bool *mountToggling;
  float *mountToggleTimer;
  float mountToggleTime;

  int *heroCharacterId;
  char *characterName;
  HeroCharacter *hero;
  ServerConnection *server;
  UICoords *hudCoords;
  ImFont *fontDefault;
  ImFont *fontBold;     // Decorative title font (Cinzel / WoW-style)
  ImFont *fontRegion;   // Large font for region name display
};

namespace InventoryUI {

void Init(const InventoryUIContext &ctx);

// Inventory helpers (called from ClientPacketHandler callbacks too)
void SetBagItem(int slot, int16_t defIdx, uint8_t qty, uint8_t lvl);
void ClearBagItem(int slot);
void ConsumeQuickSlotItem(int slotIndex);
void RecalcEquipmentStats();

// Panel scaling
void UpdatePanelScale(int windowHeight);

// Panel rendering
void RenderCharInfoPanel(ImDrawList *dl, const UICoords &c);
void RenderInventoryPanel(ImDrawList *dl, const UICoords &c);
void RenderShopPanel(ImDrawList *dl, const UICoords &c);
void RenderSkillPanel(ImDrawList *dl, const UICoords &c);
void RenderMapPanel(ImDrawList *dl, const UICoords &c);
void RenderRmcSlot(ImDrawList *dl, float screenX, float screenY, float size);
void RenderQuickbar(ImDrawList *dl, const UICoords &c);
void RenderSkillDragCursor(ImDrawList *dl);

// Tooltip
void AddPendingItemTooltip(int16_t defIndex, int itemLevel,
                           uint32_t shopBuyPrice = 0, uint8_t optionFlags = 0);
void FlushPendingTooltip();
bool HasPendingTooltip();
void ResetPendingTooltip();

// Click handling (returns true if click was consumed by a panel)
bool HandlePanelClick(float vx, float vy);
bool HandlePanelRightClick(float vx, float vy);
void HandlePanelMouseUp(GLFWwindow *window, float vx, float vy);

// Drag state queries
bool IsDragging();
int16_t GetDragDefIndex();

// Render queue (for deferred 3D item rendering)
const std::vector<ItemRenderJob> &GetRenderQueue();
void ClearRenderQueue();
void AddRenderJob(const ItemRenderJob &job);

// Deferred overlays (quantity labels rendered after 3D items)
bool HasDeferredOverlays();
void FlushDeferredOverlays();

// Deferred cooldowns (potion cooldown overlay rendered after 3D items)
bool HasDeferredCooldowns();
void FlushDeferredCooldowns();

// Load slot background textures
void LoadSlotBackgrounds(const std::string &dataPath);

// Panel geometry queries
float GetCharInfoPanelX();
float GetInventoryPanelX();
float GetShopPanelX();
bool IsPointInPanel(float vx, float vy, float panelX);
bool IsCharInfoPointInPanel(float vx, float vy);

// Skill queries
int GetSkillResourceCost(uint8_t skillId);
int GetSkillAGCost(uint8_t skillId);

// Cast bar (centered, for teleport + skill learning)
void RenderCastBar(ImDrawList *dl);

// Notifications
void UpdateAndRenderNotification(float deltaTime);
void ShowNotification(const char *msg);
bool HasActiveNotification();

// Region name display (Main 5.2: CUIMapName)
void ShowRegionName(const char *name);
void UpdateAndRenderRegionName(float deltaTime);
bool HasActiveRegionName();

} // namespace InventoryUI

#endif // MU_INVENTORY_UI_HPP
