#ifndef MU_CLIENT_PACKET_HANDLER_HPP
#define MU_CLIENT_PACKET_HANDLER_HPP

#include "CharacterSelect.hpp"
#include "ClientTypes.hpp"
#include "HeroCharacter.hpp"
#include "ItemDatabase.hpp"
#include "MonsterManager.hpp"
#include "NpcManager.hpp"
#include "Terrain.hpp"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

// Context struct that ClientPacketHandler uses to access game state.
// All pointers are owned by main.cpp — this is a non-owning view.
struct ClientGameState {
  HeroCharacter *hero = nullptr;
  MonsterManager *monsterManager = nullptr;
  NpcManager *npcManager = nullptr;
  VFXManager *vfxManager = nullptr;
  Terrain *terrain = nullptr;

  // Inventory (arrays owned by caller, size INVENTORY_SLOTS / 12)
  ClientInventoryItem *inventory = nullptr;
  ClientEquipSlot *equipSlots = nullptr;
  GroundItem *groundItems = nullptr;
  uint32_t *zen = nullptr;
  bool *syncDone = nullptr;

  bool *shopOpen = nullptr;
  std::vector<ShopItem> *shopItems = nullptr;
  std::map<int16_t, ClientItemDefinition> *itemDefs = nullptr;

  // Server-tracked character stats
  int *serverLevel = nullptr;
  int *serverHP = nullptr;
  int *serverMaxHP = nullptr;
  int *serverMP = nullptr;
  int *serverMaxMP = nullptr;
  int *serverAG = nullptr;
  int *serverMaxAG = nullptr;
  int *serverStr = nullptr;
  int *serverDex = nullptr;
  int *serverVit = nullptr;
  int *serverEne = nullptr;
  int *serverLevelUpPoints = nullptr;
  int64_t *serverXP = nullptr;
  int *serverDefense = nullptr;
  int *serverAttackSpeed = nullptr;
  int *serverMagicSpeed = nullptr;
  int16_t *potionBar = nullptr; // [3]
  int8_t *skillBar = nullptr;   // [10]
  int8_t *rmcSkillId = nullptr;
  int *heroCharacterId = nullptr;
  char *characterName = nullptr;
  std::vector<uint8_t> *learnedSkills = nullptr;

  // Quest state (synced from server) — per-quest tracking
  struct ActiveQuestClient {
    int questId = 0;
    int killCount[3] = {};
  };
  std::vector<ActiveQuestClient> *activeQuests = nullptr;
  uint64_t *completedQuestMask = nullptr;

  // Quest catalog (received from server — single source of truth)
  struct QuestCatalogTarget {
    uint8_t monType = 0;
    uint8_t killsReq = 0;
    std::string name;
  };
  struct QuestCatalogReward {
    int16_t defIndex = -1;
    uint8_t itemLevel = 0;
  };
  struct QuestCatalogEntry {
    uint8_t questId = 0;
    uint16_t guardType = 0;
    int targetCount = 0;
    QuestCatalogTarget targets[3];
    std::string questName, location, loreText;
    uint8_t recommendedLevel = 0;
    uint32_t zenReward = 0, xpReward = 0;
    QuestCatalogReward classReward[4][2];
  };
  std::vector<QuestCatalogEntry> *questCatalog = nullptr;

  // Active buffs (Elf auras)
  struct ActiveBuff {
    bool active = false;
    uint8_t type = 0;     // 1=Defense, 2=Damage
    int value = 0;
    float remaining = 0;  // Seconds left
    float maxDuration = 0;
  };
  ActiveBuff activeBuffs[2]; // [0]=Defense, [1]=Damage

  // Active debuffs (monster poison)
  bool poisoned = false;
  float poisonRemaining = 0.0f;
  float poisonMaxDuration = 0.0f;

  // Callbacks for main.cpp-specific functionality
  std::function<void(const glm::vec3 &, int, uint8_t)> spawnDamageNumber;
  std::function<int(uint8_t)> getBodyPartIndex;
  std::function<std::string(uint8_t, uint8_t)> getBodyPartModelFile;
  std::function<void(int16_t, glm::vec3 &, float &, float &)> getItemRestingAngle;
  std::function<void(const std::string &, const std::string &, int, int)>
      onQuestProgress; // questName, targetName, current, required
};

// Pending map change (set by packet handler, consumed by main loop)
struct PendingMapChange {
  bool pending = false;
  uint8_t mapId = 0;
  uint8_t spawnX = 0;
  uint8_t spawnY = 0;
};

namespace ClientPacketHandler {

// Initialize with game state context (must be called before handling packets)
void Init(ClientGameState *state);

// Parse a single packet from the initial server data burst
void HandleInitialPacket(const uint8_t *pkt, int pktSize, ServerData &out);

// Handle ongoing game packets (monster AI, combat, drops, stats)
void HandleGamePacket(const uint8_t *pkt, int pktSize);

// Handle character select packets (F3:00 charlist, F3:01 create, F3:02 delete)
void HandleCharSelectPacket(const uint8_t *pkt, int pktSize);

// Access active buffs (Elf auras) for HUD rendering
const ClientGameState::ActiveBuff *GetActiveBuffs();

// Access poison debuff state for HUD rendering
struct PoisonDebuffState {
  bool active;
  float remaining;
  float maxDuration;
};
PoisonDebuffState GetPoisonState();

// Reset internal state for character switch (clears s_initialStatsReceived, etc.)
void ResetForCharSwitch();

// Map change: check if server requested a map transition
PendingMapChange &GetPendingMapChange();

} // namespace ClientPacketHandler

#endif // MU_CLIENT_PACKET_HANDLER_HPP
