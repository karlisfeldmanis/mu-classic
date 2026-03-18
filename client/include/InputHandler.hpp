#ifndef MU_INPUT_HANDLER_HPP
#define MU_INPUT_HANDLER_HPP

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;
class Camera;
class HeroCharacter;
class ServerConnection;
class ClickEffect;
class NpcManager;
class MonsterManager;
class ObjectRenderer;
struct GroundItem;
struct UICoords;

struct ClientEquipSlot;

struct InputContext {
  HeroCharacter *hero;
  Camera *camera;
  ClickEffect *clickEffect;
  ServerConnection *server;
  MonsterManager *monsterMgr;
  NpcManager *npcMgr;
  ObjectRenderer *objectRenderer;
  GroundItem *groundItems;
  ClientEquipSlot *equipSlots; // Equipment slots [12] for ammo validation
  int maxGroundItems;
  UICoords *hudCoords;
  bool *showCharInfo;
  bool *showInventory;
  bool *showSkillWindow;
  int *hoveredNpc;
  int *hoveredMonster;
  int *hoveredGroundItem;
  int *selectedNpc;
  int16_t *potionBar; // [4]
  int8_t *skillBar;   // [10]
  int8_t *rmcSkillId;
  int *serverMP;
  int *serverAG; // AG for DK (separate from mana)
  bool *shopOpen;
  bool *isLearningSkill;
  int *heroCharacterId;
  std::vector<uint8_t> *learnedSkills;
  bool *rightMouseHeld;
  bool *showGameMenu;
  bool *teleportingToTown;
  float *teleportTimer;
  float teleportCastTime;
  bool *mountToggling;
  float *mountToggleTimer;
  float mountToggleTime;
  bool *questDialogOpen;
  int *questDialogNpcIndex;
  int *questDialogSelected;
  bool *showQuestLog;
  bool *mouseOverUIPanel; // Set each frame by main — true if cursor is over any panel
  bool *showCommandTerminal; // Enter opens, Escape closes
  bool *showMinimap;         // TAB toggles minimap overlay
  std::string dataPath;
};

namespace InputHandler {
void Init(const InputContext &ctx);
void RegisterCallbacks(GLFWwindow *window);
void ProcessInput(GLFWwindow *window, float deltaTime);
void ResetGameReady(); // Call on state transitions to prevent click bleed-through
int GetActiveQuickSlot(); // Returns 0-3 for active toggle slot, -1 if none
void RestoreQuickSlotState(); // Restore active slot from persisted rmcSkillId
} // namespace InputHandler

#endif // MU_INPUT_HANDLER_HPP
