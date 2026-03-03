#ifndef MU_INPUT_HANDLER_HPP
#define MU_INPUT_HANDLER_HPP

#include <cstdint>
#include <vector>

struct GLFWwindow;
class Camera;
class HeroCharacter;
class ServerConnection;
class ClickEffect;
class NpcManager;
class MonsterManager;
struct GroundItem;
struct UICoords;

struct InputContext {
  HeroCharacter *hero;
  Camera *camera;
  ClickEffect *clickEffect;
  ServerConnection *server;
  MonsterManager *monsterMgr;
  NpcManager *npcMgr;
  GroundItem *groundItems;
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
};

namespace InputHandler {
void Init(const InputContext &ctx);
void RegisterCallbacks(GLFWwindow *window);
void ProcessInput(GLFWwindow *window, float deltaTime);
void ResetGameReady(); // Call on state transitions to prevent click bleed-through
} // namespace InputHandler

#endif // MU_INPUT_HANDLER_HPP
