#ifndef MU_SESSION_HPP
#define MU_SESSION_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Session {
public:
  explicit Session(int fd);
  ~Session();

  int GetFd() const { return m_fd; }
  bool IsAlive() const { return m_alive; }

  // Returns complete packets extracted from recv buffer
  // Each vector<uint8_t> is one complete MU packet
  std::vector<std::vector<uint8_t>> ReadPackets();

  // Queue data to send
  void Send(const void *data, size_t len);

  // Flush send buffer to socket. Returns false if connection lost.
  bool FlushSend();

  // Mark session for removal
  void Kill() { m_alive = false; }

  // Session state
  int accountId = 0;
  int characterId = 0;
  std::string characterName;
  uint8_t charClass = 0;
  bool inWorld = false;
  bool inCharSelect = true;
  bool mapLoading = false; // True during map transition — monsters ignore this player

  // Cached combat stats (populated on char select / equip change)
  uint16_t strength = 0;
  uint16_t energy = 0;

  // Combat stats
  uint8_t classCode = 0; // 0=DW, 1=DK, 2=ELF, 3=MG
  int weaponDamageMin = 0;
  int weaponDamageMax = 0;
  int minMagicDamage = 0;
  int maxMagicDamage = 0;
  int staffRisePercent = 0;  // Staff Rise % (magicPower-based, includes level bonus)

  int attackSpeed = 0;
  int attackRate = 0;
  int defenseRate = 0;

  int totalDefense = 0;
  bool hasBow = false;
  bool hasTwoHandedWeapon = false;

  // Pet companion bonuses (applied when category 13 pet is in slot 8)
  int petBonusMaxHp = 0;           // +50 for Guardian Angel
  float petDamageReduction = 0.0f; // 0.2 for Guardian Angel (20%)
  float petAttackMultiplier = 1.0f; // 1.3 for Imp (30% increase)

  // Server-authoritative HP tracking (monsters stop attacking dead players)
  int hp = 0;
  int maxHp = 0;
  int mana = 0;
  int maxMana = 0;
  int ag = 0;
  int maxAg = 0;
  bool dead = false;

  // Full character stats (for stat allocation validation)
  uint16_t dexterity = 0;
  uint16_t vitality = 0;
  uint16_t level = 1;
  uint16_t levelUpPoints = 0;
  uint64_t experience = 0;

  // Equipment slots (12 slots: right hand, left hand, helm, armor, pants,
  // gloves, boots, wings, pet, pendant, ring1, ring2)
  static constexpr int NUM_EQUIP_SLOTS = 12;
  struct EquippedItem {
    uint8_t category = 0xFF; // 0xFF = empty
    uint8_t itemIndex = 0;
    uint8_t itemLevel = 0;
    uint8_t quantity = 0;     // Stack count (arrows/bolts)
    uint8_t optionFlags = 0;  // bit7=Skill, bit6=Luck, bits0-2=Additional(0-7)
  };
  std::array<EquippedItem, NUM_EQUIP_SLOTS> equipment{};

  // Inventory bag (8x8 = 64 slots)
  struct InventoryItem {
    int16_t defIndex = -2; // -2=empty, matches primary slot
    uint8_t category = 0;
    uint8_t itemIndex = 0;
    uint8_t quantity = 0;
    uint8_t itemLevel = 0;
    uint8_t optionFlags = 0;  // bit7=Skill, bit6=Luck, bits0-2=Additional(0-7)
    bool occupied = false; // true if any part of an item is here
    bool primary = false;  // true if this is the top-left root slot
  };
  std::array<InventoryItem, 64> bag{};
  uint32_t zen = 0;

  // World position (updated from move packets, used for server AI aggro)
  float worldX = 0.0f;
  float worldZ = 0.0f;
  uint8_t mapId = 0; // 0=Lorencia, 1=Dungeon
  float gateTransitionCooldown = 0.0f; // Seconds until gate detection re-enables
  float pendingViewportDelay = 0.0f;   // Seconds until deferred viewport send after map change

  // Potion cooldown timer (seconds)
  float potionCooldown = 0.0f;
  float hpRemainder = 0.0f;     // Fractional HP for safe zone regeneration
  float idleHpRemainder = 0.0f; // Fractional HP for idle world regeneration
  float idleTimer = 0.0f;       // Seconds since player last moved (for idle regen)
  float manaRemainder = 0.0f; // Fractional mana for regeneration
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId = -1;
  int shopNpcType = -1; // -1 means no shop is open

  // AG logic timers
  float agRegenTimer = 0.0f;
  uint32_t lastAgUseTime = 0;

  // Server-side attack rate limiter (prevents speed hack / GCD bypass)
  float attackCooldown = 0.0f; // Seconds until next attack is allowed

  // Monster→player poison debuff (OpenMU: Poison Bull type 8, Larva type 12)
  // DoT: 3% of current HP every 3 seconds for ~20 seconds
  bool poisoned = false;
  float poisonTickTimer = 0.0f; // Accumulator for 3-second tick interval
  float poisonDuration = 0.0f;  // Remaining poison duration

  // Monster→player freeze debuff (Main 5.2: Ice Monster type 22, Ice Queen type 25)
  // 50% move speed, 50% animation speed, ~1.3s duration, blue tint
  bool frozen = false;
  float frozenDuration = 0.0f; // Remaining freeze duration

  // Learned skills (skill IDs)
  std::vector<uint8_t> learnedSkills;

  // Elf summon pet
  uint16_t activeSummonIndex = 0; // Monster index of active summon (0 = none)
  int16_t activeSummonType = -1;  // Monster type of active summon (-1 = none)
  uint16_t cameraZoom = 8000;     // Camera zoom * 10 (persisted per character)
  uint16_t attackTargetMonsterIdx = 0; // Last monster player attacked (for summon assist)
  bool wasInSafeZone = false; // Previous tick safe zone state (for despawn/respawn transitions)

  // Active buffs (Elf auras: Greater Defense, Greater Damage)
  struct ActiveBuff {
    uint8_t type = 0;     // 1=Defense, 2=Damage
    float remaining = 0;  // Seconds left
    int value = 0;        // Stat bonus amount
    bool active = false;
  };
  ActiveBuff buffs[2]; // [0]=Defense, [1]=Damage (Heal is instant, no tracking)

  // Quest system — per-quest tracking (replaces chain system)
  struct ActiveQuest {
    int questId = 0;
    int killCount[3] = {};
  };
  std::vector<ActiveQuest> activeQuests; // Accepted, not yet completed
  uint64_t completedQuestMask = 0;      // Bitmask: bit N = quest N done (0-33)

  uint16_t resets = 0;   // Reset counter (persisted)
  uint8_t role = 0;      // 0=player, 1=gm, 2=admin, 3=bot

private:
  int m_fd;
  bool m_alive = true;

  // Recv buffer — accumulates partial packets
  std::vector<uint8_t> m_recvBuf;

  // Send buffer — queued outgoing data
  std::vector<uint8_t> m_sendBuf;
};

#endif // MU_SESSION_HPP
