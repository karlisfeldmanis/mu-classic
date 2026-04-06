#ifndef MU_DATABASE_HPP
#define MU_DATABASE_HPP

#include <cstdint>
#include <sqlite3.h>
#include <string>
#include <vector>
#include "PacketDefs.hpp"

struct NpcSpawnData {
  int id = 0;
  uint16_t type = 0; // NPC type ID (253=Amy, 250=Merchant, etc.)
  uint8_t mapId = 0; // 0=Lorencia
  uint8_t posX = 0;
  uint8_t posY = 0;
  uint8_t direction = 0; // 0-7
  std::string name;
};

struct MonsterSpawnData {
  int id = 0;
  uint16_t type = 0; // Monster type (3=Spider, etc.)
  uint8_t mapId = 0;
  uint8_t posX = 0;
  uint8_t posY = 0;
  uint8_t direction = 0;
};

struct CharacterData {
  int id = 0;
  int accountId = 0;
  int slot = 0;
  std::string name;
  uint8_t charClass = 0; // 0=DW, 16=DK, 32=Elf
  uint16_t level = 1;
  uint8_t mapId = 0;
  uint8_t posX = 130;
  uint8_t posY = 130;
  uint8_t direction = 2;
  uint16_t strength = 20;
  uint16_t dexterity = 20;
  uint16_t vitality = 20;
  uint16_t energy = 20;
  uint16_t life = 100;
  uint16_t maxLife = 100;
  uint16_t mana = 50;
  uint16_t maxMana = 50;
  uint16_t ag = 50;
  uint16_t maxAg = 50;
  uint32_t money = 0;
  uint64_t experience = 0;
  uint16_t levelUpPoints = 0;
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId = -1;
  int16_t summonType = -1; // -1 = no active summon
  uint16_t cameraZoom = 8000; // Zoom * 10 (default 800.0)
  // Elf buff aura persistence
  uint8_t buffDefType = 0;    // 0=inactive, 1=defense
  float buffDefRemaining = 0; // Seconds left
  int buffDefValue = 0;
  uint8_t buffDmgType = 0;    // 0=inactive, 2=damage
  float buffDmgRemaining = 0;
  int buffDmgValue = 0;
  uint16_t resets = 0;        // Reset counter
  uint8_t role = 0;           // 0=player, 1=gm, 2=admin, 3=bot
};

// Equipment slot constants (matching original MU inventory layout)
enum EquipSlot : uint8_t {
  EQUIP_RIGHT_HAND = 0,
  EQUIP_LEFT_HAND = 1,
  EQUIP_HELM = 2,
  EQUIP_ARMOR = 3,
  EQUIP_PANTS = 4,
  EQUIP_GLOVES = 5,
  EQUIP_BOOTS = 6,
};

// Item category constants (matching original MU item groups)
enum ItemCategory : uint8_t {
  ITEM_SWORD = 0,
  ITEM_AXE = 1,
  ITEM_MACE = 2,
  ITEM_SPEAR = 3,
  ITEM_BOW = 4,
  ITEM_STAFF = 5,
  ITEM_SHIELD = 6,
};

struct ItemDefinition {
  int id = 0;
  uint8_t category = 0;  // ItemCategory
  uint8_t itemIndex = 0; // Index within category (0=Sword01, 1=Sword02...)
  std::string name;
  std::string modelFile; // e.g. "Sword01.bmd"
  uint16_t level = 0;    // Level requirement
  uint16_t damageMin = 0;
  uint16_t damageMax = 0;
  uint16_t defense = 0; // Base defense (for shields/armor)
  uint8_t attackSpeed = 0;
  uint8_t twoHanded = 0;
  uint8_t width = 1;
  uint8_t height = 1;
  uint16_t reqStrength = 0;
  uint16_t reqDexterity = 0;
  uint16_t reqVitality = 0;
  uint16_t reqEnergy = 0;
  uint32_t classFlags = 0xFFFFFFFF;
  uint32_t buyPrice = 0;
  uint16_t magicPower = 0;  // Staff Rise base value (OpenMU Weapons.cs)
};

struct EquipmentSlot {
  uint8_t slot = 0;         // EquipSlot
  uint8_t category = 0;     // ItemCategory
  uint8_t itemIndex = 0;    // Index within category
  uint8_t itemLevel = 0;    // +0 to +15 enhancement
  uint8_t quantity = 0;     // Stack count (arrows/bolts)
  uint8_t optionFlags = 0;  // bit7=Skill, bit6=Luck, bits0-2=Additional(0-7)
};

struct ItemDropInfo {
  uint8_t category;
  uint8_t itemIndex;
  std::string name;
  uint16_t level;
};

class Database {
public:
  bool Open(const std::string &dbPath);
  void Close();

  // Returns accountId on success, 0 on failure
  int ValidateLogin(const std::string &username, const std::string &password);

  std::vector<CharacterData> GetCharacterList(int accountId);
  CharacterData GetCharacter(const std::string &name);
  CharacterData GetCharacterById(int id);

  // Character creation/deletion
  bool CharacterNameExists(const std::string &name);
  int CreateCharacter(int accountId, const std::string &name, uint8_t classCode);
  bool DeleteCharacter(int accountId, int charId);

  void UpdatePosition(int charId, uint8_t x, uint8_t y, int mapId = -1);
  void UpdateCameraZoom(int charId, uint16_t zoom);
  void UpdateCharacterStats(int charId, uint16_t level, uint16_t strength,
                            uint16_t dexterity, uint16_t vitality,
                            uint16_t energy, uint16_t life, uint16_t maxLife,
                            uint16_t mana, uint16_t maxMana, uint16_t ag,
                            uint16_t maxAg, uint16_t levelUpPoints,
                            uint64_t experience, const int8_t *skillBar,
                            const int16_t *potionBar, int8_t rmcSkillId);
  void SaveCharacterFull(int charId, uint16_t level, uint16_t strength,
                         uint16_t dexterity, uint16_t vitality, uint16_t energy,
                         uint16_t life, uint16_t maxLife, uint16_t mana,
                         uint16_t maxMana, uint16_t ag, uint16_t maxAg,
                         uint16_t levelUpPoints, uint64_t experience,
                         uint32_t money, uint8_t posX, uint8_t posY,
                         uint8_t mapId, const int8_t *skillBar,
                         const int16_t *potionBar, int8_t rmcSkillId,
                         int16_t summonType = -1,
                         const struct Session *session = nullptr);
  void CreateDefaultAccount();

  // NPC spawns
  std::vector<NpcSpawnData> GetNpcSpawns(uint8_t mapId);
  void SeedNpcSpawns();

  // Monster spawns
  std::vector<MonsterSpawnData> GetMonsterSpawns(uint8_t mapId);
  void SeedMonsterSpawns();

  // Items and equipment
  void SeedItemDefinitions();
  ItemDefinition GetItemDefinition(uint8_t category, uint8_t itemIndex);
  ItemDefinition GetItemDefinition(int id);
  std::vector<ItemDefinition> GetAllItemDefinitions();
  std::vector<ItemDropInfo> GetItemsByLevelRange(int minLevel, int maxLevel);
  std::vector<EquipmentSlot> GetCharacterEquipment(int characterId);
  void SeedDefaultEquipment(int characterId);

  void UpdateEquipment(int characterId, uint8_t slot, uint8_t category,
                       uint8_t itemIndex, uint8_t itemLevel,
                       uint8_t quantity = 0, uint8_t optionFlags = 0);

  // Inventory bag (8x8 grid of picked-up items)
  struct InventorySlotData {
    uint8_t slot;
    int16_t defIndex;
    uint8_t quantity;
    uint8_t itemLevel;
    uint8_t optionFlags = 0;
  };
  std::vector<InventorySlotData> GetCharacterInventory(int characterId);
  void SaveCharacterInventory(int characterId, int16_t defIndex,
                              uint8_t quantity, uint8_t itemLevel,
                              uint8_t slot, uint8_t optionFlags = 0);
  void ClearCharacterInventory(int characterId);
  void DeleteCharacterInventoryItem(int characterId, uint8_t slot);
  void DeleteCharacterInventoryAll(int characterId);

  // Money / Zen
  void UpdateCharacterMoney(int characterId, uint32_t money);

  // Experience
  static uint64_t GetXPForLevel(int level);

  // Character skills
  std::vector<uint8_t> GetCharacterSkills(int characterId);
  void LearnSkill(int characterId, uint8_t skillId);
  bool HasSkill(int characterId, uint8_t skillId);
  void SetRmcSkillId(int characterId, int8_t skillId);

  // Quest system — per-quest tracking
  struct QuestProgress {
    int questId = 0;
    int kc[3] = {};
    bool completed = false;
  };
  std::vector<QuestProgress> LoadAllQuestProgress(int characterId);
  void SaveQuestProgress(int characterId, int questId, int kc0, int kc1, int kc2, bool completed);
  void DeleteQuestProgress(int characterId, int questId);

  // Quest definitions (DB-driven)
  struct QuestTargetData {
    uint8_t monsterType = 0;
    uint8_t killsRequired = 0;
  };
  struct QuestRewardData {
    int16_t defIndex = -1;
    uint8_t itemLevel = 0;
  };
  struct QuestDefData {
    int questId = 0;
    uint16_t guardNpcType = 0;
    std::string questName;
    std::string location;
    std::string loreText;
    int targetCount = 0;
    QuestTargetData targets[3];
    QuestRewardData classReward[4][2]; // [classIdx][rewardSlot]
  };
  void SeedQuests();
  std::vector<QuestDefData> LoadAllQuests();

  // Class definitions (for character creation UI)
  void SeedClassDefinitions();
  std::vector<PMSG_CLASS_DEF_ENTRY> GetClassDefinitions();

  // Chat log persistence
  struct ChatLogEntry {
    uint8_t category;
    uint32_t color;
    std::string message;
  };
  void SaveChatMessage(int characterId, uint8_t category, uint32_t color,
                       const std::string &message);
  std::vector<ChatLogEntry> GetChatHistory(int characterId, int limit = 200);

  // Reopen DB connection (after external file replacement)
  void Reopen();

private:
  void CreateTables();
  sqlite3 *m_db = nullptr;
  std::string m_dbPath;
};

#endif // MU_DATABASE_HPP
