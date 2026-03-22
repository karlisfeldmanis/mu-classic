#ifndef MU_PACKET_DEFS_HPP
#define MU_PACKET_DEFS_HPP

#pragma once
#include <cstdint>
#include <cstring>

// =====================================================
// Character Classes
// =====================================================

enum class CharacterClass : uint8_t {
  CLASS_DW = 0,
  CLASS_DK = 16,
  CLASS_ELF = 32,
  CLASS_MG = 48
};

// =====================================================
// Packet Opcodes — named constants for all headcodes
// =====================================================

namespace Opcode {
// Auth (headcode 0xF1/0xF3 with subcodes)
constexpr uint8_t AUTH = 0xF1;       // sub 0x00=Welcome, 0x01=Login
constexpr uint8_t CHARSELECT = 0xF3; // sub 0x00=CharList, 0x03=CharSelect

// Sub-opcodes for AUTH (0xF1)
constexpr uint8_t SUB_WELCOME = 0x00;
constexpr uint8_t SUB_LOGIN = 0x01;

// Sub-opcodes for CHARSELECT (0xF3)
constexpr uint8_t SUB_CHARLIST = 0x00;
constexpr uint8_t SUB_CHARCREATE = 0x01;
constexpr uint8_t SUB_CHARDELETE = 0x02;
constexpr uint8_t SUB_CHARSELECT = 0x03;

// World & Viewport
constexpr uint8_t NPC_VIEWPORT = 0x13;
constexpr uint8_t NPC_MOVE = 0x14; // S->C: guard patrol movement
constexpr uint8_t POSITION = 0x15;
constexpr uint8_t MON_VIEWPORT_V1 = 0x1F;
constexpr uint8_t MON_RESPAWN = 0x30;
constexpr uint8_t MON_VIEWPORT_V2 = 0x34;
constexpr uint8_t MON_MOVE = 0x35;
constexpr uint8_t MOVE = 0xD4;
constexpr uint8_t PRECISE_POS = 0xD7;

// Character & Equipment
constexpr uint8_t EQUIPMENT = 0x24;
constexpr uint8_t CHARSTATS = 0x25;
constexpr uint8_t CHARSAVE = 0x26;
constexpr uint8_t EQUIP = 0x27;
constexpr uint8_t STAT_ALLOC = 0x37;
constexpr uint8_t STAT_ALLOC_RESULT = 0x38;

// Combat
constexpr uint8_t ATTACK = 0x28;
constexpr uint8_t DAMAGE = 0x29;
constexpr uint8_t MON_DEATH = 0x2A;
constexpr uint8_t MON_ATTACK = 0x2F;

// Inventory & Drops
constexpr uint8_t ITEM_DROP = 0x23; // C->S: Drop item from inventory
constexpr uint8_t DROP_SPAWN = 0x2B;
constexpr uint8_t PICKUP = 0x2C;
constexpr uint8_t PICKUP_RESULT = 0x2D;
constexpr uint8_t DROP_REMOVE = 0x2E;
constexpr uint8_t INV_SYNC = 0x36;
constexpr uint8_t INV_MOVE = 0x39;
constexpr uint8_t ITEM_USE = 0x3A;

// Shop
constexpr uint8_t SHOP_OPEN = 0x3B;        // C->S: open shop (npcType)
constexpr uint8_t SHOP_LIST = 0x3C;        // S->C: shop item list (C2)
constexpr uint8_t SHOP_BUY = 0x3D;         // C->S: buy request
constexpr uint8_t SHOP_BUY_RESULT = 0x3E;  // S->C: buy result
constexpr uint8_t SHOP_SELL = 0x3F;        // C->S: sell request
constexpr uint8_t SHOP_SELL_RESULT = 0x40; // S->C: sell result

// Skills
constexpr uint8_t SKILL_LIST = 0x41;     // S->C: learned skill list (C2)
constexpr uint8_t SKILL_USE = 0x42;      // C->S: skill attack
constexpr uint8_t SKILL_TELEPORT = 0x43; // C->S: teleport to position

// NPC Interaction
constexpr uint8_t NPC_INTERACT = 0x44; // C->S: guard/quest NPC interact (open/close)

// Summon system
constexpr uint8_t SUMMON_SPAWN = 0x45;   // S->C: summon pet spawned
constexpr uint8_t SUMMON_DESPAWN = 0x46; // S->C: summon pet despawned

// Buff system (Elf auras)
constexpr uint8_t BUFF_EFFECT = 0x47; // S->C: buff applied/expired

// Debuff system (monster poison)
constexpr uint8_t DEBUFF_EFFECT = 0x48; // S->C: debuff applied/expired

// Quest System
constexpr uint8_t QUEST = 0x50;              // Quest headcode
constexpr uint8_t SUB_QUEST_STATE = 0x00;    // S->C: current quest state
constexpr uint8_t SUB_QUEST_ACCEPT = 0x01;   // C->S: accept quest
constexpr uint8_t SUB_QUEST_COMPLETE = 0x02; // C->S: turn in quest
constexpr uint8_t SUB_QUEST_REWARD = 0x03;   // S->C: reward notification
constexpr uint8_t SUB_QUEST_ABANDON = 0x04;  // C->S: abandon active quest
constexpr uint8_t QUEST_CATALOG = 0x51;      // S->C: full quest catalog (C2)

// Map Transition
constexpr uint8_t MAP_CHANGE = 0x1C; // S->C: map transition

// Warp command
constexpr uint8_t WARP_COMMAND = 0x62;     // C->S: warp to map

// Chat log
constexpr uint8_t CHAT_LOG_HISTORY = 0x60; // S->C: chat history on login (C2)
constexpr uint8_t CHAT_LOG_SAVE = 0x61;    // C->S: save chat message

// Client settings
constexpr uint8_t CLIENT_SETTINGS = 0x63; // C->S: client settings (camera zoom)
} // namespace Opcode

// =====================================================
// Section 1: Wire Headers (C1/C2/C3/C4 framing)
// =====================================================

// =====================================================

#pragma pack(push, 1)

struct PBMSG_HEAD {
  uint8_t type;     // 0xC1
  uint8_t size;     // Total packet size
  uint8_t headcode; // Main opcode
};

struct PSBMSG_HEAD {
  uint8_t type;     // 0xC1
  uint8_t size;     // Total packet size
  uint8_t headcode; // Main opcode
  uint8_t subcode;  // Sub-opcode
};

struct PWMSG_HEAD {
  uint8_t type;     // 0xC2
  uint8_t sizeH;    // Size high byte (big-endian)
  uint8_t sizeL;    // Size low byte
  uint8_t headcode; // Main opcode
};

// =====================================================
// Section 2: Authentication & Character Selection
// =====================================================

// S->C: Welcome (F1:00)
struct PMSG_WELCOME_SEND {
  PSBMSG_HEAD h;  // C1:05:F1:00
  uint8_t result; // 0x01 = success
};

// C->S: Login (F1:01)
struct PMSG_LOGIN_RECV {
  PSBMSG_HEAD h; // C1/C3:F1:01
  char account[10];
  char password[20];
  uint32_t tickCount;
  uint8_t version[5];
  uint8_t serial[16];
};

// S->C: Login Result (F1:01)
struct PMSG_LOGIN_RESULT_SEND {
  PSBMSG_HEAD h;  // C1:05:F1:01
  uint8_t result; // 0x01=success, 0x00=fail, 0x02=already connected
};

// C->S: Character List Request — just PSBMSG_HEAD {0xC1, size, 0xF3, 0x00}

// S->C: Character List (F3:00)
struct PMSG_CHARLIST_HEAD {
  PSBMSG_HEAD h; // C1:F3:00
  uint8_t classCode;
  uint8_t moveCnt;
  uint8_t count;
};

struct PMSG_CHARLIST_ENTRY {
  uint8_t slot;
  char name[10];
  uint16_t level; // big-endian on wire
  uint8_t classCode; // 0=DW, 16=DK, 32=ELF, 48=MG
  uint8_t ctlCode;
  uint8_t charSet[20]; // [0]=class, [1-14]=equip 0-6, [15-16]=pet/mount, [17-18]=wings
  uint8_t guildStatus;
  uint8_t equipLevels[8]; // +0-15 per slot (RH, LH, Helm, Armor, Pants, Gloves, Boots, Wings)
};

// C->S: Character Create (F3:01)
struct PMSG_CHARCREATE_RECV {
  PSBMSG_HEAD h; // C1:F3:01
  char name[10];
  uint8_t classCode; // 0=DW, 16=DK, 32=ELF, 48=MG
};

// S->C: Character Create Result (F3:01)
struct PMSG_CHARCREATE_RESULT {
  PSBMSG_HEAD h;    // C1:F3:01
  uint8_t result;   // 0=fail, 1=success, 2=name taken
  char name[10];
  uint8_t slot;     // Assigned slot index (0-4)
  uint8_t classCode;
  uint16_t level;
};

// C->S: Character Delete (F3:02)
struct PMSG_CHARDELETE_RECV {
  PSBMSG_HEAD h; // C1:F3:02
  uint8_t slot;  // Slot index to delete (0-4)
  char name[10]; // Name confirmation
};

// S->C: Character Delete Result (F3:02)
struct PMSG_CHARDELETE_RESULT {
  PSBMSG_HEAD h;  // C1:F3:02
  uint8_t result; // 0=fail, 1=success
};

// C->S: Character Select (F3:03)
struct PMSG_CHARSELECT_RECV {
  PSBMSG_HEAD h; // C1:F3:03
  char name[10];
};

// S->C: Character Info (F3:03)
struct PMSG_CHARINFO_SEND {
  PSBMSG_HEAD h; // C1:F3:03
  uint8_t x;
  uint8_t y;
  uint8_t map;
  uint8_t dir;
  uint16_t level;
  uint8_t experience[8];
  uint8_t nextExperience[8];
  uint16_t levelUpPoint;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t mana;
  uint16_t maxMana;
  uint16_t attackSpeed;
  uint16_t magicSpeed;
  uint16_t defense;
  uint16_t shield;
  uint16_t maxShield;
  uint16_t bp;
  uint16_t maxBP;
  uint32_t money;
  uint8_t pkLevel;
  uint8_t ctlCode;
  uint16_t fruitAddPoint;
  uint16_t maxFruitAddPoint;
  uint16_t leadership;
  uint16_t fruitSubPoint;
  uint16_t maxFruitSubPoint;
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId;
  uint16_t cameraZoom; // Zoom * 10 (e.g. 8000 = 800.0f)
};

// C->S: Client settings update (camera zoom etc.)
struct PMSG_CLIENT_SETTINGS {
  PBMSG_HEAD h; // C1:0x63
  uint16_t cameraZoom; // Zoom * 10
};

// S->C: Map transition (0x1C)
struct PMSG_MAP_CHANGE_SEND {
  PBMSG_HEAD h; // C1:0x1C
  uint8_t mapId;   // 0=Lorencia, 1=Dungeon
  uint8_t spawnX;  // Grid X to spawn at
  uint8_t spawnY;  // Grid Y to spawn at
};

// =====================================================
// Section 3: Character Stats & Equipment
// =====================================================

// S->C: Character Equipment (0x24, C2 variable-length)
struct PMSG_EQUIPMENT_HEAD {
  PWMSG_HEAD h;  // C2:0x24 (4 bytes)
  uint8_t count; // Number of equipped slots (1 byte)
};

static_assert(sizeof(PMSG_EQUIPMENT_HEAD) == 5,
              "PMSG_EQUIPMENT_HEAD size mismatch");

struct PMSG_EQUIPMENT_SLOT {
  uint8_t slot;       // EquipSlot (0=right_hand, 1=left_hand, etc.)
  uint8_t category;   // ItemCategory (0=Sword, 1=Axe, etc.)
  uint8_t itemIndex;  // Index within category (0=Sword01, 1=Sword02)
  uint8_t itemLevel;  // Enhancement level (+0 to +15)
  char modelFile[32]; // Model file name (null-terminated, 32 chars max)
};

static_assert(sizeof(PMSG_EQUIPMENT_SLOT) == 36,
              "PMSG_EQUIPMENT_SLOT size mismatch");

// S->C: Character Stats (0x25)
struct PMSG_CHARSTATS_SEND {
  PBMSG_HEAD h; // C1:0x25
  uint16_t characterId;
  char name[32];
  uint16_t level;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t mana;
  uint16_t maxMana;
  uint16_t attackSpeed;
  uint16_t magicSpeed;
  uint16_t defense;
  uint16_t levelUpPoints;
  uint32_t experienceLo;
  uint32_t experienceHi;
  uint8_t charClass;
  uint16_t ag;
  uint16_t maxAg;
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId;
};

// C->S: Character Save (0x26)
struct PMSG_CHARSAVE_RECV {
  PBMSG_HEAD h; // C1:0x26
  uint16_t characterId;
  uint16_t level;
  uint16_t strength;
  uint16_t dexterity;
  uint16_t vitality;
  uint16_t energy;
  uint16_t life;
  uint16_t maxLife;
  uint16_t mana;
  uint16_t maxMana;
  uint16_t ag;
  uint16_t maxAg;
  uint16_t levelUpPoints;
  uint32_t experienceLo;
  uint32_t experienceHi;
  int8_t skillBar[10];
  int16_t potionBar[4];
  int8_t rmcSkillId;
};

// C->S: Equipment Change (0x27)
struct PMSG_EQUIP_RECV {
  PBMSG_HEAD h; // C1:0x27
  uint16_t characterId;
  uint8_t slot;      // EquipSlot (0=right_hand, 1=left_hand, 3=armor, 6=boots)
  uint8_t category;  // ItemCategory
  uint8_t itemIndex; // Index within category
  uint8_t itemLevel; // Enhancement +0..+15
};

// C->S: Stat Allocation Request (0x37)
struct PMSG_STAT_ALLOC_RECV {
  PBMSG_HEAD h;     // C1:0x37
  uint8_t statType; // 0=STR, 1=DEX, 2=VIT, 3=ENE
};

// S->C: Stat Allocation Response (0x38)
struct PMSG_STAT_ALLOC_SEND {
  PBMSG_HEAD h;           // C1:0x38
  uint8_t result;         // 1=success, 0=fail (no points)
  uint8_t statType;       // Which stat was incremented
  uint16_t newValue;      // New stat value
  uint16_t levelUpPoints; // Remaining points
  uint16_t maxLife;       // Updated maxLife (VIT affects HP)
  uint16_t ag;            // Remaining AG
  uint16_t maxAg;         // Updated maxAg (any stat affects DK AG)
};

// =====================================================
// Section 4: Combat
// =====================================================

// C->S: Attack Request (0x28)
struct PMSG_ATTACK_RECV {
  PBMSG_HEAD h;          // C1:0x28
  uint16_t monsterIndex; // Target monster unique index
};

// C->S: Skill Attack Request (0x42)
struct PMSG_SKILL_ATTACK_RECV {
  PBMSG_HEAD h; // C1:0x42
  uint16_t monsterIndex;
  uint8_t skillId;
  float targetX = 0; // Ground-cast AoE: world position of click
  float targetZ = 0;
};

// C->S: Teleport to Position (0x43)
struct PMSG_SKILL_TELEPORT_RECV {
  PBMSG_HEAD h; // C1:0x43
  uint8_t targetGridX;
  uint8_t targetGridY;
};

// C->S: Warp Command (0x62)
struct PMSG_WARP_COMMAND_RECV {
  PBMSG_HEAD h; // C1:0x62
  uint8_t mapId;
  uint8_t spawnX; // 0 = use default spawn
  uint8_t spawnY;
};

// S->C: Damage Result (0x29)
struct PMSG_DAMAGE_SEND {
  PBMSG_HEAD h; // C1:0x29
  uint16_t monsterIndex;
  uint16_t damage;
  uint8_t damageType; // 0=miss, 1=normal, 2=critical, 3=excellent
  uint16_t remainingHp;
  uint16_t attackerCharId;
  uint16_t attackerMonsterIndex = 0; // Non-zero = summon attacking this monster
};

// S->C: Monster Death + XP (0x2A)
struct PMSG_MONSTER_DEATH_SEND {
  PBMSG_HEAD h; // C1:0x2A
  uint16_t monsterIndex;
  uint16_t killerCharId;
  uint32_t xpReward;
};

// S->C: Monster Attack Player (0x2F)
struct PMSG_MONSTER_ATTACK_SEND {
  PBMSG_HEAD h; // C1:0x2F
  uint16_t monsterIndex;
  float damage;
  float remainingHp;
};

// =====================================================
// Section 5: Inventory & Drops
// =====================================================

// S->C: Inventory Sync Item (0x36, C2 variable-length)
struct PMSG_INVENTORY_ITEM {
  uint8_t slot;      // 0-63
  uint8_t category;  // Category (0-15)
  uint8_t itemIndex; // Index (0-31)
  uint8_t quantity;
  uint8_t itemLevel;
};

// C->S: Inventory Move Request (0x39)
struct PMSG_INVENTORY_MOVE_RECV {
  PBMSG_HEAD h; // C1:0x39
  uint8_t fromSlot;
  uint8_t toSlot;
};

// C->S: Item Use Request (0x3A)
struct PMSG_ITEM_USE_RECV {
  PBMSG_HEAD h; // C1:0x3A
  uint8_t slot; // 0-63
};

// C->S: Drop Item from Inventory (0x23)
struct PMSG_ITEM_DROP_RECV {
  PBMSG_HEAD h; // C1:0x23
  uint8_t slot; // Inventory bag slot (0-63)
};

// S->C: Ground Drop Spawned (0x2B)
struct PMSG_DROP_SPAWN_SEND {
  PBMSG_HEAD h;       // C1:0x2B
  uint16_t dropIndex; // Unique drop ID
  int16_t defIndex;   // -1=Zen, 0-511+=item def index
  uint8_t quantity;
  uint8_t itemLevel; // Enhancement +0..+2
  float worldX;
  float worldZ;
};

// C->S: Pickup Request (0x2C)
struct PMSG_PICKUP_RECV {
  PBMSG_HEAD h; // C1:0x2C
  uint16_t dropIndex;
};

// S->C: Pickup Result (0x2D)
struct PMSG_PICKUP_RESULT_SEND {
  PBMSG_HEAD h; // C1:0x2D
  uint16_t dropIndex;
  int16_t defIndex; // -1=Zen, 0-511+=item def index
  uint8_t quantity;
  uint8_t itemLevel;
  uint8_t success; // 1=ok, 0=already taken or bag full
};

// S->C: Drop Removed (0x2E)
struct PMSG_DROP_REMOVE_SEND {
  PBMSG_HEAD h; // C1:0x2E
  uint16_t dropIndex;
};

// =====================================================
// Section 5b: Shop
// =====================================================

// C->S: Open Shop (0x3B)
struct PMSG_SHOP_OPEN_RECV {
  PBMSG_HEAD h;     // C1:0x3B
  uint16_t npcType; // NPC type (250/251/253/254/255)
};

// C->S: NPC Interact (0x44)
struct PMSG_NPC_INTERACT_RECV {
  PBMSG_HEAD h;     // C1:0x44
  uint16_t npcType; // NPC type (249=guard, etc.)
  uint8_t action;   // 1=open dialog, 0=close dialog
};

// S->C: Shop item entry (part of C2 list)
struct PMSG_SHOP_ITEM {
  int16_t defIndex;  // item defIndex (cat*32+idx)
  uint8_t itemLevel; // +0 by default
  uint32_t buyPrice; // price to buy
};

// C->S: Buy Request (0x3D)
struct PMSG_SHOP_BUY_RECV {
  PBMSG_HEAD h;       // C1:0x3D
  int16_t defIndex;   // which item to buy
  uint8_t itemLevel;  // which level variant
  uint8_t quantity;   // 1 for equipment, 1+ for stackables
  uint8_t targetSlot; // 0xFF = auto, else specific bag slot
};

// S->C: Buy Result (0x3E)
struct PMSG_SHOP_BUY_RESULT_SEND {
  PBMSG_HEAD h;   // C1:0x3E
  uint8_t result; // 1=ok, 0=fail
  int16_t defIndex;
  uint8_t quantity;
};

// C->S: Sell Request (0x3F)
struct PMSG_SHOP_SELL_RECV {
  PBMSG_HEAD h;    // C1:0x3F
  uint8_t bagSlot; // inventory slot to sell
};

// S->C: Sell Result (0x40)
struct PMSG_SHOP_SELL_RESULT_SEND {
  PBMSG_HEAD h;       // C1:0x40
  uint8_t result;     // 1=ok, 0=fail
  uint8_t bagSlot;    // which slot was sold
  uint32_t zenGained; // how much zen received
};

// =====================================================
// Section 6: World & Viewport
// =====================================================

// S->C: NPC Viewport (0x13, C2 variable-length)
struct PMSG_VIEWPORT_HEAD {
  PWMSG_HEAD h; // C2:0x13
  uint8_t count;
};

struct PMSG_VIEWPORT_NPC {
  uint8_t indexH;   // Object index high (bit 7 = create flag)
  uint8_t indexL;   // Object index low
  uint8_t typeH;    // NPC type high
  uint8_t typeL;    // NPC type low
  uint8_t x;        // Grid X
  uint8_t y;        // Grid Y
  uint8_t tx;       // Target X (same as x for static)
  uint8_t ty;       // Target Y (same as y for static)
  uint8_t dirAndPk; // (dir << 4) | pkLevel
};

// S->C: Monster Viewport V1 (0x1F)
struct PMSG_MONSTER_VIEWPORT_HEAD {
  PBMSG_HEAD h; // C1:0x1F
  uint8_t count;
};

struct PMSG_MONSTER_VIEWPORT_ENTRY {
  uint8_t typeH; // Monster type high
  uint8_t typeL; // Monster type low
  uint8_t x;     // Grid X
  uint8_t y;     // Grid Y
  uint8_t dir;   // Direction (0-7)
};

// S->C: Monster Viewport V2 (0x34, C2 variable-length)
struct PMSG_MONSTER_VIEWPORT_ENTRY_V2 {
  uint8_t indexH; // Monster unique index high
  uint8_t indexL; // Monster unique index low
  uint8_t typeH;  // Monster type high
  uint8_t typeL;  // Monster type low
  uint8_t x;      // Grid X
  uint8_t y;      // Grid Y
  uint8_t dir;    // Direction (0-7)
  uint16_t hp;    // Current HP
  uint16_t maxHp; // Max HP
  uint8_t state;  // 0=alive, 1=dying, 2=dead
  uint8_t level;  // Monster level (from server)
};

// S->C: Monster Move/Chase (0x35)
struct PMSG_MONSTER_MOVE_SEND {
  PBMSG_HEAD h; // C1:0x35
  uint16_t monsterIndex;
  uint8_t targetX; // Target grid X
  uint8_t targetY; // Target grid Y
  uint8_t chasing; // 1=chasing player, 0=idle/returning
};

// S->C: NPC (Guard) Movement (0x14)
struct PMSG_NPC_MOVE_SEND {
  PBMSG_HEAD h; // C1:0x14
  uint16_t npcIndex;
  uint8_t targetX; // Target grid X
  uint8_t targetY; // Target grid Y
};

// S->C: Monster Respawn (0x30)
struct PMSG_MONSTER_RESPAWN_SEND {
  PBMSG_HEAD h; // C1:0x30
  uint16_t monsterIndex;
  uint8_t x, y; // New grid position
  uint16_t hp;
};

// C->S: Movement (0xD4)
struct PMSG_MOVE_RECV {
  PBMSG_HEAD h; // C1:0xD4
  uint8_t x;
  uint8_t y;
  uint8_t path[8];
};

// C->S: Precise Position (0xD7)
struct PMSG_PRECISE_POS_RECV {
  PBMSG_HEAD h; // C1:0xD7
  float worldX;
  float worldZ;
};

// S->C: Position Update (0x15)
struct PMSG_POSITION_SEND {
  PBMSG_HEAD h; // C1:0x15
  uint8_t indexH;
  uint8_t indexL;
  uint8_t x;
  uint8_t y;
};

// =====================================================
// Section 6b: Quest System
// =====================================================

// S->C: Quest state sync (0x50:0x00) — variable-length C1 packet
// Layout: PSBMSG_HEAD(4) + completedMask(4) + activeCount(1) + entries(4*N)
// Parsed from raw bytes; no fixed struct.

// C->S: Accept quest (0x50:0x01)
struct PMSG_QUEST_ACCEPT_SEND {
  PSBMSG_HEAD h;   // C1:0x50:0x01
  uint8_t questId;
};

// C->S: Complete quest (0x50:0x02)
struct PMSG_QUEST_COMPLETE_SEND {
  PSBMSG_HEAD h;   // C1:0x50:0x02
  uint8_t questId;
};

// C->S: Abandon quest (0x50:0x04)
struct PMSG_QUEST_ABANDON_SEND {
  PSBMSG_HEAD h;   // C1:0x50:0x04
  uint8_t questId;
};

// S->C: Reward notification (0x50:0x03)
struct PMSG_QUEST_REWARD_SEND {
  PSBMSG_HEAD h;         // C1:0x50:0x03
  uint32_t zenReward;
  uint32_t xpReward;
  uint8_t questId;
};

// S->C: Quest Catalog (0x51, C2 variable-length)
// Wire: PWMSG_HEAD(4) + count(1) + PMSG_QUEST_CATALOG_ENTRY[count]
struct PMSG_QUEST_CATALOG_TARGET {
  uint8_t monsterType;
  uint8_t killsRequired;
  char name[24];
};

struct PMSG_QUEST_CATALOG_REWARD {
  int16_t defIndex; // -1 = no item
  uint8_t itemLevel;
};

struct PMSG_QUEST_CATALOG_ENTRY {
  uint8_t questId;
  uint16_t guardNpcType;
  uint8_t targetCount;
  PMSG_QUEST_CATALOG_TARGET targets[3];
  char questName[32];
  char location[16];
  uint8_t recommendedLevel;
  uint32_t zenReward;
  uint32_t xpReward;
  PMSG_QUEST_CATALOG_REWARD classReward[4][2];
  char loreText[256];
};

// =====================================================
// Section 6c: Summon System
// =====================================================

// S->C: Summon Spawn (0x45)
struct PMSG_SUMMON_SPAWN_SEND {
  PBMSG_HEAD h; // C1:0x45
  uint16_t monsterIndex;
  uint16_t ownerCharId;
  uint16_t level; // Scaled level (matches owner level)
};

// S->C: Summon Despawn (0x46)
struct PMSG_SUMMON_DESPAWN_SEND {
  PBMSG_HEAD h; // C1:0x46
  uint16_t monsterIndex;
};

// =====================================================
// Section 6d: Buff System (Elf Auras)
// =====================================================

// S->C: Buff Effect (0x47) — buff applied or expired
struct PMSG_BUFF_EFFECT_SEND {
  PBMSG_HEAD h;       // C1:0x47
  uint8_t buffType;   // 0=Heal, 1=GreaterDefense, 2=GreaterDamage
  uint8_t active;     // 1=applied, 0=expired
  uint16_t value;     // Heal: HP restored. Def/Dmg: stat bonus amount
  float duration;     // Seconds remaining (0 for heal)
};

// =====================================================
// Section 6e: Debuff System (Monster Poison)
// =====================================================

// S->C: Debuff Effect (0x48) — debuff applied or expired
struct PMSG_DEBUFF_EFFECT_SEND {
  PBMSG_HEAD h;       // C1:0x48
  uint8_t debuffType; // 1=Poison
  uint8_t active;     // 1=applied, 0=expired
  float duration;     // Seconds remaining
};

// =====================================================
// Section 7: Helper Functions
// =====================================================

inline void SetWordBE(uint8_t *dst, uint16_t val) {
  dst[0] = static_cast<uint8_t>(val >> 8);
  dst[1] = static_cast<uint8_t>(val & 0xFF);
}

inline uint16_t GetWordBE(const uint8_t *src) {
  return (static_cast<uint16_t>(src[0]) << 8) | src[1];
}

inline void SetDwordBE(uint8_t *dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val >> 24);
  dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
  dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
  dst[3] = static_cast<uint8_t>(val & 0xFF);
}

inline PBMSG_HEAD MakeC1Header(uint8_t size, uint8_t headcode) {
  return {0xC1, size, headcode};
}

inline PSBMSG_HEAD MakeC1SubHeader(uint8_t size, uint8_t headcode,
                                   uint8_t subcode) {
  return {0xC1, size, headcode, subcode};
}

inline PWMSG_HEAD MakeC2Header(uint16_t size, uint8_t headcode) {
  PWMSG_HEAD h;
  h.type = 0xC2;
  h.sizeH = (size >> 8) & 0xFF;
  h.sizeL = size & 0xFF;
  h.headcode = headcode;
  return h;
}

// BUX decode for account/password
inline void BuxDecode(char *data, int len) {
  static const uint8_t buxCode[3] = {0xFC, 0xCF, 0xAB};
  for (int i = 0; i < len; i++) {
    data[i] ^= buxCode[i % 3];
  }
}

#pragma pack(pop)
#endif // MU_PACKET_DEFS_HPP
